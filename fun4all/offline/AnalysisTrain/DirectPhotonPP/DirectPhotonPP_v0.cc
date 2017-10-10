#include "DirectPhotonPP.h"

#include "AnaToolsTowerID.h" // always put includes with "" before those with <>
#include "AnaToolsCluster.h"
#include "AnaToolsTrigger.h"

#include "HistogramBooker.h"
#include "EmcLocalRecalibrator.h"

//#include <Fun4AllServer.h>
//#include <PHCompositeNode.h>
#include <getClass.h>
//#include <phool.h>
//#include <PHDataNode.h>
//#include <PHIODataNode.h>
#include <Fun4AllHistoManager.h>
#include <Fun4AllReturnCodes.h>

/* Other Fun4All header */
//#include <EmcIndexer.h>
#include <TrigLvl1.h>
#include <PHCentralTrack.h>
//#include <EventHeader.h>
#include <RunHeader.h>
#include <emcClusterContent.h>
#include <emcClusterContainer.h>
//#include <SvxCentralTrackList.h>
//#include <SvxCentralTrack.h>
//#include <SvxClusterList.h>
//#include <PHAngle.h>
#include <ErtOut.h>
#include <PHGlobal.h>
#include <TOAD.h>
//#include <PHAngle.h>
#include <SpinDBOutput.hh>
#include <SpinDBContent.hh>

/* ROOT header */
#include <TH1.h>
#include <TH2.h>
#include <TH3.h>
#include <THnSparse.h>
#include <TFile.h>
//#include <TF1.h>
//#include <TGraph.h>
//#include <TTree.h>
#include <TLorentzVector.h>
//#include <TVector3.h>
//#include <TriggerHelper.h>

/* STL / boost header */
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cmath>
//#include <stdexcept>
//#include <boost/lexical_cast.hpp>

using namespace std;

DirectPhotonPP::DirectPhotonPP(const char* outfile) :
  _ievent( 0 ),
  _bbc_zvertex_cut( 10 ),
  _photon_energy_min( 0.3 ), // 0.3 used by Paul for run 9 pp 500 GeV
  _photon_prob_min( 0.02 ),
  _photon_tof_min( -10 ),
  _photon_tof_max( 10 ),
  _direct_photon_energy_min( 1.0 ),
  _emcrecalib( NULL )
{
  /* Initialize array for tower status */
  for(int isector = 0; isector < 8; isector++)
    for(int ibiny = 0; ibiny < 48; ibiny++)
      for(int ibinz = 0; ibinz < 96; ibinz++)
        _tower_status[isector][ibiny][ibinz]=9999;

  /* construct output file names */
  _outfile_histos = "DirectPhotonPP-";
  _outfile_histos.append( outfile );

  /* initialize histogram manager */
  _hm = HistogramBooker::GetHistoManager( "DirectPhotonPPHistoManager" );
  //  _hm = new Fun4AllHistoManager( "DirectPhotonPPHistoManager" );
  _hm->setOutfileName( _outfile_histos );
  _hm->Print("ALL");
}

/* ----------------------------------------------- */

DirectPhotonPP::~DirectPhotonPP()
{
}

/* ----------------------------------------------- */

int
DirectPhotonPP::Init(PHCompositeNode *topNode)
{
  //ReadTowerStatus( "Warnmap_Run13pp510.txt" );
  ReadSashaWarnmap( "warn_all_run13pp500gev.dat" );

  return EVENT_OK;
}

/* ----------------------------------------------- */

int
DirectPhotonPP::InitRun(PHCompositeNode *topNode)
{
  if ( !_emcrecalib )
    {
      cout << "No EmcLocalRecalibrator set. Exit." << endl;
      return ABORTRUN;
    }

  /* Get run number */
  RunHeader* runheader = findNode::getClass<RunHeader>(topNode, "RunHeader");
  if(runheader == NULL)
    {
      cout << "No RunHeader." << endl;
      return ABORTRUN;
    }
  int runnumber = runheader->get_RunNumber();

  /* Get fill number */
  SpinDBContent spin_cont;
  SpinDBOutput spin_out;

  /*Initialize opbject to access spin DB*/
  spin_out.Initialize();
  spin_out.SetUserName("phnxrc");
  spin_out.SetTableName("spin");

  /* Retrieve entry from Spin DB and get fill number */
  int qa_level=spin_out.GetDefaultQA(runnumber);
  spin_out.StoreDBContent(runnumber, runnumber, qa_level);
  spin_out.GetDBContentStore(spin_cont, runnumber);
  int fillnumber = spin_cont.GetFillNumber();

  /* Load EMCal recalibrations for run and fill */
  _emcrecalib->ReadEnergyCorrection( runnumber );
  _emcrecalib->ReadTofCorrection( fillnumber );

  return EVENT_OK;
}

