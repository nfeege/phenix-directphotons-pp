#include "DirectPhotonPP.h"

/* always put includes with "" before those with <> */
#include "AnaToolsTowerID.h"
#include "AnaToolsCluster.h"
#include "AnaToolsTrigger.h"

#include "HistogramBooker.h"
#include "EmcLocalRecalibrator.h"
#include "EmcLocalRecalibratorSasha.h"
#include "EMCWarnmapChecker.h"

/* Other Fun4All header */
#include <getClass.h>
#include <Fun4AllHistoManager.h>
#include <Fun4AllReturnCodes.h>
#include <PHCentralTrack.h>
#include <RunHeader.h>
#include <emcClusterContent.h>
#include <emcClusterContainer.h>
#include <PHGlobal.h>
#include <TOAD.h>
#include <SpinDBOutput.hh>
#include <SpinDBContent.hh>

/* ROOT header */
#include <TH1.h>
#include <TH2.h>
#include <THnSparse.h>
#include <TFile.h>
#include <TLorentzVector.h>

/* STL / boost header */
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cmath>

using namespace std;

DirectPhotonPP::DirectPhotonPP(const char* outfile) :
  _dsttype( "MinBias" ),
  _ievent( 0 ),
  _runnumber( 0 ),
  _bbc_zvertex_cut( 10 ),
  _photon_energy_min( 0.3 ), // 0.3 used by Paul for run 9 pp 500 GeV
  _photon_prob_min( 0.02 ),
  _photon_tof_min( -10 ),
  _photon_tof_max( 10 ),
  _direct_photon_energy_min( 1.0 ),
  _emcrecalib( nullptr ),
  _emcrecalib_sasha( nullptr ),
  _emcwarnmap( nullptr ),
  _debug_cluster( false ),
  _debug_trigger( false ),
  _debug_pi0( false )
{
  /* construct output file names */
  _outfile_histos = "DirectPhotonPP-";
  _outfile_histos.append( outfile );

  /* initialize histogram manager */
  _hm = HistogramBooker::GetHistoManager( "DirectPhotonPPHistoManager" );
  _hm->setOutfileName( _outfile_histos );
  _hm->Print("ALL");

  /* initialize warnmap */
  for(int sec=0; sec<8; sec++)
    for(int iy=0; iy<48; iy++)
      for(int iz=0; iz<96; iz++)
        _tower_status[sec][iy][iz] = 0;
}

/* ----------------------------------------------- */

DirectPhotonPP::~DirectPhotonPP()
{
}

/* ----------------------------------------------- */

  int
DirectPhotonPP::Init(PHCompositeNode *topNode)
{
  /* Check that ONE local recalibrator is defined */
  if ( ! ( _emcrecalib || _emcrecalib_sasha ) )
  {
    cout << "!!! DirectPhotonPP: No EmcLocalRecalibrator set. Throw exception." << endl;
    throw (DONOTREGISTERSUBSYSTEM);
  }

  /* Exit if MORE than one local recalibrator is defined
   * (There Can Be Only One) */
  if( _emcrecalib && _emcrecalib_sasha )
  {
    cerr << "!!! DirectPhotonPP: More than ONE EmcLocalRecalibrator set. Throw exception." << endl;
    throw (DONOTREGISTERSUBSYSTEM);
  }

  /* Initialize EMC warnmap checker */
  _emcwarnmap = new EMCWarnmapChecker();
  if(!_emcwarnmap)
  {
    cerr << "No emcwarnmap" << endl;
    throw (DONOTREGISTERSUBSYSTEM);
  }

  for(int sec=0; sec<8; sec++)
    for(int iy=0; iy<48; iy++)
      for(int iz=0; iz<96; iz++)
        _tower_status[sec][iy][iz] = _emcwarnmap->GetStatusNils(sec, iy, iz);

  return EVENT_OK;
}

/* ----------------------------------------------- */

  int
DirectPhotonPP::InitRun(PHCompositeNode *topNode)
{
  /* Get run number */
  RunHeader* runheader = findNode::getClass<RunHeader>(topNode, "RunHeader");
  if(runheader == nullptr)
  {
    cout << "No RunHeader." << endl;
    return ABORTRUN;
  }
  _runnumber = runheader->get_RunNumber();

  /* Get fill number */
  SpinDBContent spin_cont;
  SpinDBOutput spin_out;

  /*Initialize opbject to access spin DB*/
  spin_out.Initialize();
  spin_out.SetUserName("phnxrc");
  spin_out.SetTableName("spin");

  /* Retrieve entry from Spin DB and get fill number */
  int qa_level=spin_out.GetDefaultQA(_runnumber);
  spin_out.StoreDBContent(_runnumber, _runnumber, qa_level);
  spin_out.GetDBContentStore(spin_cont, _runnumber);
  int fillnumber = spin_cont.GetFillNumber();

  /* Load EMCal recalibrations for run and fill */
  if ( _emcrecalib )
  {
    _emcrecalib->ReadEnergyCorrection( _runnumber );
    _emcrecalib->ReadTofCorrection( fillnumber );
  }

  return EVENT_OK;
}

/* ----------------------------------------------- */

  int
