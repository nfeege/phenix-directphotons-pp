#include "PhotonNode.h"

#include "AnaToolsTowerID.h"
#include "AnaToolsCluster.h"

#include "EmcLocalRecalibrator.h"
#include "EmcLocalRecalibratorSasha.h"
#include "EMCWarnmapChecker.h"
#include "PhotonContainer.h"
#include "Photon.h"
#include "PhotonERT.h"
#include "SpinPattern.h"

#include <RunHeader.h>
#include <SpinDBOutput.hh>
#include <SpinDBContent.hh>

#include <PHGlobal.h>
#include <TrigLvl1.h>
#include <ErtOut.h>
#include <emcClusterContainer.h>
#include <emcClusterContent.h>
#include <PHCentralTrack.h>

#include <PHCompositeNode.h>
#include <PHIODataNode.h>
#include <PHNodeIterator.h>

#include <TOAD.h>
#include <getClass.h>
#include <Fun4AllReturnCodes.h>

#include <cstdlib>
#include <cmath>
#include <iostream>
#include <fstream>
#include <string>

using namespace std;

const double PI = TMath::Pi();

PhotonNode::PhotonNode(const string &name) :
  SubsysReco(name),
  emcrecalib(nullptr),
  emcrecalib_sasha(nullptr),
  emcwarnmap(nullptr),
  photoncont(nullptr),
  spinpattern(nullptr),
  runnumber(0),
  fillnumber(0)
{
  datatype = ERT;
}

PhotonNode::~PhotonNode()
{
}

int PhotonNode::Init(PHCompositeNode *topNode)
{
  PHNodeIterator mainIter(topNode);
  PHCompositeNode *dstNode = dynamic_cast<PHCompositeNode*>( mainIter.findFirst("PHCompositeNode", "DST") );
  if(!dstNode)
  {
    cerr << "No DST node" << endl;
    exit(1);
  }

  photoncont = new PhotonContainer();
  PHIODataNode<PHObject> *photonNode = new PHIODataNode<PHObject>(photoncont, "PhotonContainer", "PHObject");
  if(!photoncont || !photonNode)
  {
    cerr << "Failure to create photon node" << endl;
    exit(1);
  }
  dstNode->addNode(photonNode);

  PHCompositeNode *runNode = dynamic_cast<PHCompositeNode*>( mainIter.findFirst("PHCompositeNode", "RUN") );
  if(!runNode)
  {
    cerr << "No RUN node" << endl;
    exit(1);
  }

  spinpattern = new SpinPattern();
  PHIODataNode<PHObject> *spinNode = new PHIODataNode<PHObject>(spinpattern, "SpinPattern", "PHObject");
  if(!spinpattern || !spinNode)
  {
    cerr << "Failure to create spin node" << endl;
    exit(1);
  }
  runNode->addNode(spinNode);

  // EMCal recalibration class
  emcrecalib = new EmcLocalRecalibrator();
  if(!emcrecalib)
  {
    cerr << "No emcrecalib" << endl;
    exit(1);
  }

  emcrecalib_sasha = new EmcLocalRecalibratorSasha();
  if(!emcrecalib_sasha)
  {
    cerr << "No emcrecalib_sasha" << endl;
    exit(1);
  }

  /* Initialize warnmap checker */
  emcwarnmap = new EMCWarnmapChecker();
  if(!emcwarnmap)
  {
    cerr << "No emcwarnmap" << endl;
    exit(1);
  }

  return EVENT_OK;
}

int PhotonNode::InitRun(PHCompositeNode *topNode)
{
  // get run number
  RunHeader *runheader = findNode::getClass<RunHeader>(topNode, "RunHeader");
  if(!runheader)
  {
    cerr << "No runheader" << endl;
    return ABORTRUN;
  }
  runnumber = runheader->get_RunNumber();

  SpinDBOutput spin_out;
  SpinDBContent spin_cont;

  // initialize object to access spin DB
  spin_out.Initialize();
  spin_out.SetUserName("phnxrc");
  spin_out.SetTableName("spin");

  // retrieve entry from Spin DB and get fill number
  int qa_level = spin_out.GetDefaultQA(runnumber);
  spin_out.StoreDBContent(runnumber, runnumber, qa_level);
  spin_out.GetDBContentStore(spin_cont, runnumber);
  fillnumber = spin_cont.GetFillNumber();

  // load EMCal recalibrations for run and fill
  emcrecalib->ReadEnergyCorrection( runnumber );
  emcrecalib->ReadTofCorrection( fillnumber );

  // update spinpattern
  if( spin_out.CheckRunRow(runnumber,qa_level) == 1 &&
      spin_cont.GetRunNumber() == runnumber )
    UpdateSpinPattern(spin_cont);
  else
    spinpattern->Reset();

  return EVENT_OK;
}