/* ----------------------------------------------- */

int
DirectPhotonPP::process_event(PHCompositeNode *topNode)
{
  _ievent++;

  /* retrieve all histograms used in this function */
  TH1* h1_events = static_cast<TH1*>( _hm->getHisto("h1_events") );

  h1_events->Fill("all",1);

  /*
   *  Get pointer to data nodes
   */
  emcClusterContainer* data_emccontainer_raw = findNode::getClass<emcClusterContainer> (topNode, "emcClusterContainer");
  PHCentralTrack* data_tracks = findNode::getClass<PHCentralTrack> (topNode, "PHCentralTrack");
  PHGlobal* data_global = findNode::getClass<PHGlobal> (topNode, "PHGlobal");
  ErtOut* data_ert = findNode::getClass<ErtOut> (topNode, "ErtOut");
  TrigLvl1* data_triggerlvl1 = findNode::getClass<TrigLvl1> (topNode, "TrigLvl1");
  //data_runheader = findNode::getClass<RunHeader> (topNode, "RunHeader");
  //data_eventheader = findNode::getClass<EventHeader> (topNode, "EventHeader");

  /*
   * Check availability of data nodes
   */
  if(!data_global)
    {
      cout<<"\nABORT RUN\nNo gbl"<<endl;
      return DISCARDEVENT;
    }

  if(!data_emccontainer_raw)
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

  /* Run local recalibration of EMCal cluster data */
  emcClusterContainer* data_emccontainer = data_emccontainer_raw->clone();
  _emcrecalib->ApplyClusterCorrection( data_emccontainer );


  /* Get trigger information */
  const unsigned int bit_4x4b  = 0x00000040;
  const unsigned int bit_4x4a  = 0x00000080;
  const unsigned int bit_4x4c  = 0x00000100;
  const unsigned int bit_4x4or = 0x000001C0;

  //unsigned int lvl1_raw = data_triggerlvl1->get_lvl1_trigraw();
  unsigned int lvl1_live = data_triggerlvl1->get_lvl1_triglive();
  //unsigned int lvl1_scaled = data_triggerlvl1->get_lvl1_trigscaled();

  /* Get global event parameters */
  float bbc_z  = data_global->getBbcZVertex();

  /* Count event trigger stats */
  if ( abs ( bbc_z ) <= _bbc_zvertex_cut )
    {
      h1_events->Fill("bbcz10",1);

      if ( lvl1_live & bit_4x4b )
        h1_events->Fill("bbcz10_ert4x4b",1);

      if ( lvl1_live & bit_4x4a )
        h1_events->Fill("bbcz10_ert4x4a",1);

      if ( lvl1_live & bit_4x4c )
        h1_events->Fill("bbcz10_ert4x4c",1);

      if ( lvl1_live & bit_4x4or )
        h1_events->Fill("bbcz10_ert4x4or",1);
    }

  /* Count events to calculate trigger efficiency */
  FillTriggerEfficiency( data_emccontainer, data_global, data_ert );

  /* Look at all clusters as crosscheck of warnmap */
  FillClusterPtSpectrum( data_emccontainer , data_global );

  /* Store TOF information for cluster as calibration check */
  FillClusterTofSpectrum( data_emccontainer , data_global );
  FillClusterTofSpectrum( data_emccontainer_raw , data_global , "raw" );

  /* Analyze pi0s events for crosscheck */
  FillPi0InvariantMass( data_emccontainer , data_global , data_triggerlvl1 , data_ert );
  FillPi0InvariantMass( data_emccontainer_raw , data_global , data_triggerlvl1 , data_ert , "raw" );

  /* Analyze direct photon events */
  if ( abs ( bbc_z ) <= _bbc_zvertex_cut && ( lvl1_live & bit_4x4b ) )
    {
      FillPhotonPtSpectrum( data_emccontainer , data_tracks , data_global );
    }

  /* clean up */
  delete data_emccontainer;

  return EVENT_OK;
}