DirectPhotonPP::process_event(PHCompositeNode *topNode)
{
  /* count up event counter */
  _ievent++;

  /*
   *  Get pointer to data nodes
   */
  emcClusterContainer* data_emc = findNode::getClass<emcClusterContainer> (topNode, "emcClusterContainer");
  PHCentralTrack* data_tracks = findNode::getClass<PHCentralTrack> (topNode, "PHCentralTrack");
  PHGlobal* data_global = findNode::getClass<PHGlobal> (topNode, "PHGlobal");
  ErtOut* data_ert = findNode::getClass<ErtOut> (topNode, "ErtOut");
  TrigLvl1* data_triggerlvl1 = findNode::getClass<TrigLvl1> (topNode, "TrigLvl1");

  /*
   * Check availability of data nodes
   */
  if(!data_global)
  {
    cout<<"\nABORT RUN\nNo gbl"<<endl;
    return DISCARDEVENT;
  }

  if(!data_emc)
  {
    cout<<"\nABORT RUN\nNo emcont"<<endl;
    return DISCARDEVENT;
  }

  if(!data_tracks)
  {
    cout<<"\nABORT RUN\nNo tracker data"<<endl;
    return DISCARDEVENT;
  }

  if(!data_triggerlvl1)
  {
    cout<<"\nABORT RUN\nNo trg"<<endl;
    return DISCARDEVENT;
  }

  if(!data_ert)
  {
    cout<<"\nABORT RUN\nNo ert"<<endl;
    return DISCARDEVENT;
  }

  /*
   * *** EVALUATE: Trigger information ***
   */

  /* Get global event parameters */
  double bbc_z  = data_global->getBbcZVertex();
  double bbc_t0  = data_global->getBbcTimeZero();

  /* Get Lvl1 trigger bits */
  unsigned lvl1_live = data_triggerlvl1->get_lvl1_triglive();
  unsigned lvl1_scaled = data_triggerlvl1->get_lvl1_trigscaled();

  /* skip noise trigger + pulsed */
  const unsigned bit_ppg = 0x70000000;
  if( (lvl1_live & bit_ppg) ||
      (lvl1_scaled & bit_ppg) )
  {
    if ( _debug_trigger )
      cout << " *** Event " << _ievent << ": Check bit_ppg - FAILED" << endl;

    return DISCARDEVENT;
  }
  else
  {
    if ( _debug_trigger )
      cout << " *** Event " << _ievent << ": Check bit_ppg - PASSED" << endl;
  }

  /* Count events */
  FillTriggerStats( "h1_events" , data_triggerlvl1 , data_ert , bbc_z );

  /* Count events to calculate trigger efficiency */
  //  FillTriggerEfficiency( data_emc, data_global, data_ert );


  /*
   * *** EVALUATE: Cluster information ***
   */
  /* Run local recalibration of EMCal cluster data */
  emcClusterContainer* data_emc_corr = data_emc->clone();

  if ( _emcrecalib )
  {
    _emcrecalib->ApplyClusterCorrection( data_emc_corr );
  }
  else if ( _emcrecalib_sasha )
  {
    _emcrecalib_sasha->ApplyClusterCorrection( _runnumber, data_emc_corr );
  }

  /* Apply cuts to calorimeter cluster collections and create subsets for next analysis steps */
  emcClusterContainer* data_emc_cwarn = data_emc->clone();
  selectClusterGoodTower( data_emc_cwarn );

  emcClusterContainer* data_emc_cwarn_cshape_cenergy = data_emc_cwarn->clone();
  selectClusterPhotonShape( data_emc_cwarn_cshape_cenergy );
  selectClusterPhotonEnergy( data_emc_cwarn_cshape_cenergy );

  emcClusterContainer* data_emc_corr_cwarn = data_emc_corr->clone();
  selectClusterGoodTower( data_emc_corr_cwarn );

  emcClusterContainer* data_emc_corr_cwarn_cshape_cenergy = data_emc_corr_cwarn->clone();
  selectClusterPhotonShape( data_emc_corr_cwarn_cshape_cenergy );
  selectClusterPhotonEnergy( data_emc_corr_cwarn_cshape_cenergy );

  emcClusterContainer* data_emc_corr_cwarn_cshape_cenergy_ctof = data_emc_corr_cwarn_cshape_cenergy->clone();
  selectClusterPhotonTof( data_emc_corr_cwarn_cshape_cenergy_ctof, bbc_t0 );

  /* Print detailes cluster collection infomration for debugging purpose */
  if ( _debug_cluster )
  {
    cout << "***** Event " << _ievent << ": data_emc" << endl;
    PrintClusterContainer( data_emc, bbc_t0 );
    cout << endl;
    cout << endl;
    cout << "***** Event " << _ievent << ": data_emc_corr" << endl;
    PrintClusterContainer( data_emc_corr, bbc_t0 );
    cout << endl;
    cout << endl;
    cout << "***** Event " << _ievent << ": data_emc_cwarn" << endl;
    PrintClusterContainer( data_emc_cwarn, bbc_t0 );
    cout << endl;
    cout << endl;
    cout << "***** Event " << _ievent << ": data_emc_cwarn_cshape_cenergy" << endl;
    PrintClusterContainer( data_emc_cwarn_cshape_cenergy, bbc_t0 );
    cout << endl;
    cout << endl;
    cout << "***** Event " << _ievent << ": data_emc_corr_cwarn_cshape_cenergy" << endl;
    PrintClusterContainer( data_emc_corr_cwarn_cshape_cenergy, bbc_t0 );
    cout << endl;
    cout << endl;
    cout << "***** Event " << _ievent << ": data_emc_corr_cwarn_cshape_cenergy_ctof" << endl;
    PrintClusterContainer( data_emc_corr_cwarn_cshape_cenergy_ctof, bbc_t0 );
    cout << endl;
    cout << endl;
  }

  /*
   * *** EVALUATE: Calibration and warnmap crosscheck ***
   */
  /* Look at all cluster as crosscheck of warnmap */
  FillClusterPtSpectrum( "h2_pT_1cluster" , data_emc_cwarn );
  FillClusterPtSpectrum( "h2_pT_1cluster_nowarn" , data_emc );

  /* Store TOF information for all cluster as calibration check */
  FillClusterTofSpectrum( "hn_tof" , data_emc_corr_cwarn_cshape_cenergy , data_global , bbc_t0 );
  FillClusterTofSpectrum( "hn_tof_raw" , data_emc_cwarn_cshape_cenergy , data_global , bbc_t0 );


  /* Check event information: analyze this event or not?
  */

  /* Check event information for MinBias data */
  if ( _dsttype == "MinBias" )
  {
    /* Check trigger */
    if (
        fabs ( bbc_z ) <= _bbc_zvertex_cut &&         /* if BBC-z location within range */
        lvl1_scaled & anatools::Mask_BBC_narrowvtx  /* if trigger criteria met */
       )
    {
      if ( _debug_trigger )
        cout << " *** Event " << _ievent << ": Check BBC_narrowvertex scaled and BBC_Z <= cut - PASSED" << endl;

      /* Analyze cluster pairs to find pi0's in events for calibration crosscheck */
      if ( _debug_pi0 )
        cout << " *** Event " << _ievent << ": FillPi0InvariantMass (hn_pi0)" << endl;
      FillPi0InvariantMass( "hn_pi0", data_emc_corr_cwarn_cshape_cenergy_ctof, data_triggerlvl1, data_ert );

      if ( _debug_pi0 )
        cout << " *** Event " << _ievent << ": FillPi0InvariantMass (hn_pi0_notof)" << endl;
      FillPi0InvariantMass( "hn_pi0_notof", data_emc_corr_cwarn_cshape_cenergy, data_triggerlvl1, data_ert );

      if ( _debug_pi0 )
        cout << " *** Event " << _ievent << ": FillPi0InvariantMass (hn_pi0_raw)" << endl;
      FillPi0InvariantMass( "hn_pi0_raw", data_emc_cwarn_cshape_cenergy, data_triggerlvl1, data_ert );

      /* Analyze photon spectrum */
      FillPhotonPtSpectrum( "hn_1photon",
          "hn_2photon",
          data_emc_corr_cwarn_cshape_cenergy_ctof,
          data_emc_corr_cwarn,
          data_tracks,
          data_triggerlvl1,
          data_ert);
    }
    else
    {
      if ( _debug_trigger )
        cout << " *** Event " << _ievent << ": Check BBC_narrowvertex scaled and BBC_Z <= cut - FAILED" << endl;
    }
  }
  /* check event information for ERT data */
  else if ( _dsttype == "ERT" )
  {
    if ( _debug_trigger )
      cout << " *** Event " << _ievent << ": BBC_z, ERT-a, ERT-b, ERT-c: " <<
        (bbc_z) << ", " <<
        (lvl1_scaled & anatools::Mask_ERT_4x4a) << ", " <<
        (lvl1_scaled & anatools::Mask_ERT_4x4b) << ", " <<
        (lvl1_scaled & anatools::Mask_ERT_4x4c) << endl;

    /* Check trigger */
    if (
        fabs ( bbc_z ) <= _bbc_zvertex_cut &&         /* if BBC-z location within range */
        lvl1_live & anatools::Mask_BBC_narrowvtx && /* if trigger criteria met */
        ( lvl1_scaled & anatools::Mask_ERT_4x4a ||
          lvl1_scaled & anatools::Mask_ERT_4x4b ||
          lvl1_scaled & anatools::Mask_ERT_4x4c )   /* if trigger criteria met */
       )
    {
      if ( _debug_trigger )
        cout << " *** Event " << _ievent << ": Check BBC_narrowvertex live and ERT scaled and BBC_Z <= cut - PASSED" << endl;

      /* Analyze cluster pairs to find pi0's in events for calibration crosscheck */
      if ( _debug_pi0 )
        cout << " *** Event " << _ievent << ": FillPi0InvariantMass (hn_pi0)" << endl;
      FillPi0InvariantMass( "hn_pi0", data_emc_corr_cwarn_cshape_cenergy_ctof, data_triggerlvl1, data_ert );

      if ( _debug_pi0 )
        cout << " *** Event " << _ievent << ": FillPi0InvariantMass (hn_pi0_notof)" << endl;
      FillPi0InvariantMass( "hn_pi0_notof", data_emc_corr_cwarn_cshape_cenergy, data_triggerlvl1, data_ert );

      if ( _debug_pi0 )
        cout << " *** Event " << _ievent << ": FillPi0InvariantMass (hn_pi0_raw)" << endl;
      FillPi0InvariantMass( "hn_pi0_raw", data_emc_cwarn_cshape_cenergy, data_triggerlvl1, data_ert );

      /* Analyze photon spectrum */
      FillPhotonPtSpectrum( "hn_1photon",
          "hn_2photon",
          data_emc_corr_cwarn_cshape_cenergy_ctof,
          data_emc_corr_cwarn,
          data_tracks,
          data_triggerlvl1,
          data_ert);
    }
    else
    {
      if ( _debug_trigger )
        cout << " *** Event " << _ievent << ": Check BBC_narrowvertex live and ERT scaled and BBC_Z <= cut - FAILED" << endl;
    }
  }
  /* abort event if no matching data type */
  else
  {
    cout<<"\nDISCARD EVENT \nDST type unknown: " << _dsttype <<endl;
    return DISCARDEVENT;
  }

  /* clean up */
  delete data_emc_cwarn_cshape_cenergy;
  delete data_emc_cwarn;
  delete data_emc_corr;
  delete data_emc_corr_cwarn;
  delete data_emc_corr_cwarn_cshape_cenergy;
  delete data_emc_corr_cwarn_cshape_cenergy_ctof;

  return EVENT_OK;
}

