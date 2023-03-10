// use #include "" only for your local include and put
// those in the first line(s) before any #include <>
// otherwise you are asking for weird behavior
// (more info - check the difference in include path search when using "" versus <>)

#include "CemcMon.h"

#include <onlmon/OnlMon.h>  // for OnlMon
#include <onlmon/OnlMonDB.h>
#include <onlmon/OnlMonServer.h>
#include <onlmon/pseudoRunningMean.h>

#include <Event/Event.h>
#include <Event/EventTypes.h>
#include <Event/msg_profile.h>
#include <TH1.h>
#include <TH2.h>

#include <cmath>
#include <cstdio>  // for printf
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>  // for allocator, string, char_traits

//#include <caloreco/CaloWaveformProcessing.h>
//#include <calobase/TowerInfoContainerv1.h> 

enum
{
  TRGMESSAGE = 1,
  FILLMESSAGE = 2
};

using namespace std;


const  int emc_adc_map[8][8] = {
  62,60,46,44,30,28,14,12,
  63,61,47,45,31,29,15,13,
  58,56,42,40,26,24,10,8,
  59,57,43,41,27,25,11,9,
  54,52,38,36,22,20,6,4,
  55,53,39,37,23,21,7,5,
  50,48,34,32,18,16,2,0,
  51,49,35,33,19,17,3,1
};

// this one has the channel number for each x/y position
int sector_adcmap[48][8];

struct emcxy {
  int x;
  int y;
};

// this one gives the x and y positions of a given ADC channel
emcxy adcxy[64];



CemcMon::CemcMon(const std::string &name)
  : OnlMon(name)
{
  // leave ctor fairly empty, its hard to debug if code crashes already
  // during a new CemcMon()

  for (int ix = 0; ix < 8; ix++ )
    {
      for (int iy = 0; iy < 8; iy++ )
        {
          adcxy[ emc_adc_map[ix][iy] ].x = ix;
          adcxy[ emc_adc_map[ix][iy] ].y = iy;
        }
    }

  for (int i =0; i < 8;i++)
    {
      for (int j = 0 ; j < 8;j++)
        {
          sector_adcmap[i][j] = emc_adc_map[i][j];
          sector_adcmap[i+8][j] = emc_adc_map[i][j]+64;
          sector_adcmap[i+16][j] = emc_adc_map[i][j]+128;
          sector_adcmap[i+24][j] = emc_adc_map[i][j]+192;
          sector_adcmap[i+32][j] = emc_adc_map[i][j]+256;
          sector_adcmap[i+40][j] = emc_adc_map[i][j]+320;
        }
    }


  return;
}

const int depth = 50;
const int historyLength = 100;
const int n_channel = 48;
const float hit_threshold = 100;


CemcMon::~CemcMon()
{
  // you can delete NULL pointers it results in a NOOP (No Operation)
  delete dbvars;
  return;
}