/* ----------------------------------------------- */
int
DirectPhotonPP::FillTriggerEfficiency( emcClusterContainer *data_emccontainer,
                                       PHGlobal *data_global,
                                       ErtOut *data_ert )
{
  /* retrieve all histograms used in this function */
  TH3 *h3_trig = static_cast<TH3*>( _hm->getHisto("h3_trig") );
  TH3 *h3_trig_pion = static_cast<TH3*>( _hm->getHisto("h3_trig_pion") );

  /* Get event global parameters */
  double bbc_z = data_global->getBbcZVertex();
  double bbct0 = data_global->getBbcTimeZero();
  if( fabs(bbc_z) > 30. ) return 1;

  int nemccluster = data_emccontainer->size();

  /* Fire ERT on arm 0 (west) or 1 (east)*/
  bool FireERT[2] = {};

  for(int i=0; i<nemccluster; i++)
    {
      emcClusterContent *emccluster = data_emccontainer->getCluster(i);
      int arm = emccluster->arm();
      if( anatools::PassERT(data_ert, emccluster, anatools::ERT_4x4a) ||
          anatools::PassERT(data_ert, emccluster, anatools::ERT_4x4b) ||
          anatools::PassERT(data_ert, emccluster, anatools::ERT_4x4c) )
        FireERT[arm] = true;
    }

  vector<int> v_used;
  v_used.clear();

  for(int i=0; i<nemccluster; i++)
    {
      emcClusterContent *emccluster1 = data_emccontainer->getCluster(i);
      int sector = anatools::CorrectClusterSector( emccluster1->arm(), emccluster1->sector() );
      double cluster_pT = anatools::Get_pT( emccluster1 );

      /* Require the other arm to be fired */
      int oarm = emccluster1->arm()==0 ? 1 : 0;
      if( !FireERT[oarm] ) continue;

      if( testGoodTower(emccluster1) &&
          testPhoton(emccluster1, bbct0) &&
          emccluster1->ecore() > _photon_energy_min )
        {
          v_used.push_back(i);

          h3_trig->Fill(cluster_pT, sector, "all", 1.);

          if( anatools::PassERT(data_ert, emccluster1, anatools::ERT_4x4a) )
            h3_trig->Fill(cluster_pT, sector, "ERT4x4a", 1.);

          if( anatools::PassERT(data_ert, emccluster1, anatools::ERT_4x4b) )
            h3_trig->Fill(cluster_pT, sector, "ERT4x4b", 1.);

          if( anatools::PassERT(data_ert, emccluster1, anatools::ERT_4x4c) )
            h3_trig->Fill(cluster_pT, sector, "ERT4x4c", 1.);

          for(int j=0; j<nemccluster; j++)
            if( j != i && find(v_used.begin(), v_used.end(), j) == v_used.end() )
              {
                emcClusterContent *emccluster2 = data_emccontainer->getCluster(j);
                double tot_pT = anatools::GetTot_pT(emccluster1, emccluster2);

                if( testGoodTower(emccluster2) &&
                    testPhoton(emccluster2, bbct0) &&
                    emccluster2->ecore() > _photon_energy_min )
                  {
                    double minv = anatools::GetInvMass(emccluster1, emccluster2);
                    if( minv < 0.112 || minv > 0.162 )
                      continue;

                    h3_trig_pion->Fill(tot_pT, sector, "all", 1.);

                    if( anatools::PassERT(data_ert, emccluster1, anatools::ERT_4x4a) ||
                        anatools::PassERT(data_ert, emccluster2, anatools::ERT_4x4a) )
                      h3_trig_pion->Fill(tot_pT, sector, "ERT4x4a", 1.);

                    if( anatools::PassERT(data_ert, emccluster1, anatools::ERT_4x4b) ||
                        anatools::PassERT(data_ert, emccluster2, anatools::ERT_4x4b) )
                      h3_trig_pion->Fill(tot_pT, sector, "ERT4x4b", 1.);

                    if( anatools::PassERT(data_ert, emccluster1, anatools::ERT_4x4c) ||
                        anatools::PassERT(data_ert, emccluster2, anatools::ERT_4x4c) )
                      h3_trig_pion->Fill(tot_pT, sector, "ERT4x4c", 1.);
                  }
              }
        }
    }

  return 0;
}

/* ----------------------------------------------- */

int
DirectPhotonPP::FillClusterPtSpectrum( emcClusterContainer *data_emccontainer,
                                       PHGlobal *data_global )
{

  /* retrieve all histograms used in this function */
  TH2* h2_pT_1cluster_nowarn = static_cast<TH2*>( _hm->getHisto("h2_pT_1cluster_nowarn") );
  TH2* h2_pT_1cluster        = static_cast<TH2*>( _hm->getHisto("h2_pT_1cluster") );

  /* Analyze all cluster in this event and fill pT spectrum */
  int nemccluster;
  nemccluster = data_emccontainer->size();

  for( int i = 0; i < nemccluster; i++ )
    {
      emcClusterContent *emccluster = data_emccontainer->getCluster(i);

      int sector = anatools::CorrectClusterSector( emccluster->arm() , emccluster->sector() );

      float cluster_pT = anatools::Get_pT( emccluster );

      h2_pT_1cluster_nowarn->Fill( cluster_pT, sector );

      /* Store cluster pT only for 'good' tower */
      if ( testGoodTower( emccluster ) )
        h2_pT_1cluster->Fill( cluster_pT, sector );
    }

  return 0;
}

/* ----------------------------------------------- */

