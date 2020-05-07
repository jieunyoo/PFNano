#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/stream/EDProducer.h"

#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"

#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/Utilities/interface/StreamID.h"

#include "DataFormats/VertexReco/interface/VertexFwd.h"

#include "DataFormats/PatCandidates/interface/Jet.h"
#include "DataFormats/PatCandidates/interface/PackedCandidate.h"

#include "DataFormats/Candidate/interface/CandidateFwd.h"

#include "RecoBTag/FeatureTools/interface/TrackInfoBuilder.h"
#include "TrackingTools/Records/interface/TransientTrackRecord.h"

// SV
#include "DataFormats/BTauReco/interface/TrackIPTagInfo.h"
#include "DataFormats/BTauReco/interface/SecondaryVertexTagInfo.h"
#include "RecoBTag/FeatureTools/interface/deep_helpers.h"
#include "DataFormats/Candidate/interface/VertexCompositePtrCandidate.h"
using namespace btagbtvdeep;

#include "CommonTools/Utils/interface/StringCutObjectSelector.h"
#include "DataFormats/NanoAOD/interface/FlatTable.h"

template<typename T>
class JetConstituentTableProducer : public edm::stream::EDProducer<> {
public:
  explicit JetConstituentTableProducer(const edm::ParameterSet &);
  ~JetConstituentTableProducer() override;

  static void fillDescriptions(edm::ConfigurationDescriptions &descriptions);

private:
  void produce(edm::Event &, const edm::EventSetup &) override;

  typedef edm::Ptr<pat::PackedCandidate> CandidatePtr;
  typedef edm::View<pat::PackedCandidate> CandidateView;
  typedef reco::VertexCollection VertexCollection;
  typedef reco::VertexCompositePtrCandidateCollection SVCollection;

  const bool readBtag_;
  const std::string namePF_;
  const std::string nameSV_;
  const StringCutObjectSelector<pat::Jet> jetCut_;
  const double jet_radius_;

  edm::EDGetTokenT<edm::View<T>> jet_token_;
  edm::EDGetTokenT<VertexCollection> vtx_token_;
  edm::EDGetTokenT<reco::CandidateView> pfcand_token_;
  //edm::EDGetTokenT<CandidateView> pfcand_token_;
  edm::EDGetTokenT<SVCollection> sv_token_;

  edm::Handle<VertexCollection> vtxs_;
  //edm::Handle<CandidateView> pfcands_;
  edm::Handle<reco::CandidateView> pfcands_;
  edm::Handle<SVCollection> svs_;
  edm::ESHandle<TransientTrackBuilder> track_builder_;

  const reco::Vertex *pv_ = nullptr;


};

//
// constructors and destructor
//
template< typename T>
JetConstituentTableProducer<T>::JetConstituentTableProducer(const edm::ParameterSet &iConfig)
    : readBtag_(iConfig.getParameter<bool>("readBtag")),
      namePF_(iConfig.getParameter<std::string>("namePF")),
      nameSV_(iConfig.getParameter<std::string>("nameSV")),
      jetCut_(iConfig.getParameter<std::string>("cut"), true), 
      jet_radius_(iConfig.getParameter<double>("jet_radius")),
      
      jet_token_(consumes<edm::View<T>>(iConfig.getParameter<edm::InputTag>("jets"))),
      vtx_token_(consumes<VertexCollection>(iConfig.getParameter<edm::InputTag>("vertices"))),
      //pfcand_token_(consumes<CandidateView>(iConfig.getParameter<edm::InputTag>("pf_candidates"))),
      pfcand_token_(consumes<reco::CandidateView>(iConfig.getParameter<edm::InputTag>("pf_candidates"))),
      sv_token_(consumes<SVCollection>(iConfig.getParameter<edm::InputTag>("secondary_vertices"))){
  produces<nanoaod::FlatTable>(namePF_);
  produces<nanoaod::FlatTable>(nameSV_);
  produces<std::vector<reco::CandidatePtr>>();
}

template< typename T>
JetConstituentTableProducer<T>::~JetConstituentTableProducer() {}