int CemcMon::Init()
{

  // read our calibrations from CemcMonData.dat
  std::string fullfile = std::string(getenv("CEMCCALIB")) + "/" + "CemcMonData.dat";
  std::ifstream calib(fullfile);
  calib.close();
  // use printf for stuff which should go the screen but not into the message
  // system (all couts are redirected)
  printf("doing the Init\n");

  // Histograms definitions 
  h2_hcal_hits = new TH2F("h2_hcal_hits", "", 96, 0, 96, 256, 0, 256);
  h2_hcal_rm = new TH2F("h2_hcal_rm", ""    , 96, 0, 96, 256, 0, 256);
  h2_hcal_mean = new TH2F("h2_hcal_mean", "", 96, 0, 96, 256, 0, 256);
  h_event = new TH1F("h_event", "", 1, 0, 1);
  h_waveform_twrAvg = new TH1F("h_waveform_twrAvg", "", 16, 0.5, 16.5);
  h_waveform_time = new TH1F("h_waveform_time", "", 16, 0.5, 16.5);
  h_waveform_pedestal = new TH1F("h_waveform_pedestal", "", 5e3, 0, 5e3);
  h_sectorAvg_total = new TH1F("h_sectorAvg_total", "", 32, 0.5, 32.5);
  for (int ih = 0; ih < Nsector; ih++)
    h_rm_sectorAvg[ih] = new TH1F(Form("h_rm_sectorAvg_s%d", ih), "", historyLength, 0, historyLength);

  // make the per-packet running mean objects
  // 32 packets and 48 channels for hcal detectors
  for (int i = 0; i < Nsector; i++)
  {
    rm_vector_sectAvg.push_back(new pseudoRunningMean(1, depth));
  }
  for (int i = 0; i < Ntower; i++)
  {
    rm_vector_twr.push_back(new pseudoRunningMean(1, depth));
  }


  std::string h_id = "cemc_occupancy";
  cemc_occupancy = new TH2F(h_id.c_str(), "cemc_occupancy plot", 48*2 , -48 , 48, 32*8, -0.5, 255.5 );
  cemc_occupancy->GetXaxis()->SetTitle("eta");
  cemc_occupancy->GetYaxis()->SetTitle("phi");
  cemc_occupancy->SetStats(false);
  //cemc_occupancy->SetMinimum(0);
  //  cemc_occupancy->SetMaximum(1200);

  h_id = "cemc_runningmean";
  cemc_runningmean = new TH2F(h_id.c_str(), "Cemc Running Mean Run 0 Event 0", 48*2 , -48 , 48, 32*8, -0.5, 255.5 );
  cemc_runningmean->GetXaxis()->SetTitle("eta");
  cemc_runningmean->GetYaxis()->SetTitle("phi");
  cemc_runningmean->SetStats(false);
  cemc_runningmean->SetMinimum(0);
  cemc_runningmean->SetMaximum(700);

  cemc_signal = new TH1F("cemc_signal", "Signal Distribution", 512 , -200., 4000);


  //  cemchist2 = new TH2F("cemcmon_hist2", "test 2d histo", 101, 0., 100., 101, 0., 100.);
  OnlMonServer *se = OnlMonServer::instance();
  // register histograms with server otherwise client won't get them
  se->registerHisto(this, cemc_occupancy );  // uses the TH1->GetName() as key
  se->registerHisto(this, cemc_runningmean );  // uses the TH1->GetName() as key

  se->registerHisto(this, h2_hcal_hits);
  se->registerHisto(this, h2_hcal_rm);
  se->registerHisto(this, h2_hcal_mean);
  se->registerHisto(this, h_event);
  se->registerHisto(this, h_sectorAvg_total);
  se->registerHisto(this, h_waveform_twrAvg);
  se->registerHisto(this, h_waveform_time);
  se->registerHisto(this, h_waveform_pedestal);
  for (int ih = 0; ih < Nsector; ih++)
    se->registerHisto(this, h_rm_sectorAvg[ih]);

  //  se->registerHisto(this, cemchist2);
  dbvars = new OnlMonDB(ThisName);  // use monitor name for db table name
  DBVarInit();
  Reset();

  // make the per-packet runnumg mean objects
  for ( int i = 0; i < 64; i++)
    {
      rm_vector.push_back( new pseudoRunningMean(192,50));
    }


  // initialize waveform extraction tool
/*
  WaveformProcessing = new CaloWaveformProcessing();
  WaveformProcessing->set_processing_type(CaloWaveformProcessing::TEMPLATE);
  WaveformProcessing->set_template_file("testbeam_cemc_template.root");
  WaveformProcessing->initialize_processing();
  // initialize TowerInfoContainer
  CaloInfoContainer = new TowerInfoContainerv1(TowerInfoContainerv1::DETECTOR::EMCAL);
*/



  return 0;
}

int CemcMon::BeginRun(const int /* runno */)
{
  // if you need to read calibrations on a run by run basis
  // this is the place to do it

  // reset the running means
  std::vector<runningMean*>::iterator rm_it;
  for ( rm_it = rm_vector.begin(); rm_it != rm_vector.end(); ++rm_it)
    {
      (*rm_it)->Reset();
    }

  return 0;
}

//static int evtcount = 0; 


std::vector<float> CemcMon::getSignal(Packet *p, const int channel)
{

  double baseline = 0;
  for ( int s = 0;  s< 3; s++)
    {
      baseline += p->iValue(s,channel);
    }
  baseline /= 3.;

  double signal = 0;
  float x = 0;
  for ( int s = 3;  s< p->iValue(0,"SAMPLES"); s++)
    {
      x++;
      signal += p->iValue(s,channel) - baseline;
    }
	  
  signal /= x;

  // simulate a failure  if ( evtcount > 450 && p->getIdentifier() ==6011) return 0;

  std::vector<float> result = {5,5,5};
  return result;
}
	  