/* ----------------------------------------------- */
  int
DirectPhotonPP::FillTriggerStats( string histname,
    TrigLvl1* data_triggerlvl1,
    ErtOut *data_ert ,
    double bbc_z )
{

  /* retrieve histograms used in this function */
  TH1* h1_events = static_cast<TH1*>( _hm->getHisto(histname) );

  /* Get trigger information */
  //  unsigned int lvl1_raw = data_triggerlvl1->get_lvl1_trigraw();
  unsigned int lvl1_live = data_triggerlvl1->get_lvl1_triglive();
  unsigned int lvl1_scaled = data_triggerlvl1->get_lvl1_trigscaled();

  /* Fill event counter */
  h1_events->Fill("all",1);

  /* Count event trigger stats */
  if ( lvl1_scaled & anatools::Mask_BBC_narrowvtx )
    h1_events->Fill("BBCNarrow",1);

  if ( lvl1_scaled & anatools::Mask_BBC_narrowvtx &&
      fabs ( bbc_z ) <= _bbc_zvertex_cut )
    h1_events->Fill("BBCNarrow_zcut",1);

  if ( lvl1_live & anatools::Mask_BBC_narrowvtx &&
      lvl1_scaled & anatools::Mask_ERT_4x4a &&
      fabs ( bbc_z ) <= _bbc_zvertex_cut )
    h1_events->Fill("BBCNarrow_zcut_ERT4x4a",1);

  if ( lvl1_live & anatools::Mask_BBC_narrowvtx &&
      lvl1_scaled & anatools::Mask_ERT_4x4b &&
      fabs ( bbc_z ) <= _bbc_zvertex_cut )
    h1_events->Fill("BBCNarrow_zcut_ERT4x4b",1);

  if ( lvl1_live & anatools::Mask_BBC_narrowvtx &&
      lvl1_scaled & anatools::Mask_ERT_4x4c &&
      fabs ( bbc_z ) <= _bbc_zvertex_cut )
    h1_events->Fill("BBCNarrow_zcut_ERT4x4c",1);

  if ( lvl1_live & anatools::Mask_BBC_narrowvtx &&
      lvl1_scaled & anatools::Mask_ERT_4x4or &&
      fabs ( bbc_z ) <= _bbc_zvertex_cut )
    h1_events->Fill("BBCNarrow_zcut_ERT4x4or",1);

  if ( lvl1_scaled & anatools::Mask_ERT_4x4or )
    h1_events->Fill("ERT4x4or",1);

  return 0;
}