int PhotonNode::process_event(PHCompositeNode *topNode)
{
  PHGlobal *data_global = findNode::getClass<PHGlobal>(topNode, "PHGlobal");
  TrigLvl1 *data_triggerlvl1 = findNode::getClass<TrigLvl1>(topNode, "TrigLvl1");
  ErtOut *data_ert = findNode::getClass<ErtOut>(topNode, "ErtOut");
  emcClusterContainer *data_emccontainer_raw = findNode::getClass<emcClusterContainer>(topNode, "emcClusterContainer");
  PHCentralTrack *data_tracks = findNode::getClass<PHCentralTrack>(topNode, "PHCentralTrack");

  if(!data_global)
  {
    cerr << "No gbl" << endl;
    return DISCARDEVENT;
  }
  if(!data_triggerlvl1)
  {
    cerr << "No trg" << endl;
    return DISCARDEVENT;
  }
  if(!data_ert)
  {
    cerr << "No ert" << endl;
    return DISCARDEVENT;
  }
  if(!data_emccontainer_raw)
  {
    cerr << "No emcont" << endl;
    return DISCARDEVENT;
  }
  if(!data_tracks)
  {
    cerr << "\nNo tracker data" << endl;
    return DISCARDEVENT;
  }

  // get bbc info
  float bbc_z = data_global->getBbcZVertex();
  float bbc_t0 = data_global->getBbcTimeZero();

  // get crossing number
  //int crossing = data_triggerlvl1->get_lvl1_clock_cross();

  // get ert triger info
  const unsigned bit_ppg = 0x70000000;
  unsigned lvl1_live = data_triggerlvl1->get_lvl1_triglive();
  unsigned lvl1_scaled = data_triggerlvl1->get_lvl1_trigscaled();
  if( (lvl1_live & bit_ppg) || (lvl1_scaled & bit_ppg) ) return DISCARDEVENT;

  // fill photon node
  photoncont->set_bbc_z(bbc_z);
  photoncont->set_bbc_t0(bbc_t0);
  //photoncont->set_crossing(crossing);
  photoncont->set_trigger(lvl1_live, lvl1_scaled);

  if( datatype == ERT && !photoncont->get_ert_b_scaled() && fabs(bbc_z) > 30. )
    return DISCARDEVENT;
  else if( datatype == MB && fabs(bbc_z) > 30. )
    return EVENT_OK;

  // Run local recalibration of EMCal cluster data
  emcClusterContainer *data_emccontainer = data_emccontainer_raw->clone();
  //emcrecalib->ApplyClusterCorrection( data_emccontainer );
  emcrecalib_sasha->ApplyClusterCorrection( runnumber, data_emccontainer );

  int nemccluster = data_emccontainer->size();
  for(int iclus=0; iclus<nemccluster; iclus++)
  {
    emcClusterContent *emccluster_raw = data_emccontainer_raw->getCluster(iclus);
    emcClusterContent *emccluster = data_emccontainer->getCluster(iclus);
    if( emccluster->ecore() > 0.3 &&
        ( emcwarnmap->GetStatusNils(emccluster) <= 10 ||
          emcwarnmap->GetStatusSasha(emccluster) == 0 )
      )
    {
      int arm = emccluster->arm();
      int rawsector = emccluster->sector();
      int sector = anatools::CorrectClusterSector(arm, rawsector);
      int iypos = emccluster->iypos();
      int izpos = emccluster->izpos();

      int towerid = anatools::TowerID(sector, iypos, izpos);
      float x = emccluster->x();
      float y = emccluster->y();
      float z = emccluster->z();
      float ecore_raw = emccluster_raw->ecore();
      float tofcorr_raw = emccluster_raw->tofcorr();
      float ecore = emccluster->ecore();
      //float theta_cv = anatools::GetTheta_CV(emccluster);
      //float cone_energy = GetTrackConeEnergy(data_tracks, emccluster, 0.5);

      Photon *photon = new Photon(towerid, x, y, z, ecore_raw, ecore, tofcorr_raw);
      //PhotonERT *photonERT = new PhotonERT(towerid, x, y, z, ecore_raw, theta_cv, cone_energy);
      photon->set_trig(data_ert, emccluster);
      if( emccluster->prob_photon() > 0.02 )
        photon->set_prob();
      if(photon)
      {
        photoncont->AddPhoton(*photon);
        delete photon;
      }
    }
  }

  // clean up
  delete data_emccontainer;

  return EVENT_OK;
}

int PhotonNode::End(PHCompositeNode *topNode)
{
  delete emcrecalib;
  delete emcrecalib_sasha;
  delete emcwarnmap;

  return EVENT_OK;
}

void PhotonNode::SelectMB()
{
  datatype = MB;
  return;
}

void PhotonNode::SelectERT()
{
  datatype = ERT;
  return;
}

bool PhotonNode::TestPhoton(const emcClusterContent *emccluster, float bbc_t0)
{
  if( emccluster->ecore() > 0.3 &&
      fabs( emccluster->tofcorr() - bbc_t0 ) < 10. &&
      emccluster->prob_photon() > 0.02 )
    return true;
  else
    return false;
}