int
DirectPhotonPP::FillClusterTofSpectrum( emcClusterContainer *data_emccontainer,
                                        PHGlobal *data_global,
                                        string quali )
{

  /* retrieve all histograms used in this function */
  TH3* h3_tof_raw = static_cast<TH3*>( _hm->getHisto("h3_tof_raw") );
  TH3* h3_tof     = static_cast<TH3*>( _hm->getHisto("h3_tof") );

  /* Analyze all cluster in this event and fill TOF spectrum */
  int nemccluster;
  nemccluster = data_emccontainer->size();

  for( int i = 0; i < nemccluster; i++ )
    {
      emcClusterContent *emccluster = data_emccontainer->getCluster(i);

      if( testGoodTower( emccluster ) &&
          abs( data_global->getBbcZVertex() ) < 30 &&
          testPhotonEnergy( emccluster ) &&
          testPhotonShape( emccluster ) )
        {
          int sector = anatools::CorrectClusterSector( emccluster->arm() , emccluster->sector() );

          double bbct0 = data_global->getBbcTimeZero();
          double tof = emccluster->tofcorr() - bbct0;

          double pT = anatools::Get_pT(emccluster);

          if ( quali == "raw" )
            h3_tof_raw->Fill( sector, pT, tof );
          else
            h3_tof->Fill( sector, pT, tof );
        }
    }

  return 0;
}

/* ----------------------------------------------- */