/* ----------------------------------------------- */
  int
DirectPhotonPP::FillTriggerEfficiency( emcClusterContainer *data_emc,
    PHGlobal *data_global,
    ErtOut *data_ert )
{
  //  /* retrieve all histograms used in this function */
  //  THnSparse *hn_trig = static_cast<THnSparse*>( _hm->getHisto("hn_trig") );
  //  THnSparse *hn_trig_pion = static_cast<THnSparse*>( _hm->getHisto("hn_trig_pion") );
  //
  //  /* Get event global parameters */
  //  double bbc_z = data_global->getBbcZVertex();
  //  double bbc_t0 = data_global->getBbcTimeZero();
  //  if( fabs(bbc_z) > 30. ) return 1;
  //
  //  int nemccluster = data_emc->size();
  //
  //  /* Fire ERT on arm 0 (west) or 1 (east)*/
  //  bool FireERT[2] = {};
  //
  //  for(int i=0; i<nemccluster; i++)
  //    {
  //      emcClusterContent *emccluster = data_emc->getCluster(i);
  //      int arm = emccluster->arm();
  //      if( anatools::PassERT(data_ert, emccluster, anatools::ERT_4x4a) ||
  //          anatools::PassERT(data_ert, emccluster, anatools::ERT_4x4b) ||
  //          anatools::PassERT(data_ert, emccluster, anatools::ERT_4x4c) )
  //        FireERT[arm] = true;
  //    }
  //
  //  vector<int> v_used;
  //  v_used.clear();
  //
  //  for(int i=0; i<nemccluster; i++)
  //    {
  //      emcClusterContent *emccluster1 = data_emc->getCluster(i);
  //      int sector = anatools::CorrectClusterSector( emccluster1->arm(), emccluster1->sector() );
  //      double cluster_pT = anatools::Get_pT( emccluster1 );
  //
  //      /* Require the other arm to be fired */
  //      int oarm = emccluster1->arm()==0 ? 1 : 0;
  //      if( !FireERT[oarm] ) continue;
  //
  //      if( testGoodTower(emccluster1) &&
  //          testPhoton(emccluster1, bbc_t0) &&
  //          emccluster1->ecore() > _photon_energy_min )
  //        {
  //          v_used.push_back(i);
  //
  //          h3_trig->Fill(cluster_pT, sector, "all", 1.);
  //
  //          if( anatools::PassERT(data_ert, emccluster1, anatools::ERT_4x4a) )
  //            h3_trig->Fill(cluster_pT, sector, "ERT4x4a", 1.);
  //
  //          if( anatools::PassERT(data_ert, emccluster1, anatools::ERT_4x4b) )
  //            h3_trig->Fill(cluster_pT, sector, "ERT4x4b", 1.);
  //
  //          if( anatools::PassERT(data_ert, emccluster1, anatools::ERT_4x4c) )
  //            h3_trig->Fill(cluster_pT, sector, "ERT4x4c", 1.);
  //
  //          for(int j=0; j<nemccluster; j++)
  //            if( j != i && find(v_used.begin(), v_used.end(), j) == v_used.end() )
  //              {
  //                emcClusterContent *emccluster2 = data_emc->getCluster(j);
  //                double tot_pT = anatools::GetTot_pT(emccluster1, emccluster2);
  //
  //                if( testGoodTower(emccluster2) &&
  //                    testPhoton(emccluster2, bbc_t0) &&
  //                    emccluster2->ecore() > _photon_energy_min )
  //                  {
  //                    double minv = anatools::GetInvMass(emccluster1, emccluster2);
  //                    if( minv < 0.112 || minv > 0.162 )
  //                      continue;
  //
  //                    h3_trig_pion->Fill(tot_pT, sector, "all", 1.);
  //
  //                    if( anatools::PassERT(data_ert, emccluster1, anatools::ERT_4x4a) ||
  //                        anatools::PassERT(data_ert, emccluster2, anatools::ERT_4x4a) )
  //                      h3_trig_pion->Fill(tot_pT, sector, "ERT4x4a", 1.);
  //
  //                    if( anatools::PassERT(data_ert, emccluster1, anatools::ERT_4x4b) ||
  //                        anatools::PassERT(data_ert, emccluster2, anatools::ERT_4x4b) )
  //                      h3_trig_pion->Fill(tot_pT, sector, "ERT4x4b", 1.);
  //
  //                    if( anatools::PassERT(data_ert, emccluster1, anatools::ERT_4x4c) ||
  //                        anatools::PassERT(data_ert, emccluster2, anatools::ERT_4x4c) )
  //                      h3_trig_pion->Fill(tot_pT, sector, "ERT4x4c", 1.);
  //                  }
  //              }
  //        }
  //    }

  return 0;
}

/* ----------------------------------------------- */

  int
DirectPhotonPP::FillClusterPtSpectrum( string histname,
    emcClusterContainer *data_emc )
{

  /* retrieve all histograms used in this function */
  TH2* h2_pT_1cluster        = static_cast<TH2*>( _hm->getHisto(histname) );

  /* Analyze all cluster in this event and fill pT spectrum */
  int nemccluster;
  nemccluster = data_emc->size();

  for( int i = 0; i < nemccluster; i++ )
  {
    emcClusterContent *emccluster = data_emc->getCluster(i);

    int sector = anatools::CorrectClusterSector( emccluster->arm() , emccluster->sector() );

    double cluster_pT = anatools::Get_pT( emccluster );

    h2_pT_1cluster->Fill( cluster_pT, sector );
  }

  return 0;
}

/* ----------------------------------------------- */

  int