template< typename T>
void JetConstituentTableProducer<T>::produce(edm::Event &iEvent, const edm::EventSetup &iSetup) {
  // elements in all these collections must have the same order!
  auto outCands = std::make_unique<std::vector<reco::CandidatePtr>>();
  auto outSVs = std::make_unique<std::vector<const reco::VertexCompositePtrCandidate *>> ();
  std::vector<int> jetIdx_pf, jetIdx_sv, candIdx, svIdx;
   // PF Cands
  std::vector<float> btagEtaRel, btagPtRatio, btagPParRatio, btagSip3dVal, btagSip3dSig, btagJetDistVal;
  // Secondary vertices
  std::vector<float> sv_mass, sv_pt, sv_ntracks, sv_chi2, sv_normchi2, sv_dxy, sv_dxysig, sv_d3d, sv_d3dsig, sv_costhetasvpv;
  std::vector<float> sv_ptrel, sv_phirel, sv_deltaR, sv_enratio;
  
  auto jets = iEvent.getHandle(jet_token_);

  iEvent.getByToken(vtx_token_, vtxs_);
  iEvent.getByToken(pfcand_token_, pfcands_);
  iEvent.getByToken(sv_token_, svs_);
  iSetup.get<TransientTrackRecord>().get("TransientTrackBuilder", track_builder_);
  

  for (unsigned i_jet = 0; i_jet < jets->size(); ++i_jet) {
    const auto &jet = jets->at(i_jet);
    math::XYZVector jet_dir = jet.momentum().Unit();
    GlobalVector jet_ref_track_dir(jet.px(), jet.py(), jet.pz());

    ////////  //////////////
    // Secondary Vertices
    std::vector<const reco::VertexCompositePtrCandidate *> jetSVs;
    for (const auto &sv : *svs_) {
      if (reco::deltaR2(sv, jet) < jet_radius_ * jet_radius_) {
        jetSVs.push_back(&sv);
      }
    }
    // sort by dxy significance
    std::sort(jetSVs.begin(),
              jetSVs.end(),
              [&](const reco::VertexCompositePtrCandidate *sva, const reco::VertexCompositePtrCandidate *svb) {
                return sv_vertex_comparator(*sva, *svb, *pv_);
              });

    for (const auto &sv : jetSVs) {
      outSVs->push_back(sv);
      jetIdx_sv.push_back(i_jet);
      // Jet independent
      sv_mass.push_back(sv->mass());
      sv_pt.push_back(sv->pt());
      
      sv_ntracks.push_back(sv->numberOfDaughters());
      sv_chi2.push_back(sv->vertexChi2());
      sv_normchi2.push_back(catch_infs_and_bound(sv->vertexChi2() / sv->vertexNdof(), 1000, -1000, 1000));
      const auto& dxy_meas = vertexDxy(*sv, *pv_);
      sv_dxy.push_back(dxy_meas.value());
      sv_dxysig.push_back(catch_infs_and_bound(dxy_meas.value() / dxy_meas.error(), 0, -1, 800));
      const auto& d3d_meas = vertexD3d(*sv, *pv_);
      sv_d3d.push_back(d3d_meas.value());
      sv_d3dsig.push_back(catch_infs_and_bound(d3d_meas.value() / d3d_meas.error(), 0, -1, 800));
      sv_costhetasvpv.push_back(vertexDdotP(*sv, *pv_));
      // Jet related
      sv_ptrel.push_back(sv->pt() / jet.pt());
      sv_phirel.push_back(reco::deltaPhi(*sv, jet));
      sv_deltaR.push_back(catch_infs_and_bound(std::fabs(reco::deltaR(*sv, jet_dir)) - 0.5, 0, -2, 0));
      sv_enratio.push_back(sv->energy() / jet.energy());
    }  
    
    std::vector<reco::CandidatePtr> const & daughters = jet.daughterPtrVector();

    for (const auto &cand : daughters) {
      auto candPtrs = pfcands_->ptrs();
      auto candInNewList = std::find( candPtrs.begin(), candPtrs.end(), cand );
      if ( candInNewList == candPtrs.end() ) {
	std::cout << "Cannot find candidate : " << cand.id() << ", " << cand.key() << ", pt = " << cand->pt() << std::endl;
	continue;
      }
      outCands->push_back(cand);
      jetIdx_pf.push_back(i_jet);
      candIdx.push_back( candInNewList - candPtrs.begin() );
      if (readBtag_ && !vtxs_->empty()) {
	if ( cand.isNull() ) continue;
	auto const *packedCand = dynamic_cast <pat::PackedCandidate const *>(cand.get());
	if ( packedCand == nullptr ) continue;
	if ( packedCand && packedCand->hasTrackDetails()){
	  btagbtvdeep::TrackInfoBuilder trkinfo(track_builder_);
	  trkinfo.buildTrackInfo(&(*packedCand), jet_dir, jet_ref_track_dir, vtxs_->at(0));
	  btagEtaRel.push_back(trkinfo.getTrackEtaRel());
	  btagPtRatio.push_back(trkinfo.getTrackPtRatio());
	  btagPParRatio.push_back(trkinfo.getTrackPParRatio());
	  btagSip3dVal.push_back(trkinfo.getTrackSip3dVal());
	  btagSip3dSig.push_back(trkinfo.getTrackSip3dSig());
	  btagJetDistVal.push_back(trkinfo.getTrackJetDistVal());
	} else {
          btagEtaRel.push_back(0);
          btagPtRatio.push_back(0);
          btagPParRatio.push_back(0);
          btagSip3dVal.push_back(0);
          btagSip3dSig.push_back(0);
          btagJetDistVal.push_back(0);
        }
      }
    }  // end jet loop
  }

  
  auto candTable = std::make_unique<nanoaod::FlatTable>(outCands->size(), namePF_, false);
  auto svTable = std::make_unique<nanoaod::FlatTable>(outSVs->size(), nameSV_, false);
  // PF Cand table
  candTable->addColumn<int>("jetIdx", jetIdx_pf, "Index of the parent jet", nanoaod::FlatTable::IntColumn);
  candTable->addColumn<int>("candIdx", candIdx, "Index in the candidate list", nanoaod::FlatTable::IntColumn);

  svTable->addColumn<int>("jetIdx", jetIdx_sv, "Index of the parent jet", nanoaod::FlatTable::IntColumn);
  candTable->addColumn<int>("svIdx", svIdx, "Index in the candidate list", nanoaod::FlatTable::IntColumn);
  if (readBtag_) {
    // PF table
    candTable->addColumn<float>("btagEtaRel", btagEtaRel, "btagEtaRel", nanoaod::FlatTable::FloatColumn, 10);
    candTable->addColumn<float>("btagPtRatio", btagPtRatio, "btagPtRatio", nanoaod::FlatTable::FloatColumn, 10);
    candTable->addColumn<float>("btagPParRatio", btagPParRatio, "btagPParRatio", nanoaod::FlatTable::FloatColumn, 10);
    candTable->addColumn<float>("btagSip3dVal", btagSip3dVal, "btagSip3dVal", nanoaod::FlatTable::FloatColumn, 10);
    candTable->addColumn<float>("btagSip3dSig", btagSip3dSig, "btagSip3dSig", nanoaod::FlatTable::FloatColumn, 10);
    candTable->addColumn<float>("btagJetDistVal", btagJetDistVal, "btagJetDistVal", nanoaod::FlatTable::FloatColumn, 10);

    // SV table
    svTable->addColumn<float>("mass", sv_mass, "SV mass", nanoaod::FlatTable::FloatColumn, 10);
    svTable->addColumn<float>("pt", sv_pt, "SV pt", nanoaod::FlatTable::FloatColumn, 10);
    svTable->addColumn<float>("ntracks", sv_ntracks, "Number of trakcs associated to SV", nanoaod::FlatTable::FloatColumn, 10);
    svTable->addColumn<float>("chi2", sv_chi2, "chi2", nanoaod::FlatTable::FloatColumn, 10);
    svTable->addColumn<float>("normchi2", sv_normchi2, "chi2/ndof", nanoaod::FlatTable::FloatColumn, 10);
    svTable->addColumn<float>("dxy", sv_dxy, "", nanoaod::FlatTable::FloatColumn, 10);
    svTable->addColumn<float>("dxysig", sv_dxysig, "", nanoaod::FlatTable::FloatColumn, 10);
    svTable->addColumn<float>("d3d", sv_d3d, "", nanoaod::FlatTable::FloatColumn, 10);
    svTable->addColumn<float>("d3dsig", sv_d3dsig, "", nanoaod::FlatTable::FloatColumn, 10);
    svTable->addColumn<float>("costhetasvpv", sv_costhetasvpv, "", nanoaod::FlatTable::FloatColumn, 10);
    // Jet related
    svTable->addColumn<float>("phirel", sv_phirel, "DeltaPhi(sv, jet)", nanoaod::FlatTable::FloatColumn, 10);
    svTable->addColumn<float>("ptrel", sv_ptrel, "pT relative to parent jet", nanoaod::FlatTable::FloatColumn, 10);
    svTable->addColumn<float>("deltaR", sv_deltaR, "dR from parent jet", nanoaod::FlatTable::FloatColumn, 10);
    svTable->addColumn<float>("enration", sv_enratio, "energy relative to parent jet", nanoaod::FlatTable::FloatColumn, 10);
  }
  iEvent.put(std::move(candTable), namePF_);  
  iEvent.put(std::move(svTable), nameSV_);

  iEvent.put(std::move(outCands));
  
}