int
DirectPhotonPP::FillPi0InvariantMass( emcClusterContainer *data_emccontainer,
                                      PHGlobal *data_global,
                                      TrigLvl1* data_triggerlvl1,
                                      ErtOut *data_ert,
                                      string quali )
{
  /* retrieve all histograms used in this function */
  TH3* h3_inv_mass_pi0calib_raw = static_cast<TH3*>( _hm->getHisto("h3_inv_mass_pi0calib_raw") );
  TH3* h3_inv_mass_pi0calib     = static_cast<TH3*>( _hm->getHisto("h3_inv_mass_pi0calib") );
  THnSparse* hn_pion            = static_cast<THnSparse*>( _hm->getHisto("hn_pion") );

  /* Get event global parameters */
  double bbc_z = data_global->getBbcZVertex();
  double bbct0 = data_global->getBbcTimeZero();
  if( abs(bbc_z) > 30. ) return 1;

  /* Get trigger information */
  const unsigned int bit_4x4b  = 0x00000040;
  const unsigned int bit_4x4a  = 0x00000080;
  const unsigned int bit_4x4c  = 0x00000100;
  const unsigned int bit_4x4or = 0x000001C0;

  //unsigned int lvl1_raw = data_triggerlvl1->get_lvl1_trigraw();
  unsigned int lvl1_live = data_triggerlvl1->get_lvl1_triglive();
  unsigned int lvl1_scaled = data_triggerlvl1->get_lvl1_trigscaled();

  /* Trigger selection */
  enum bin_selection { ERT_4x4a = 0,
                       ERT_4x4b = 1,
                       ERT_4x4c = 2 };

  unsigned int nemccluster = data_emccontainer->size();

  /* NEW method: Make all possible cluster combinations, avoid duplicate combinations */
  vector< unsigned int > v_used;

  /* loop over all EMCal cluster */
  for( unsigned int cidx1 = 0; cidx1 < nemccluster; cidx1++ )
    {
      emcClusterContent *emccluster1 = data_emccontainer->getCluster( cidx1 );

      if ( testTightFiducial( emccluster1 )
           && testPhoton( emccluster1 , bbct0 )
           //&& testPhotonEnergy( emccluster1 )
           //&& testPhotonShape( emccluster1 )
           )
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

              emcClusterContent* emccluster2 = data_emccontainer->getCluster( cidx2 );

              if ( testGoodTower( emccluster2 )
                   && testPhoton( emccluster2 , bbct0 )
                   //&& testPhotonEnergy( emccluster2 )
                   //&& testPhotonShape( emccluster2 )
                   && anatools::GetAsymmetry_E( emccluster1, emccluster2 ) < 0.8
                   )
                {
                  int sector1 = anatools::CorrectClusterSector( emccluster1->arm() , emccluster1->sector() );
                  int sector2 = anatools::CorrectClusterSector( emccluster2->arm() , emccluster2->sector() );

                  /* Require two photons are from the same part of the EMCal */
                  int is = 3;
                  if( sector1<4 && sector2<4 ) // W0,1,2,3
                    is = 0;
                  else if( (sector1==4 || sector1==5) && (sector2==4 || sector2==5) ) // E2,3
                    is = 1;
                  else if( (sector1==6 || sector1==7) && (sector2==6 || sector2==7) ) // PbGl
                    is = 2;
                  else
                    is = 3;
                  if(is==3) continue;

                  /* pE = {px, py, pz, ecore} */
                  TLorentzVector photon1_pE = anatools::Get_pE(emccluster1);
                  TLorentzVector photon2_pE = anatools::Get_pE(emccluster2);
                  TLorentzVector tot_pE =  photon1_pE + photon2_pE;
                  double tot_px = tot_pE.Px();
                  double tot_py = tot_pE.Py();
                  double tot_pz = tot_pE.Pz();
                  double tot_pT = tot_pE.Pt();
                  double tot_mom = tot_pE.P();

                  /* Fill invariant mass for pi0 candidate in histogram */
                  double invMass = anatools::GetInvMass( emccluster1, emccluster2 );

                  /* Use the photon which has higher energy to label the sector and trigger */
                  int sector = sector1;
                  int trig1 = anatools::PassERT(data_ert, emccluster1, anatools::ERT_4x4a);
                  int trig2 = anatools::PassERT(data_ert, emccluster1, anatools::ERT_4x4b);
                  int trig3 = anatools::PassERT(data_ert, emccluster1, anatools::ERT_4x4c);
                  if( photon2_pE.E() > photon1_pE.E() )
                    {
                      sector = sector2;
                      trig1 = anatools::PassERT(data_ert, emccluster2, anatools::ERT_4x4a);
                      trig2 = anatools::PassERT(data_ert, emccluster2, anatools::ERT_4x4b);
                      trig3 = anatools::PassERT(data_ert, emccluster2, anatools::ERT_4x4c);
                    }

                  /* Fill eta and phi */
                  double tot_eta = tot_mom > 0. ? atan(tot_pz/tot_mom) : 9999.;
                  double tot_phi = tot_px > 0. ? atan(tot_py/tot_px) : 3.1416+atan(tot_py/tot_px);

                  /* Require the target cluster fires the trigger */
                  if( quali != "raw" && abs(bbc_z) < 10. )
                    {
                      if( ( lvl1_scaled & bit_4x4a ) && trig1 )
                        {
                          double fill_hn_pion[] = {sector, tot_pT, invMass, tot_eta, tot_phi, ERT_4x4a};
                          hn_pion->Fill(fill_hn_pion);
                        }
                      if( ( lvl1_scaled & bit_4x4b ) && trig2 )
                        {
                          double fill_hn_pion[] = {sector, tot_pT, invMass, tot_eta, tot_phi, ERT_4x4b};
                          hn_pion->Fill(fill_hn_pion);
                        }
                      if( ( lvl1_scaled & bit_4x4c ) && trig3 )
                        {
                          double fill_hn_pion[] = {sector, tot_pT, invMass, tot_eta, tot_phi, ERT_4x4c};
                          hn_pion->Fill(fill_hn_pion);
                        }
                    }

                  /* more restrictive photon candidate pair selection for sector-by-sector pi0 energy
                   * calibration
                   */
                  if ( ( sector1 == sector2 ) && ( lvl1_live & bit_4x4or ) )
                    {
                      if ( quali == "raw" )
                        h3_inv_mass_pi0calib_raw->Fill(sector1, tot_pT, invMass);
                      else
                        h3_inv_mass_pi0calib->Fill(sector1, tot_pT, invMass);
                    } // check sector
                } // check warnmap cluster 2
            } // loop cluster 2
        } // check warnmap cluster 1
    } // loop cluster 1

  return 0;
}

/* ----------------------------------------------- */

