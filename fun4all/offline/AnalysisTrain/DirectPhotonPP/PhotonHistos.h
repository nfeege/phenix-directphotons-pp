#ifndef __PHOTONHISTOS_H__
#define __PHOTONHISTOS_H__

#include <SubsysReco.h>

class EmcLocalRecalibrator;
class EmcLocalRecalibratorSasha;

class SpinPattern;
class SpinDBContent;

class PHGlobal;
class PHCentralTrack;
class emcClusterContainer;
class emcClusterContent;
class emcTowerContainer;
class TrigLvl1;
class ErtOut;
class PHCompositeNode;

class Fun4AllHistoManager;
class TH1;
class TH2;
class TH3;

class PhotonHistos: public SubsysReco
{
  public:
    PhotonHistos(const std::string &name = "PhotonHistos", const char *filename = "PhotonHistos.root");
    virtual ~PhotonHistos();

    int Init(PHCompositeNode *topNode);
    int InitRun(PHCompositeNode *topNode);
    int process_event(PHCompositeNode *topNode);
    int End(PHCompositeNode *topNode);

    /* Select to run on MinBias or ERT sample */
    void SelectMB();
    void SelectERT();

  protected:
    /* Event counts */
    int FillEventCounts(const PHGlobal *data_global, const TrigLvl1 *data_triggerlvl1);

    /* ToF and energy calibration */
    int FillClusterTofSpectrum(const emcClusterContainer *data_emccontainer, const PHGlobal *data_global, const std::string &qualii = "");
    int FillPi0InvariantMass(const emcClusterContainer *data_emccontainer, const PHGlobal *data_global, const std::string &quali = "");

    /* BBC and ERT trigger efficiency */
    int FillBBCEfficiency(const emcClusterContainer *data_emccontainer, const TrigLvl1 *data_triggerlvl1);
    int FillERTEfficiency(const emcClusterContainer *data_emccontainer, const PHGlobal *data_global,
        const TrigLvl1 *data_triggerlvl1, const ErtOut *data_ert, const int evtype);

    /* Check tower energy distribution*/
    int FillTowerEnergy(const emcClusterContainer *data_emccontainer, const emcTowerContainer *data_emctwrcontainer,
        const PHGlobal *data_global, const TrigLvl1 *data_triggerlvl1);

    /* Count pi0 yield */
    int FillPi0Spectrum(const emcClusterContainer *data_emccontainer,
        const PHGlobal *data_global, const TrigLvl1 *data_triggerlvl1, const ErtOut *data_ert, const int evtype);

    /* Count direct photon yield */
    int FillPhotonSpectrum(const emcClusterContainer *data_emccontainer, const PHCentralTrack *data_tracks,
        const PHGlobal *data_global, const TrigLvl1 *data_triggerlvl1, const ErtOut *data_ert, const int evtype);

    /* Create histograms */
    void BookHistograms();

    /* Sum energy in cone around the reference particle
     * for isolated photon and isolated pair */
    double SumEEmcal(const emcClusterContent *cluster, const emcClusterContainer *cluscont);
    void SumEEmcal(const emcClusterContent *cluster1, const emcClusterContent *cluster2,
        const emcClusterContainer *cluscont, double &econe1, double &econe2);
    double SumPTrack(const emcClusterContent *cluster, const PHCentralTrack *tracks);

    /* Check event type, photon cuts and tower status */
    bool IsEventType(const int evtype, const TrigLvl1 *data_triggerlvl1);
    bool TestPhoton(const emcClusterContent *cluster, double bbc_t0);
    bool IsGoodTower(const emcClusterContent *cluster);
    bool InFiducial(const emcClusterContent *cluster);

    /* Get warnmap status and spin pattern */
    int GetPattern(int crossing);

    /* Setup energy and ToF calibrator and read warnmap */
    void EMCRecalibSetup();
    void ReadSashaWarnmap(const std::string &filename);

    /* Update spin pattern information and store in class */
    void UpdateSpinPattern(SpinDBContent &spin_cont);

    enum DataType {MB, ERT};
    DataType datatype;

    /* Number of histogram array */
    static const int nh_calib = 2*8;
    static const int nh_bbc = 2*8;
    static const int nh_ert = 2*6*8;
    static const int nh_etwr = 16*8*2*8;
    static const int nh_eta_phi = 2*3;
    static const int nh_pion = 2*3*4*3*8;
    static const int nh_1photon = 2*3*4*2*3*8;
    static const int nh_2photon = 2*3*4*2*2*3*8;

    /* Tower status for warnmap */
    int tower_status[8][48][96];

    /* EMCal recalibrator and spin information*/
    EmcLocalRecalibrator *emcrecalib;
    EmcLocalRecalibratorSasha *emcrecalib_sasha;
    SpinPattern *spinpattern;

    int runnumber;
    int fillnumber;

    /* Output histograms */
    std::string outFile;
    Fun4AllHistoManager *hm;
    TH1 *h_events;
    TH2 *h2_tof[nh_calib];
    TH2 *h2_minv[nh_calib];
    TH1 *h_bbc[nh_bbc];
    TH2 *h2_bbc_pion[nh_bbc];
    TH1 *h_ert[nh_ert];
    TH2 *h2_ert_pion[nh_ert];
    TH3 *h3_etwr[nh_etwr];
    TH2 *h2_eta_phi[nh_eta_phi];
    TH2 *h2_pion[nh_pion];
    TH1 *h_1photon[nh_1photon];
    TH2 *h2_2photon[nh_2photon];
};

#endif /* __PHOTONHISTOS_H__ */