template< typename T>
void JetConstituentTableProducer<T>::fillDescriptions(edm::ConfigurationDescriptions &descriptions) {
  edm::ParameterSetDescription desc;
  desc.add<bool>("readBtag", true);
  desc.add<std::string>("namePF", "AK8PFCands");
  desc.add<std::string>("nameSV", "AK8PFCands");
  desc.add<std::string>("cut", "pt()>170");
  desc.add<double>("jet_radius", 0.4);
  desc.add<edm::InputTag>("jets", edm::InputTag("slimmedJetsAK8"));
  desc.add<edm::InputTag>("vertices", edm::InputTag("offlineSlimmedPrimaryVertices"));
  desc.add<edm::InputTag>("pf_candidates", edm::InputTag("packedPFCandidates"));
  desc.add<edm::InputTag>("secondary_vertices", edm::InputTag("slimmedSecondaryVertices"));
  descriptions.addWithDefaultLabel(desc);
}

typedef JetConstituentTableProducer<pat::Jet> PatJetConstituentTableProducer;
typedef JetConstituentTableProducer<reco::GenJet> GenJetConstituentTableProducer;

DEFINE_FWK_MODULE(PatJetConstituentTableProducer);
DEFINE_FWK_MODULE(GenJetConstituentTableProducer);