bool PhotonNode::DispCut(const emcClusterContent *emccluster)
{
  int arm = emccluster->arm();
  int rawsector = emccluster->sector();
  int sector = anatools::CorrectClusterSector(arm, rawsector);

  if( sector < 6 )
  {
    if( emccluster->prob_photon() > 0.02 )
      return true;
    else
      return false;
  }

  const double p1 = 0.270;
  const double p2 = -0.0145;
  const double p3 = 0.00218;

  float theta = emccluster->theta();
  float corrdispy = emccluster->corrdispy();
  float corrdispz = emccluster->corrdispz();

  double dispCut = p1 + p2*theta + p3*theta*theta;
  double dispMax = corrdispy > corrdispz ? corrdispy : corrdispz;

  if( dispMax < dispCut )
    return true;
  else
    return false;
}

float PhotonNode::GetTrackConeEnergy(const PHCentralTrack *tracks, const emcClusterContent *cluster, double cone_angle)
{ 
  // get cluster angles in radians
  float theta_emc = cluster->theta();
  float phi_emc = cluster->phi();

  // cone energy
  float cone_energy = 0.;

  int npart = tracks->get_npart();
  for(int i=0; i<npart; i++)
  {
    float px = tracks->get_mompx(i);
    float py = tracks->get_mompy(i);
    float pz = tracks->get_mompz(i);
    float mom = tracks->get_mom(i);

    // get track angles in radians
    if(px == 0.) continue;
    float theta = mom > 0. ? acos(pz/mom) : 1.;
    float phi = px > 0. ? atan(py/px) : PI+atan(py/px);

    // add track energy from clusters within cone range
    float dtheta = theta - theta_emc;
    float dphi = phi - phi_emc;
    if( sqrt(dtheta*dtheta + dphi*dphi) < cone_angle )
      cone_energy += mom;
  }

  return cone_energy;
}

void PhotonNode::UpdateSpinPattern(SpinDBContent &spin_cont)
{
  // get spin info
  int runnumber = spin_cont.GetRunNumber();
  int qa_level = spin_cont.GetQALevel();
  int fillnumber = spin_cont.GetFillNumber();
  int badrunqa = spin_cont.GetBadRunFlag();
  int crossing_shift = spin_cont.GetCrossingShift();

  float pb, pbstat, pbsyst;
  float py, pystat, pysyst;

  spin_cont.GetPolarizationBlue(1, pb, pbstat, pbsyst);
  spin_cont.GetPolarizationYellow(1, py, pystat, pysyst);

  int badbunch[120];
  int spinpattern_blue[120];
  int spinpattern_yellow[120];

  long long bbc_narrow[120];
  long long bbc_wide[120];
  long long zdc_narrow[120];
  long long zdc_wide[120];

  for(int i=0; i<120; i++)
  {
    badbunch[i] = spin_cont.GetBadBunchFlag(i);
    spinpattern_blue[i] = spin_cont.GetSpinPatternBlue(i);
    spinpattern_yellow[i] = spin_cont.GetSpinPatternYellow(i);

    bbc_narrow[i] = spin_cont.GetScalerBbcVertexCut(i);
    bbc_wide[i] = spin_cont.GetScalerBbcNoCut(i);
    zdc_narrow[i] = spin_cont.GetScalerZdcNarrow(i);
    zdc_wide[i] = spin_cont.GetScalerZdcWide(i);
  }

  // update spinpattern
  int current_qa_level = spinpattern->get_qa_level();
  if(current_qa_level < qa_level)
  {
    spinpattern->Reset();

    spinpattern->set_runnumber(runnumber);
    spinpattern->set_qa_level(qa_level);
    spinpattern->set_fillnumber(fillnumber);
    spinpattern->set_badrunqa(badrunqa);
    spinpattern->set_crossing_shift(crossing_shift);

    spinpattern->set_pb(pb);
    spinpattern->set_pbstat(pbstat);
    spinpattern->set_pbsyst(pbsyst);
    spinpattern->set_py(py);
    spinpattern->set_pystat(pystat);
    spinpattern->set_pysyst(pysyst);

    for(int i=0; i<120; i++)
    {
      spinpattern->set_badbunch(i, badbunch[i]);
      spinpattern->set_spinpattern_blue(i, spinpattern_blue[i]);
      spinpattern->set_spinpattern_yellow(i, spinpattern_yellow[i]);

      /* For Run13, GL1p swap narrow and wide.
       * So I swap narrow and wide to correct the error. */
      spinpattern->set_bbc_narrow(i, bbc_wide[i]);
      spinpattern->set_bbc_wide(i, bbc_narrow[i]);
      spinpattern->set_zdc_narrow(i, zdc_wide[i]);
      spinpattern->set_zdc_wide(i, zdc_narrow[i]);
    }
  }

  return;
}