int
DirectPhotonPP::FillPhotonPtSpectrum( emcClusterContainer *d_emccontainer,
                                      PHCentralTrack* d_tracks,
                                      PHGlobal *d_global )
{
  TH1* h1_nphotons               = static_cast<TH1*>( _hm->getHisto("h1_nphotons") );
  THnSparse* hn_1photon          = static_cast<THnSparse*>( _hm->getHisto("hn_1photon") );
  THnSparse* hn_2photon          = static_cast<THnSparse*>( _hm->getHisto("hn_2photon") );
  THnSparse* hn_2photon_theta_cv = static_cast<THnSparse*>( _hm->getHisto("hn_2photon_theta_cv") );

  /* Analyze all photon candidates in this event and fill pT spectrum */
  unsigned int nemccluster = d_emccontainer->size();

  /* Get event global parameters */
  double bbct0 = d_global->getBbcTimeZero();

  /* counter for number of 'direct photon candidates' found in this event */
  long nphotons = 0;

  /* Enum identifying level of photon selection / cut */
  enum bin_selection { CUT_ISOPHOTON = 0,
                       CUT_DIRECTPHOTON = 1,
                       CUT_ENERGY_SHAPE_TRACK_TOF = 2,
                       CUT_ENERGY_SHAPE_TRACK = 3,
                       CUT_ENERGY_SHAPE = 4,
                       CUT_ENERGY = 5 };

  /* loop over all EMCal cluster */
  for( unsigned int cidx1 = 0; cidx1 < nemccluster; cidx1++ ) { emcClusterContent *emccluster1 = d_emccontainer->getCluster( cidx1 );

    if ( testTightFiducial( emccluster1 ) )
      {
        int sector1 = anatools::CorrectClusterSector( emccluster1->arm() , emccluster1->sector() );

        /* pE = {px, py, pz, ecore} */
        TLorentzVector photon1_pE = anatools::Get_pE( emccluster1 );
        double photon1_px = photon1_pE.Px();
        double photon1_py = photon1_pE.Py();
        double photon1_pz = photon1_pE.Pz();
        double photon1_pT = photon1_pE.Pt();
        double photon1_ptotal = photon1_pE.P();
        double photon1_E = photon1_pE.E();

        /* Fill eta and phi */
        double photon1_eta = photon1_ptotal > 0. ? atan(photon1_pz/photon1_ptotal) : 9999.;
        double photon1_phi = photon1_px > 0. ? atan(photon1_py/photon1_px) : 3.1416+atan(photon1_py/photon1_px);

        /* Record information for 1-photon: */

        /* fill isolated direct photons pT histogram */
        if ( testIsolatedPhoton( emccluster1 , d_emccontainer, d_tracks , 0.4 , bbct0 ) )
          {
            double fill_histo_1photon[] = { sector1,
                                            CUT_ISOPHOTON,
                                            photon1_pT,
                                            photon1_E,
                                            photon1_eta,
                                            photon1_phi };

            hn_1photon->Fill( fill_histo_1photon );
          }

        /* fill direct photons pT histogram */
        if ( testDirectPhoton( emccluster1 , bbct0 ) )
          {
            double fill_histo_1photon[] = { sector1,
                                            CUT_DIRECTPHOTON,
                                            photon1_pT,
                                            photon1_E,
                                            photon1_eta,
                                            photon1_phi };

            hn_1photon->Fill( fill_histo_1photon );

            if ( photon1_E > 1.0 )
              nphotons++;
          }

        /* fill histogram for other trigger settings */
        if ( testPhotonEnergy( emccluster1 )
             && testPhotonShape( emccluster1 )
             && testPhotonTrackVeto( emccluster1 )
             && testPhotonTof( emccluster1, bbct0 ) )
          {
            double fill_histo_1photon[] = { sector1,
                                            CUT_ENERGY_SHAPE_TRACK_TOF,
                                            photon1_pT,
                                            photon1_E,
                                            photon1_eta,
                                            photon1_phi };

            hn_1photon->Fill( fill_histo_1photon );
          }

        if ( testPhotonEnergy( emccluster1 )
             && testPhotonShape( emccluster1 )
             && testPhotonTrackVeto( emccluster1 ) )
          {
            double fill_histo_1photon[] = { sector1,
                                            CUT_ENERGY_SHAPE_TRACK,
                                            photon1_pT,
                                            photon1_E,
                                            photon1_eta,
                                            photon1_phi };

            hn_1photon->Fill( fill_histo_1photon );
          }

        if ( testPhotonEnergy( emccluster1 )
             && testPhotonShape( emccluster1 ) )
          {
            double fill_histo_1photon[] = { sector1,
                                            CUT_ENERGY_SHAPE,
                                            photon1_pT,
                                            photon1_E,
                                            photon1_eta,
                                            photon1_phi };

            hn_1photon->Fill( fill_histo_1photon );
          }

        if ( testPhotonEnergy( emccluster1 ) )
          {
            double fill_histo_1photon[] = { sector1,
                                            CUT_ENERGY,
                                            photon1_pT,
                                            photon1_E,
                                            photon1_eta,
                                            photon1_phi };

            hn_1photon->Fill( fill_histo_1photon );
          }


        /* Record information for 2-photon pairs: */

        /* loop over partner photon candidates */
        for( unsigned int cidx2 = 0; cidx2 < nemccluster; cidx2++ )
          {
            /* skip if trying to combine cluster with itself */
            if ( cidx1 == cidx2 )
              continue;

            emcClusterContent* emccluster2 = d_emccontainer->getCluster( cidx2 );

            if ( testGoodTower( emccluster2 ) )
              {
                /* Fill invariant mass for two-photon pair in histogram */
                float photon12_invMass = anatools::GetInvMass( emccluster1, emccluster2 );
                float photon12_pT = 0;

                /* Fill theta_cv */
                float theta_cv = anatools::GetTheta_CV( emccluster1 );

                if ( testIsolatedPhoton( emccluster1 , d_emccontainer, d_tracks , 0.4 , bbct0 )
                     && testPhoton( emccluster2 , bbct0 ) )
                  {
                    double fill_histo_inv_mass[] = { sector1,
                                                     CUT_ISOPHOTON,
                                                     photon1_pT,
                                                     photon12_pT,
                                                     photon12_invMass };
                    hn_2photon->Fill( fill_histo_inv_mass );
                  }

                if ( testDirectPhoton( emccluster1 , bbct0 )
                     && testPhoton( emccluster2 , bbct0 ) )
                  {
                    double fill_histo_inv_mass[] = { sector1,
                                                     CUT_DIRECTPHOTON,
                                                     photon1_pT,
                                                     photon12_pT,
                                                     photon12_invMass };
                    hn_2photon->Fill( fill_histo_inv_mass );
                  }

                if ( testPhotonEnergy( emccluster1 )
                     && testPhotonEnergy( emccluster2 )
                     && testPhotonShape( emccluster1 )
                     && testPhotonShape( emccluster2  )
                     && testPhotonTrackVeto( emccluster1 )
                     && testPhotonTrackVeto( emccluster2  )
                     && testPhotonTof( emccluster1, bbct0 )
                     && testPhotonTof( emccluster2, bbct0 ) )
                  {
                    double fill_histo_inv_mass[] = { sector1,
                                                     CUT_ENERGY_SHAPE_TRACK_TOF,
                                                     photon1_pT,
                                                     photon12_pT,
                                                     photon12_invMass };
                    hn_2photon->Fill( fill_histo_inv_mass );
                  }

                if ( testPhotonEnergy( emccluster1 )
                     && testPhotonEnergy( emccluster2 )
                     && testPhotonShape( emccluster1 )
                     && testPhotonShape( emccluster2  )
                     && testPhotonTrackVeto( emccluster1 )
                     && testPhotonTrackVeto( emccluster2  ) )
                  {
                    double fill_histo_inv_mass[] = { sector1,
                                                     CUT_ENERGY_SHAPE_TRACK,
                                                     photon1_pT,
                                                     photon12_pT,
                                                     photon12_invMass };
                    hn_2photon->Fill( fill_histo_inv_mass );
                  }

                if ( testPhotonEnergy( emccluster1 )
                     && testPhotonEnergy( emccluster2 )
                     && testPhotonShape( emccluster1 )
                     && testPhotonShape( emccluster2  ) )
                  {
                    double fill_histo_inv_mass[] = { sector1,
                                                     CUT_ENERGY_SHAPE,
                                                     photon1_pT,
                                                     photon12_pT,
                                                     photon12_invMass };
                    hn_2photon->Fill( fill_histo_inv_mass );
                  }

                if ( testPhotonEnergy( emccluster1 )
                     && testPhotonEnergy( emccluster2  ) )
                  {
                    double fill_histo_inv_mass[] = { sector1,
                                                     CUT_ENERGY,
                                                     photon1_pT,
                                                     photon12_pT,
                                                     photon12_invMass };
                    hn_2photon->Fill( fill_histo_inv_mass );
                  }

                /* Special Charge Veto (CV) histogram: */
                if ( testPhotonEnergy( emccluster1 )
                     && testPhotonEnergy( emccluster2 )
                     && testPhotonTof( emccluster1, bbct0 )
                     && testPhotonTof( emccluster2, bbct0 )
                     && testPhotonShape( emccluster1 )
                     && testPhotonShape( emccluster2 ) )
                  {
                    double fill_histo_inv_mass_theta_cv[] = { sector1, photon1_E , photon12_invMass , theta_cv };
                    hn_2photon_theta_cv->Fill( fill_histo_inv_mass_theta_cv );
                  }

                /* check if invariant mass falls within pi0 range */
                //if ( photon12_invMass > 0.130 && photon12_invMass < 0.145 )
                //    isDirectPhoton = false;

              } // if cluster 2 is in good tower
          } // loop cluster 2
      } // if cluster 1 is in tight fiducial
  } // loop cluster 1

  h1_nphotons->Fill( nphotons );

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
  delete _emcrecalib;

  return EVENT_OK;
}