DirectPhotonPP::FillClusterTofSpectrum( string histname,
    emcClusterContainer *data_emc,
    PHGlobal *data_global,
    double bbc_t0 )
{

  /* retrieve all histograms used in this function */
  THnSparse* hn_tof     = static_cast<THnSparse*>( _hm->getHisto(histname) );

  /* Analyze all cluster in this event and fill TOF spectrum */
  int nemccluster;
  nemccluster = data_emc->size();

  for( int i = 0; i < nemccluster; i++ )
  {
    emcClusterContent *emccluster = data_emc->getCluster(i);

    int sector = anatools::CorrectClusterSector( emccluster->arm() , emccluster->sector() );

    double tof = emccluster->tofcorr() - bbc_t0;

    double pT = anatools::Get_pT(emccluster);

    double fill_hn_tof[] = {(double)sector, pT, tof};
    hn_tof->Fill( fill_hn_tof );
  }

  return 0;
}

/* ----------------------------------------------- */

  int
DirectPhotonPP::FillPi0InvariantMass( string histname,
    emcClusterContainer *data_emc,
    TrigLvl1* data_triggerlvl1,
    ErtOut *data_ert )
{
  /* retrieve all histograms used in this function */
  THnSparse* hn_pion = static_cast<THnSparse*>( _hm->getHisto(histname) );

  /* Get trigger information */
  unsigned int lvl1_scaled = data_triggerlvl1->get_lvl1_trigscaled();

  /* Trigger selection */
  enum bin_selection { FILL_ERT_null = 0,
    FILL_ERT_4x4a = 1,
    FILL_ERT_4x4b = 2,
    FILL_ERT_4x4c = 3 };

  /* NEW method: Make all possible cluster combinations, avoid duplicate combinations */
  vector< unsigned int > v_used;

  /* loop over all EMCal cluster */
  unsigned int nemccluster = data_emc->size();
  for( unsigned int cidx1 = 0; cidx1 < nemccluster; cidx1++ )
  {
    emcClusterContent *emccluster1 = data_emc->getCluster( cidx1 );

    if ( testTightFiducial( emccluster1 ) )
    {
      v_used.push_back( cidx1 );

      /* loop over partner photon candidates */
      for( unsigned int cidx2 = 0; cidx2 < nemccluster; cidx2++ )
      {
        /* skip if trying to combine cluster with itself */
        if ( cidx1 == cidx2 )
          continue;

        /* skip if this cluster has already been used as primary cluster, i.e. emccluster1 */
        if ( find( v_used.begin(), v_used.end(), cidx2 ) != v_used.end() )
          continue;

        emcClusterContent* emccluster2 = data_emc->getCluster( cidx2 );

        if ( _debug_pi0 )
        {
          cout << " *** Event " << _ievent << ": FillPi0InvariantMass: Cluster 1 energy    " << emccluster1->ecore() << endl;
          cout << " *** Event " << _ievent << ": FillPi0InvariantMass: Cluster 2 energy    " << emccluster2->ecore() << endl;
        }


        /* check energy asymmetry */
        if ( anatools::GetAsymmetry_E( emccluster1, emccluster2 ) >= 0.8 )
        {
          if ( _debug_pi0 )
            cout << " *** Event " << _ievent << ": FillPi0InvariantMass: Check GetStatus && AsymCut for photon pair - FAILED" << endl;

          continue;
        }
        else
        {
          if ( _debug_pi0 )
            cout << " *** Event " << _ievent << ": FillPi0InvariantMass: Check GetStatus && AsymCut for photon pair - PASSED" << endl;
        }

        /* get sectors */
        int sector1 = anatools::CorrectClusterSector( emccluster1->arm() , emccluster1->sector() );
        int sector2 = anatools::CorrectClusterSector( emccluster2->arm() , emccluster2->sector() );

        /* check if clusters in same section of detector */
        if( !anatools::SectorCheck(sector1,sector2) )
        {
          if ( _debug_pi0 )
            cout << " *** Event " << _ievent << ": FillPi0InvariantMass: Check Sectors for photon pair - FAILED" << endl;

          continue;
        }
        else
        {
          if ( _debug_pi0 )
            cout << " *** Event " << _ievent << ": FillPi0InvariantMass: Check Sectors for photon pair - PASSED" << endl;
        }

        /* pE = {px, py, pz, ecore} */
        TLorentzVector photon1_pE = anatools::Get_pE(emccluster1);
        TLorentzVector photon2_pE = anatools::Get_pE(emccluster2);
        TLorentzVector tot_pE =  photon1_pE + photon2_pE;
        double tot_pT = tot_pE.Pt();
        double tot_mom = tot_pE.P();
        double tot_px = tot_pE.Px();
        double tot_py = tot_pE.Py();
        double tot_pz = tot_pE.Pz();

        /* Fill invariant mass for pi0 candidate in histogram */
        double invMass = anatools::GetInvMass( emccluster1, emccluster2 );

        /* Use the photon which has higher energy to label the sector and trigger */
        int sector = sector1;
        int trig_ert_a = anatools::PassERT(data_ert, emccluster1, anatools::ERT_4x4a);
        int trig_ert_b = anatools::PassERT(data_ert, emccluster1, anatools::ERT_4x4b);
        int trig_ert_c = anatools::PassERT(data_ert, emccluster1, anatools::ERT_4x4c);
        if( photon2_pE.E() > photon1_pE.E() )
        {
          sector = sector2;
          trig_ert_a = anatools::PassERT(data_ert, emccluster2, anatools::ERT_4x4a);
          trig_ert_b = anatools::PassERT(data_ert, emccluster2, anatools::ERT_4x4b);
          trig_ert_c = anatools::PassERT(data_ert, emccluster2, anatools::ERT_4x4c);
        }

        if ( _debug_pi0 )
        {
          cout << " *** Event " << _ievent << ": FillPi0InvariantMass: trig_ert_a, trig_ert_b, trig_ert_c: " <<
            trig_ert_a << " , " << trig_ert_b << " , " << trig_ert_c << endl;
        }

        /* Fill eta and phi */
        double tot_eta = tot_mom > 0. ? atan(tot_pz/tot_mom) : 9999.;
        double tot_phi = tot_px > 0. ? atan(tot_py/tot_px) : 3.1416+atan(tot_py/tot_px);

        /* Fill histogram */
        if ( _debug_pi0 )
        {
          cout << " *** Event " << _ievent << ": FillPi0InvariantMass: Fill (not checking ERT) photon pair tot_pT " << tot_pT << endl;
        }

        double fill_hn_pion[] = {(double)sector, tot_pT, invMass, tot_eta, tot_phi, (double)FILL_ERT_null};
        hn_pion->Fill(fill_hn_pion);

        /* Require the target cluster fires the trigger */
        if( ( lvl1_scaled & anatools::Mask_ERT_4x4a ) && trig_ert_a )
        {
          double fill_hn_pion[] = {(double)sector, tot_pT, invMass, tot_eta, tot_phi, (double)FILL_ERT_4x4a};
          hn_pion->Fill(fill_hn_pion);

          if ( _debug_pi0 )
          {
            cout << " *** Event " << _ievent << ": FillPi0InvariantMass: Fill (ERT-a ) photon pair tot_pT " << tot_pT << endl;
          }
        }
        if( ( lvl1_scaled & anatools::Mask_ERT_4x4b ) && trig_ert_b )
        {
          double fill_hn_pion[] = {(double)sector, tot_pT, invMass, tot_eta, tot_phi, (double)FILL_ERT_4x4b};
          hn_pion->Fill(fill_hn_pion);

          if ( _debug_pi0 )
          {
            cout << " *** Event " << _ievent << ": FillPi0InvariantMass: Fill (ERT-b ) photon pair tot_pT " << tot_pT << endl;
          }
        }
        if( ( lvl1_scaled & anatools::Mask_ERT_4x4c ) && trig_ert_c )
        {
          double fill_hn_pion[] = {(double)sector, tot_pT, invMass, tot_eta, tot_phi, (double)FILL_ERT_4x4c};
          hn_pion->Fill(fill_hn_pion);

          if ( _debug_pi0 )
          {
            cout << " *** Event " << _ievent << ": FillPi0InvariantMass: Fill (ERT-c ) photon pair tot_pT " << tot_pT << endl;
          }
        }
      } // loop cluster 2
    } // check if in tight fiducial volume
  } // loop cluster 1

  return 0;
}