std::vector<float> CemcMon::anaWaveform(Packet *p, const int channel)
{
  std::vector<float> waveform;
  for ( int s = 0;  s< p->iValue(0,"SAMPLES"); s++) {
     waveform.push_back(p->iValue(s,channel));
  }
  std::vector<std::vector<float>> multiple_wfs;
  multiple_wfs.push_back(waveform);

  std::vector<std::vector<float>> fitresults_ohcal;
//  fitresults_ohcal = WaveformProcessing->process_waveform(multiple_wfs);

  std::vector<float> result;
//  result = fitresults_ohcal.at(0);

  return result;
}


int CemcMon::process_event(Event *e  /* evt */)
{
  OnlMonServer *se = OnlMonServer::instance();
  // using ONLMONBBCLL1 makes this trigger selection configurable from the outside
  // e.g. if the BBCLL1 has problems or if it changes its name
  if (!se->Trigger("ONLMONBBCLL1"))
  {
    std::ostringstream msg;
    msg << "Processing Event " << evtcnt
        << ", Trigger : 0x" << std::hex << se->Trigger()
        << std::dec;

    se->send_message(this, MSG_SOURCE_UNSPECIFIED, MSG_SEV_INFORMATIONAL, msg.str(), TRGMESSAGE);
  }
/*
  if ( e->getEvtType() == BEGRUNEVENT)  // see what kind of run this is, LED or Physics 
    {
//      Packet *p961 = e->getPacket(961);
//      if ( p961)   // this is only printing a message
//	{
//	  p961->dump();
//	  delete p961;
//	}

      Packet *p962 = e->getPacket(962);
      if ( p962)   // we extract the flag 0 = Physics, 1= LED, more can be defined
	{
	  switch (p962->iValue(0) )
	    {
	    case 0:
	      runtypestr = "Physics";
	      break;
	    case 1:
	      runtypestr = "LED";
	      break;
	    default:
	      runtypestr = "Unknown";
	      break;
	    }
	  delete p962;
	}
      return 0;
    }

  char title[512];
  sprintf ( title , "Cemc Running Mean Run %d Event %d RunType %s",  e->getRunNumber(), e->getEvtSequence(), runtypestr.c_str());
  cemc_runningmean->SetTitle(title);

  sprintf ( title , "Cemc Occupancy Run %d Event %d RunType %s",  e->getRunNumber(), e->getEvtSequence(), runtypestr.c_str());
  cemc_occupancy->SetTitle(title);

  if ( e->getEvtType() == 1) cemc_runningmean->Reset();
  evtcount = e->getEvtSequence();

  for ( int pid = 6001; pid <= 6128; pid++)
    { 
      Packet *p = e->getPacket(pid);
      if (p)
	{
	  //p->identify();
	  
	  int phi_slice = (p->getIdentifier() - 6001)/4 *8 ; 
	  int high_low_eta = (1 - p->getIdentifier()%2) ;  // 0=low eta  1 high eta
	  int north_or_south = ((p->getIdentifier() - 6001)/2) %2;   
	  
	  double signalvector[192] = {0};

	  for ( int c = 0; c <  p->iValue(0,"CHANNELS"); c++)
	    {
              // std::vector result =  getSignal(p,c); // simple peak extraction
              std::vector result = anaWaveform(p, c);  // full waveform fitting
              float signal = result.at(0);

	      signalvector[c] = signal;
	    }

	  int rm_index = p->getIdentifier() - 6001;
	  rm_vector[rm_index]->Add(signalvector);

	      // cout << __FILE__ << " " << __LINE__ << " signal is " << signal << endl;
	      
	  for ( int channel = 0; channel <  p->iValue(0,"CHANNELS"); channel++)
	    {
	      double signal = getSignal(p, channel);
	      int adc_channel = channel%64;
	      int adc_group = channel / 64;
	      float etabin = adcxy[adc_channel].x + adc_group * 8 + high_low_eta *24 +0.5;
	      int phibin = adcxy[adc_channel].y + phi_slice;

	      cemc_signal->Fill(signal);

	      if ( signal > 10. ) 
		{
		  if ( north_or_south  ) //
		    {
		      cemc_occupancy->Fill( etabin, phibin);
		      cemc_runningmean->Fill(etabin, phibin, rm_vector[rm_index]->getMean(channel));

		    }
		  else
		    {
		      cemc_occupancy->Fill( -1 * etabin, phibin);
		      cemc_runningmean->Fill(-1 * etabin, phibin, rm_vector[rm_index]->getMean(channel));
		    }
		}
	      
	      
	    }
	  delete p;

	}
    }
*/

  h_waveform_twrAvg->Reset();  // only record the latest event waveform
  unsigned int towerNumber = 0;
  float sectorAvg[Nsector] = {0};
  // loop over packets which contain a single sector
  for (int packet = packetlow; packet <= packethigh; packet++)
  {
    Packet* p = e->getPacket(packet);
    std::cout << "looking at packet " << packet << std::endl;

    if (p)
    {
      // float sectorAvg = 0;

      for (int c = 0; c < p->iValue(0, "CHANNELS"); c++)
      {
        
        //std::cout << "looking at channel " << c << std::endl;
        towerNumber++;

        // std::vector result =  getSignal(p,c); // simple peak extraction
//        std::vector result = anaWaveform(p, c);  // full waveform fitting
        std::vector<int> result = {5,5,5};
        float signal   =result.at(0);
        float time     =result.at(1);
        float pedestal =result.at(2);

        // channel mapping
//        unsigned int key = CaloInfoContainer->encode_key(towerNumber - 1);
//        unsigned int phi_bin = CaloInfoContainer->getTowerPhiBin(key);
//        unsigned int eta_bin = CaloInfoContainer->getTowerEtaBin(key);
        //std::cout << "ieta " << eta_bin << "  iphi " << phi_bin<< std::endl;
	unsigned int phi_bin = 0;
	unsigned int eta_bin = 0;
        int sectorNumber = phi_bin / 8 + 1;
        h_waveform_time->Fill(time);
        h_waveform_pedestal->Fill(pedestal);

        sectorAvg[sectorNumber - 1] += signal;

        rm_vector_twr[towerNumber - 1]->Add(&signal);

        int bin = h2_hcal_mean->FindBin(eta_bin + 0.5, phi_bin + 0.5);
        h2_hcal_mean->SetBinContent(bin, h2_hcal_mean->GetBinContent(bin) + signal);
        h2_hcal_rm->SetBinContent(bin, rm_vector_twr[towerNumber - 1]->getMean(0));
        if (signal > hit_threshold)
        {
          h2_hcal_hits->Fill(eta_bin + 0.5, phi_bin + 0.5);
        }

        // record waveform
        for (int s = 0; s < p->iValue(0, "SAMPLES"); s++)
        {
          h_waveform_twrAvg->Fill(s, p->iValue(s, c));
        }

      }  // channel loop

      delete p;
    }  // if packet good
  }    // packet loop

  // sector loop
  for (int isec = 0; isec < Nsector; isec++)
  {
    sectorAvg[isec] /= 48;
    h_sectorAvg_total->Fill(isec + 1, sectorAvg[isec]);
    rm_vector_sectAvg[isec]->Add(&sectorAvg[isec]);
    if (evtcnt <= historyLength)
    {
      h_rm_sectorAvg[isec]->SetBinContent(evtcnt, rm_vector_sectAvg[isec]->getMean(0));
    }
    else
    {
      for (int ib = 1; ib < historyLength; ib++)
      {
        h_rm_sectorAvg[isec]->SetBinContent(ib, h_rm_sectorAvg[isec]->GetBinContent(ib + 1));
      }
      h_rm_sectorAvg[isec]->SetBinContent(evtcnt, rm_vector_sectAvg[isec]->getMean(0));
    }
  }  // sector loop

  h_event->Fill(0);
  h_waveform_twrAvg->Scale(1. / 32. / 48.);  // average tower waveform

 
  if (idummy++ > 10)
  {
    if (dbvars)
    {
      dbvars->SetVar("cemcmoncount", (float) evtcnt, 0.1 * evtcnt, (float) evtcnt);
      dbvars->SetVar("cemcmondummy", sin((double) evtcnt), cos((double) se->Trigger()), (float) evtcnt);
      dbvars->SetVar("cemcmonnew", (float) se->Trigger(), 10000. / se->CurrentTicks(), (float) evtcnt);
      dbvars->DBcommit();
    }
    std::ostringstream msg;
    msg << "Filling Histos";
    se->send_message(this, MSG_SOURCE_UNSPECIFIED, MSG_SEV_INFORMATIONAL, msg.str(), FILLMESSAGE);
    idummy = 0;
  }
  return 0;
}


int CemcMon::Reset()
{
  // reset our internal counters
  evtcnt = 0;
  idummy = 0;
  return 0;
}

int CemcMon::DBVarInit()
{
  // variable names are not case sensitive
  std::string varname;
  varname = "cemcmoncount";
  dbvars->registerVar(varname);
  varname = "cemcmondummy";
  dbvars->registerVar(varname);
  varname = "cemcmonnew";
  dbvars->registerVar(varname);
  if (verbosity > 0)
  {
    dbvars->Print();
  }
  dbvars->DBInit();
  return 0;
}