/* ----------------------------------------------- */

void
DirectPhotonPP::ReadTowerStatus(const string &filename)
{
  unsigned int nBadSc = 0;
  unsigned int nBadGl = 0;

  int sector = 0;
  int biny = 0;
  int binz = 0;
  int status = 0;

  TOAD *toad_loader = new TOAD("DirectPhotonPP");
  string file_location = toad_loader->location(filename);
  cout << "TOAD file location: " << file_location << endl;
  ifstream fin( file_location.c_str() );

  while( fin >> sector >> biny >> binz >> status )
    {
      // count tower with bad status for PbSc and PbGl
      if ( status > 10 )
        {
          if( sector < 6 ) nBadSc++;
          else nBadGl++;
        }
      _tower_status[sector][biny][binz] = status;
    }

  cout << "NBad PbSc: " << nBadSc << ", PbGl: " << nBadGl << endl;
  fin.close();
  delete toad_loader;

  return;
}

/* ----------------------------------------------- */

void
DirectPhotonPP::ReadSashaWarnmap(const string &filename)
{
  unsigned int nBadSc = 0;
  unsigned int nBadGl = 0;

  int ich = 0;
  int sector = 0;
  int biny = 0;
  int binz = 0;
  int status = 0;

  TOAD *toad_loader = new TOAD("DirectPhotonPP");
  string file_location = toad_loader->location(filename);
  cout << "TOAD file location: " << file_location << endl;
  ifstream fin( file_location.c_str() );

  while( fin >> ich >> status )
    {
      // Attention!! I use my indexing for warn map in this program!!!
      if( ich >= 10368 && ich < 15552 ) { // PbSc
        if( ich < 12960 ) ich += 2592;
        else              ich -= 2592;
      }
      else if( ich >= 15552 )           { // PbGl
        if( ich < 20160 ) ich += 4608;
        else              ich -= 4608;
      }

      // get tower location
      anatools::TowerLocation(ich, sector, biny, binz);

      // count tower with bad status for PbSc and PbGl
      if ( status > 0 )
        {
          if( sector < 6 ) nBadSc++;
          else nBadGl++;
        }
      _tower_status[sector][biny][binz] = status;

      // mark edge towers
      if( anatools::Edge_cg(sector, biny, binz) )
        _tower_status[sector][biny][binz] = 20;
    }

  cout << "NBad PbSc: " << nBadSc << ", PbGl: " << nBadGl << endl;
  fin.close();
  delete toad_loader;

  return;
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

bool
DirectPhotonPP::testPhoton( emcClusterContent *emccluster,
                            double bbct0 )
{
  bool test_e = testPhotonEnergy( emccluster );
  bool test_tof = testPhotonTof( emccluster, bbct0 );
  bool test_shape = testPhotonShape( emccluster );
  bool test_trackveto = testPhotonTrackVeto( emccluster );

  return ( test_e && test_tof && test_shape && test_trackveto );
}

/* ----------------------------------------------- */

bool
DirectPhotonPP::testDirectPhoton( emcClusterContent *emccluster,
                                  double bbct0 )
{
  bool test_photon = testPhoton( emccluster , bbct0 );

  bool test_direct_photon_energy = false;

  if ( emccluster->ecore() > _direct_photon_energy_min )
    test_direct_photon_energy = true;

  return ( test_photon && test_direct_photon_energy );
}

/* ----------------------------------------------- */

bool
DirectPhotonPP::testIsolatedPhoton( emcClusterContent *emccluster0 ,
                                    emcClusterContainer *emccontainer ,
                                    PHCentralTrack *tracks ,
                                    double coneangle ,
                                    double bbct0 )
{
  // return false if cluster does not pass photon cut
  if ( ! testPhoton( emccluster0, bbct0 ) )
    return false;

  // check isolation
  float isocone_energy = 0;

  // get cluster angles in radians
  float phi0 = emccluster0->phi();
  float theta0 = emccluster0->theta();
  //float eta0 = -log(tan(theta0/2.0));

  // what does PHAngle( angle ) do?

  int nemccluster = emccontainer->size();

  for( int i = 0; i < nemccluster; i++ )
    {
      emcClusterContent *emccluster1 = emccontainer->getCluster(i);

      // avoid cluster double counting
      if ( emccluster0->id() == emccluster1->id() )
        continue;

      // drop cluster which are not photon candidates
      //if ( ! testPhoton( emccluster1, bbct0 ) )
      // continue;

      // get cluster angles in radians
      float phi1 = emccluster1->phi();
      float theta1 = emccluster1->theta();
      //float eta1 = -log(tan(theta1/2.0));

      // add energy from clusters within cone range
      float dphi = ( phi0 - phi1 );
      float dtheta = ( theta0 - theta1 );

      if ( sqrt( dphi*dphi + dtheta*dtheta ) < coneangle
           && sqrt( dphi*dphi + dtheta*dtheta ) != 0 )
        isocone_energy+=emccluster1->ecore();
    }

  // if isolated
  //cout << "Test isolated photon: " << isocone_energy << " in cone for iso threshold " << 0.1 * emccluster0->ecore() << endl;
  if(isocone_energy<( 0.1 * emccluster0->ecore() ) )
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
                               double bbct0 )
{
  if ( emccluster->tofcorr() - bbct0 > _photon_tof_min && emccluster->tofcorr() - bbct0 < _photon_tof_max )
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