/* ----------------------------------------------- */

  int
DirectPhotonPP::FillPhotonPtSpectrum( string histname_1photon,
    string histname_2photon,
    emcClusterContainer *data_photons,
    emcClusterContainer *data_cluster,
    PHCentralTrack *data_tracks,
    TrigLvl1* data_triggerlvl1,
    ErtOut *data_ert )
{
  THnSparse* hn_1photon          = static_cast<THnSparse*>( _hm->getHisto( histname_1photon ) );
  THnSparse* hn_2photon          = static_cast<THnSparse*>( _hm->getHisto( histname_2photon ) );

  /* Analyze all photon candidates in this event and fill pT spectrum */
  unsigned int max_photons = data_photons->size();

  //  /* Enum identifying level of photon selection / cut */
  //  enum bin_selection { CUT_ISOPHOTON = 0,
  //                       CUT_DIRECTPHOTON = 1,
  //                       CUT_ENERGY_SHAPE_TRACK_TOF = 2,
  //                       CUT_ENERGY_SHAPE_TRACK = 3,
  //                       CUT_ENERGY_SHAPE = 4,
  //                       CUT_ENERGY = 5 };

  /* Get trigger information */
  unsigned int lvl1_scaled = data_triggerlvl1->get_lvl1_trigscaled();

  /* Trigger selection */
  enum bin_selection { FILL_ERT_null = 0,
    FILL_ERT_4x4a = 1,
    FILL_ERT_4x4b = 2,
    FILL_ERT_4x4c = 3 };


  /* loop over all EMCal cluster */
  for( unsigned int cidx1 = 0; cidx1 < max_photons; cidx1++ )
  {
    emcClusterContent *photon1 = data_photons->getCluster( cidx1 );

    if ( testTightFiducial( photon1 ) && testDirectPhotonEnergy( photon1 ) )
    {
      int sector1 = anatools::CorrectClusterSector( photon1->arm() , photon1->sector() );

      /* pE = {px, py, pz, ecore} */
      TLorentzVector photon1_pE = anatools::Get_pE( photon1 );
      double photon1_px = photon1_pE.Px();
      double photon1_py = photon1_pE.Py();
      double photon1_pz = photon1_pE.Pz();
      double photon1_pT = photon1_pE.Pt();
      double photon1_ptotal = photon1_pE.P();
      double photon1_E = photon1_pE.E();

      /* Fill eta and phi */
      double photon1_eta = photon1_ptotal > 0. ? atan(photon1_pz/photon1_ptotal) : 9999.;
      double photon1_phi = photon1_px > 0. ? atan(photon1_py/photon1_px) : 3.1416+atan(photon1_py/photon1_px);

      /* Is isolated photon? */
      double photon1_isolated = 0;

      if ( testIsolatedCluster( photon1 , data_cluster, data_tracks, 0.4, 0.1 ) )
        photon1_isolated = 1;

      /* Check which ERT trigger the photon fired */
      int trig_ert_a = anatools::PassERT(data_ert, photon1, anatools::ERT_4x4a);
      int trig_ert_b = anatools::PassERT(data_ert, photon1, anatools::ERT_4x4b);
      int trig_ert_c = anatools::PassERT(data_ert, photon1, anatools::ERT_4x4c);

      /* fill direct photons pT histogram */
      double fill_histo_1photon[] = { (double)sector1,
        photon1_pT,
        photon1_E,
        photon1_eta,
        photon1_phi,
        (double)FILL_ERT_null,
        photon1_isolated };

      hn_1photon->Fill( fill_histo_1photon );

      /* Require the target cluster fires the trigger */
      if( ( lvl1_scaled & anatools::Mask_ERT_4x4a ) && trig_ert_a )
      {
        double fill_histo_1photon[] = { (double)sector1,
          photon1_pT,
          photon1_E,
          photon1_eta,
          photon1_phi,
          (double)FILL_ERT_4x4a,
          photon1_isolated };

        hn_1photon->Fill( fill_histo_1photon );
      }
      if( ( lvl1_scaled & anatools::Mask_ERT_4x4b ) && trig_ert_b )
      {
        double fill_histo_1photon[] = { (double)sector1,
          photon1_pT,
          photon1_E,
          photon1_eta,
          photon1_phi,
          (double)FILL_ERT_4x4b,
          photon1_isolated };

        hn_1photon->Fill( fill_histo_1photon );
      }
      if( ( lvl1_scaled & anatools::Mask_ERT_4x4c ) && trig_ert_c )
      {
        double fill_histo_1photon[] = { (double)sector1,
          photon1_pT,
          photon1_E,
          photon1_eta,
          photon1_phi,
          (double)FILL_ERT_4x4c,
          photon1_isolated };

        hn_1photon->Fill( fill_histo_1photon );
      }


      /* Record information for 2-photon pairs: */

      /* loop over partner photon candidates */
      for( unsigned int cidx2 = 0; cidx2 < max_photons; cidx2++ )
      {
        /* skip if trying to combine cluster with itself */
        if ( cidx1 == cidx2 )
          continue;

        emcClusterContent* photon2 = data_photons->getCluster( cidx2 );

        /* Fill invariant mass for two-photon pair in histogram */
        double photon12_invMass = anatools::GetInvMass( photon1, photon2 );

        double fill_histo_2photon[] = { (double)sector1,
          photon1_pT,
          photon12_invMass,
          photon1_eta,
          photon1_phi,
          (double)FILL_ERT_null,
          photon1_isolated };

        hn_2photon->Fill( fill_histo_2photon );

        /* Require the target cluster fires the trigger */
        if( ( lvl1_scaled & anatools::Mask_ERT_4x4a ) && trig_ert_a )
        {
          double fill_histo_2photon[] = { (double)sector1,
            photon1_pT,
            photon12_invMass,
            photon1_eta,
            photon1_phi,
            (double)FILL_ERT_4x4a,
            photon1_isolated };

          hn_2photon->Fill( fill_histo_2photon );
        }
        if( ( lvl1_scaled & anatools::Mask_ERT_4x4b ) && trig_ert_b )
        {
          double fill_histo_2photon[] = { (double)sector1,
            photon1_pT,
            photon12_invMass,
            photon1_eta,
            photon1_phi,
            (double)FILL_ERT_4x4b,
            photon1_isolated };

          hn_2photon->Fill( fill_histo_2photon );
        }
        if( ( lvl1_scaled & anatools::Mask_ERT_4x4c ) && trig_ert_c )
        {
          double fill_histo_2photon[] = { (double)sector1,
            photon1_pT,
            photon12_invMass,
            photon1_eta,
            photon1_phi,
            (double)FILL_ERT_4x4c,
            photon1_isolated };

          hn_2photon->Fill( fill_histo_2photon );
        }


      } // loop cluster 2
    } // photon1 in tight fiducial?
  } // loop photon1

  return 0;
}
/* ----------------------------------------------- */

  int
DirectPhotonPP::End(PHCompositeNode *topNode)
{
  /* Write histogram output to ROOT file */
  _hm->dumpHistos();
  delete _hm;

  /* clean up */
  if ( _emcrecalib )
    delete _emcrecalib;

  if ( _emcrecalib_sasha )
    delete _emcrecalib_sasha;

  if ( _emcwarnmap )
    delete _emcwarnmap;

  return EVENT_OK;
}

/* ----------------------------------------------- */

  int
DirectPhotonPP::get_tower_status( int sector,
    int ybin,
    int zbin )
{
  try
  {
    anatools::TowerID(sector, ybin, zbin);
  }
  catch(int i)
  {
    return 9999;
  }

  return _tower_status[sector][ybin][zbin];
}

/* ----------------------------------------------- */

  bool
DirectPhotonPP::testGoodTower( emcClusterContent *emccluster )
{
  int status = get_tower_status(
      anatools::CorrectClusterSector( emccluster->arm() , emccluster->sector() ),
      emccluster->iypos(),
      emccluster->izpos() );

  //if ( ( status == 0 ) || ( status == 10 ) )
  if ( status == 0 )
    return true;
  else
    return false;
}

/* ----------------------------------------------- */

  bool
DirectPhotonPP::testTightFiducial( emcClusterContent *emccluster )
{
  int status = get_tower_status(
      anatools::CorrectClusterSector( emccluster->arm() , emccluster->sector() ),
      emccluster->iypos(),
      emccluster->izpos() );

  if ( status == 0 )
    return true;

  else
    return false;
}

/* ----------------------------------------------- */

//bool
//DirectPhotonPP::testPhoton( emcClusterContent *emccluster,
//                            double bbc_t0 )
//{
//  bool test_e = testPhotonEnergy( emccluster );
//  bool test_tof = testPhotonTof( emccluster, bbc_t0 );
//  bool test_shape = testPhotonShape( emccluster );
//  bool test_trackveto = testPhotonTrackVeto( emccluster );
//
//  return ( test_e && test_tof && test_shape && test_trackveto );
//}

/* ----------------------------------------------- */

  bool
DirectPhotonPP::testDirectPhotonEnergy( emcClusterContent *emccluster )
{
  bool test_direct_photon_energy = false;

  if ( emccluster->ecore() > _direct_photon_energy_min )
    test_direct_photon_energy = true;

  return ( test_direct_photon_energy );
}

/* ----------------------------------------------- */

  bool
DirectPhotonPP::testIsolatedCluster( emcClusterContent *emccluster0 ,
    emcClusterContainer *emc ,
    PHCentralTrack *tracks ,
    double coneangle ,
    double threshold )
{
  // check isolation
  double isocone_energy = 0;

  // get cluster angles in radians
  double phi0 = emccluster0->phi();
  double theta0 = emccluster0->theta();
  //double eta0 = -log(tan(theta0/2.0));

  // what does PHAngle( angle ) do?

  int nemccluster = emc->size();

  for( int i = 0; i < nemccluster; i++ )
  {
    emcClusterContent *emccluster1 = emc->getCluster(i);

    // avoid cluster double counting
    if ( emccluster0->id() == emccluster1->id() )
      continue;

    // get cluster angles in radians
    double phi1 = emccluster1->phi();
    double theta1 = emccluster1->theta();

    // add energy from clusters within cone range
    double dphi = ( phi0 - phi1 );
    double dtheta = ( theta0 - theta1 );

    if ( sqrt( dphi*dphi + dtheta*dtheta ) < coneangle
        && sqrt( dphi*dphi + dtheta*dtheta ) != 0 )
      isocone_energy+=emccluster1->ecore();
  }

  // if isolated
  //cout << "Test isolated photon: " << isocone_energy << " in cone for iso threshold " << 0.1 * emccluster0->ecore() << endl;
  if ( isocone_energy < ( threshold * emccluster0->ecore() ) )
    return true;

  // else
  else
    return false;
}

/* ----------------------------------------------- */

  bool
DirectPhotonPP::testPhotonEnergy( emcClusterContent *emccluster )
{
  if ( emccluster->ecore() > _photon_energy_min )
    return true;
  else
    return false;
}

/* ----------------------------------------------- */

  bool
DirectPhotonPP::testPhotonTof( emcClusterContent *emccluster,
    double bbc_t0 )
{
  if ( emccluster->tofcorr() - bbc_t0 > _photon_tof_min && emccluster->tofcorr() - bbc_t0 < _photon_tof_max )
    return true;
  else
    return false;
}

/* ----------------------------------------------- */

  bool
DirectPhotonPP::testPhotonShape( emcClusterContent *emccluster )
{
  if ( emccluster->prob_photon() > _photon_prob_min )
    return true;
  else
    return false;
}

/* ----------------------------------------------- */

  bool
DirectPhotonPP::testPhotonTrackVeto( emcClusterContent *emccluster )
{
  // Angle between EMC cluster and PC3 track
  double theta_cv = anatools::GetTheta_CV(emccluster);

  // Charge veto in medium dtheta
  double emc_e = emccluster->ecore();
  int sector = anatools::CorrectClusterSector( emccluster->arm() , emccluster->sector() );
  if(sector<6)
  {
    if( theta_cv > 4.22*pow(10,-4) - 1.16*pow(10,-2)*emc_e - 4.53*pow(10,-3)*pow(emc_e,2) &&
        theta_cv < 1.01*pow(10,-1) - 2.02*pow(10,-1)*emc_e + 1.51*pow(10,-1)*pow(emc_e,2) - 3.66*pow(10,-2)*pow(emc_e,3) )
      return false;
  }
  else
  {
    if( theta_cv > 1.27*pow(10,-2) - 2.41*pow(10,-2)*emc_e + 2.26*pow(10,-2)*pow(emc_e,2) &&
        theta_cv < 1.64*pow(10,-2) - 7.38*pow(10,-3)*emc_e + 1.45*pow(10,-1)*exp(-4*emc_e) )
      return false;
  }

  return true;
}

/* ----------------------------------------------- */

  int
DirectPhotonPP::selectClusterGoodTower( emcClusterContainer *emc )
{
  unsigned nemccluster = emc->size();

  for( unsigned i = 0; i < nemccluster; i++ )
  {
    unsigned test_idx = nemccluster - 1 - i;

    emcClusterContent *emccluster = emc->getCluster(test_idx);

    if ( testGoodTower( emccluster ) == false )
      emc->removeCluster(test_idx);
  }

  return 0;
}

/* ----------------------------------------------- */


  int
DirectPhotonPP::selectClusterPhotonShape( emcClusterContainer *emc )
{
  unsigned nemccluster = emc->size();

  for( unsigned i = 0; i < nemccluster; i++ )
  {
    unsigned test_idx = nemccluster - 1 - i;

    emcClusterContent *emccluster = emc->getCluster(test_idx);

    if ( testPhotonShape( emccluster ) == false )
      emc->removeCluster(test_idx);
  }

  return 0;
}

/* ----------------------------------------------- */


  int
DirectPhotonPP::selectClusterPhotonEnergy( emcClusterContainer *emc )
{
  unsigned nemccluster = emc->size();

  for( unsigned i = 0; i < nemccluster; i++ )
  {
    unsigned test_idx = nemccluster - 1 - i;

    emcClusterContent *emccluster = emc->getCluster(test_idx);

    if ( testPhotonEnergy( emccluster ) == false )
      emc->removeCluster(test_idx);
  }

  return 0;
}

/* ----------------------------------------------- */

  int
DirectPhotonPP::selectClusterPhotonTof( emcClusterContainer *emc, double bbc_t0 )
{
  unsigned nemccluster = emc->size();

  for( unsigned i = 0; i < nemccluster; i++ )
  {
    unsigned test_idx = nemccluster - 1 - i;

    emcClusterContent *emccluster = emc->getCluster(test_idx);

    if ( testPhotonTof( emccluster, bbc_t0 ) == false )
      emc->removeCluster(test_idx);
  }

  return 0;
}

/* ----------------------------------------------- */

//int
//DirectPhotonPP::selectClusterIsolation( emcClusterContainer *emc ,
//                                        emcClusterContainer *emc_bg ,
//                                        PHCentralTrack* data_tracks )
//{
//  unsigned nemccluster = emc->size();
//
//  for( unsigned i = 0; i < nemccluster; i++ )
//    {
//      unsigned test_idx = nemccluster - 1 - i;
//
//      emcClusterContent *emccluster = emc->getCluster(test_idx);
//
//      if ( testIsolatedCluster( emccluster , emc_bg, data_tracks ) == false )
//        emc->removeCluster(test_idx);
//    }
//
//  return 0;
//}

/* ----------------------------------------------- */

  void
DirectPhotonPP::PrintClusterContainer( emcClusterContainer *emc , double bbc_t0 = 0 )
{
  unsigned nemccluster = emc->size();

  cout << " *** Number of clusters: " << nemccluster << endl;
  cout << " *** id ecore photon_prob tof-bbc_t0 sector tower_y tower_z tower_id tower_status" << endl;

  for( unsigned i = 0; i < nemccluster; i++ )
  {
    emcClusterContent *emccluster = emc->getCluster(i);

    cout << " *** "
      << i << "\t"
      << emccluster->ecore() << "\t"
      << emccluster->prob_photon() << "\t"
      << emccluster->tofcorr() - bbc_t0 << "\t"
      << anatools::CorrectClusterSector( emccluster->arm(), emccluster->sector() ) << "\t"
      << emccluster->iypos() << "\t"
      << emccluster->izpos() << "\t"
      << anatools::TowerID(
          anatools::CorrectClusterSector( emccluster->arm() , emccluster->sector() ),
          emccluster->iypos(),
          emccluster->izpos() ) << "\t"
      << get_tower_status(
          anatools::CorrectClusterSector( emccluster->arm() , emccluster->sector() ),
          emccluster->iypos(),
          emccluster->izpos() ) << "\t"
      << endl;
  }

  return;
}
