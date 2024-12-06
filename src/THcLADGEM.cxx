#include "THcLADGEM.h"
#include "THaEvData.h"
#include "THaGlobals.h"
#include "THcGlobals.h"
#include "THaCutList.h"
#include "THcParmList.h"
#include "VarDef.h"
#include "VarType.h"
#include "THaApparatus.h"
#include "THcHallCSpectrometer.h"
#include "THaTrack.h"

using namespace std;

//____________________________________________________________
THcLADGEM::THcLADGEM( const char* name, const char* description,
		      THaApparatus* apparatus) :
  THaNonTrackingDetector(name, description, apparatus)
{
  // constructor

  fModules.clear();
  fModulesInitialized = false;

  fNModules = 0;
  fNLayers = 0;
  fNhits = 0;

  fGEMTracks = new TClonesArray( "THcLADGEMTrack", 20 );

}

//____________________________________________________________
THcLADGEM::~THcLADGEM()
{
  // Destructor
  for(auto module : fModules ) {
    delete module;
  }
  fModules.clear();

  delete fGEMTracks;

}

//____________________________________________________________
void THcLADGEM::Clear( Option_t* opt)
{

  //  cout << "THcLADGEM::Clear" << endl;
  fNhits = 0;
  fClusOutData.clear();
  fNClusters = 0;
  fNTracks = 0;

  for(auto& module: fModules )
    module->Clear();

  for(int i=0; i<fNLayers; i++)
    f2DHits[i].clear();
}


//____________________________________________________________
THaAnalysisObject::EStatus THcLADGEM::Init( const TDatime& date)
{

  //  cout << "THcLADGEM::Init" << endl;

  EStatus status;
  if( (status = THaNonTrackingDetector::Init( date )) )
    return fStatus = status;

  fPresentP = nullptr;
  THaVar* vpresent = gHaVars->Find(Form("%s.present", GetApparatus()->GetName()));
  if(vpresent) {
    fPresentP = (Bool_t*) vpresent->GetValuePointer();
  }

  // Call Hall C style DetectorMap 
  // GEM channel maps to be defined in MAPS/LAD/detector.map 
  // e.g. gHcDetectorMap->FillMap(fDetMap, EngineID)
  // InitHitList(fDetMap, "THcLADGEMHit", fDetMap->GetTotNumChan()+1,0, RefTimeCut)

  // Init Subdetectors
  for(auto& module: fModules ) {
    status = module->Init(date);
    if( status != kOK )
      return fStatus = status;
  }

  return fStatus = kOK;

}

//____________________________________________________________
Int_t THcLADGEM::DefineVariables( EMode mode )
{
  if( mode == kDefine && fIsSetup ) return kOK;
  fIsSetup = ( mode == kDefine );

  // Cluster variables
  RVarDef vars_clus[] = {
    {"clust.layer",   "GEM Layer",                       "fClusOutData.layer"},
    {"clust.module",  "GEM Module ID",                   "fClusOutData.imodule"},
    {"clust.axis",    "U/V axis",                        "fClusOutData.axis"},
    {"clust.mpd",     "MPD ID",                          "fClusOutData.mpdid"},
    {"clust.nstrip",  "Number of strips in cluster",     "fClusOutData.nstrip"},
    {"clust.maxstrip","Max strip of the given cluster",  "fClusOutData.maxstrip"},
    {"clust.index",   "Cluster index",                   "fClusOutData.clindex"},
    {"clust.adc",     "Cluster ADC mean",                "fClusOutData.adc"},
    {"clust.time",    "Cluster Time mean",               "fClusOutData.time"},
    {"clust.pos",     "Weighted cluster position",       "fClusOutData.pos"},
    {"clust.maxpos",  "Max strip pos of the cluster",    "fClusOutData.mpos"},
    { 0 }
  };

  DefineVarsFromList( vars_clus, mode );

  // Track/Space point variables
  RVarDef vars_trk[] = {
    {"trk.ntracks",    "Number of GEM track candidates", "fNTracks"},
    {"trk.id",         "GEM Track ID",                   "fGEMTracks.THcLADGEMTrack.GetTrackID()"},
    {"trk.x1",         "Space point1 X",                 "fGEMTracks.THcLADGEMTrack.GetX1()"},
    {"trk.y1",         "Space point1 Y",                 "fGEMTracks.THcLADGEMTrack.GetY1()"},
    {"trk.z1",         "Space point1 Z",                 "fGEMTracks.THcLADGEMTrack.GetZ1()"},
    {"trk.x2",         "Space point2 X",                 "fGEMTracks.THcLADGEMTrack.GetX2()"},
    {"trk.y2",         "Space point2 Y",                 "fGEMTracks.THcLADGEMTrack.GetY2()"},
    {"trk.z2",         "Space point2 Z",                 "fGEMTracks.THcLADGEMTrack.GetZ2()"},
    {"trk.t",          "Avg time",                       "fGEMTracks.THcLADGEMTrack.GetT()"},
    {"trk.dt",         "Time difference between two sp", "fGEMTracks.THcLADGEMTrack.GetdT()"},
    {"trk.d0",         "Track dist from vertex",         "fGEMTracks.THcLADGEMTrack.GetD0()"},
    {"trk.projz",      "Projected z-vertex",             "fGEMTracks.THcLADGEMTrack.GetProjVz()"},
    { 0 }
  };
  DefineVarsFromList( vars_trk, mode );

  return kOK;
}

//____________________________________________________________
Int_t THcLADGEM::ReadDatabase( const TDatime& date )
{
  cout << "THcLADGEM::ReadDatabase" << endl;
  // Called by THaDetectorBase::Init()
  // Read parameters from THcParmList

  char prefix[2];
  
  prefix[0] = std::tolower(GetApparatus()->GetName()[0]);
  prefix[1] = '\0';

  // initial values
  fGEMAngle = 123.5;

  DBRequest list[] = {
    {"gem_num_modules", &fNModules, kInt}, // should be defined in DB file
    {"gem_num_layers",  &fNLayers,  kInt},
    {"gem_angle",       &fGEMAngle, kDouble, 0, 1},
    {0}
  };
  gHcParms->LoadParmValues((DBRequest*)&list, prefix);

  // Define GEM Modules
  for(int imod = 0; imod < fNModules; imod++) {
    THcLADGEMModule* new_module = new THcLADGEMModule(Form("m%d",imod), Form("m%d",imod), imod, this);
    fModules.push_back(new_module);
  }

  f2DHits.resize(fNLayers);

  return kOK;
}

//____________________________________________________________
Int_t THcLADGEM::Decode( const THaEvData& evdata )
{

  // Decode MPD data
  for(auto& module: fModules) {
    module->Decode(evdata);
  }

  /*
  Bool_t present = kTRUE;
  if(fPresentP) {
    present = *fPresentP;
  }
  Int_t nhits = DecodeToHitList(evdata, !present);
  */

  return 0;
}

//____________________________________________________________
Int_t THcLADGEM::CoarseProcess( TClonesArray& tracks )
{
  //  cout << "THcLADGEM::CoarseProcess" << endl;

  for( auto module : fModules ) {
    module->CoarseProcess(tracks); // X/Y clustering, form 2D hits

    // Cluster output handling
    for(int i=0; i<2; i++) {
      // 0:U(X) cluster 1:V(Y) cluster
      for( auto& cluster : module->GetClusters(i) ) {
	fClusOutData.layer.push_back(cluster.GetLayer());
	fClusOutData.imodule.push_back(module->GetModuleID());
	fClusOutData.mpdid.push_back(cluster.GetMPD());
	fClusOutData.axis.push_back(cluster.GetAxis());
	fClusOutData.nstrip.push_back(cluster.GetNStrips());
	fClusOutData.maxstrip.push_back(cluster.GetStripMax());
	fClusOutData.clindex.push_back(fNClusters);
	fClusOutData.adc.push_back(cluster.GetADCsum());
	fClusOutData.time.push_back(cluster.GetTime());
	fClusOutData.pos.push_back(cluster.GetPos());
	fClusOutData.mpos.push_back(cluster.GetPosMax());
	fNClusters++;
      }
    }
  }

  // Loop over all 2D hits and find track candidates
  // Using only two layers to define a track candidate
  // LAD has only two layers...If more than two layers, use the outer two

  double angle = fGEMAngle * TMath::DegToRad();

  for(auto& gemhit1 : f2DHits[fNLayers-2] ) {
    for(auto& gemhit2 : f2DHits[fNLayers-1] ) {

      if( !gemhit1.IsGoodHit || !gemhit2.IsGoodHit ) continue;

      double tdiff = gemhit1.TimeMean - gemhit2.TimeMean; // time difference (TimeMean1 - TimeMean2)
      double tmean = (gemhit1.TimeMean + gemhit2.TimeMean)*0.5; // average time

      TVector3 v_hit1(gemhit1.posX, gemhit1.posY, gemhit1.posZ);
      TVector3 v_hit2(gemhit2.posX, gemhit2.posY, gemhit2.posZ);

      // THaTrack* this_track = nullptr;
      // this_track = AddTrack(tracks, 0.0, 0.0, 0.0, 0.0); // AddTrack is func of THaTrackingDetector
      // FIXME: theta, phi might be defined differently in TVector3 and THaTrack
      // this_track->SetD(v_hit1.X(), v_hit1.Y(), v_hit1.Theta(), v_hit1.Phi() ); // DCS x, y , theta, phi

      // Rotate along y-axis
      v_hit1.RotateY(angle);
      v_hit2.RotateY(angle);
      
      // Set New position
      gemhit1.posX = v_hit1[0];
      gemhit1.posY = v_hit1[1];
      gemhit1.posZ = v_hit1[2];
      gemhit2.posX = v_hit2[0];
      gemhit2.posY = v_hit2[1];
      gemhit2.posZ = v_hit2[2];

      // d0: DCAr from the primary vertex, assume (0,0,0) for now
      // we want to get the primary vtx from HMS eventually whenever available
      TVector3 v_prim(0., 0., 0.);
      double numer = ((v_prim - v_hit1).Cross((v_prim - v_hit2))).Mag();
      double denom = (v_hit2 - v_hit1).Mag();
      // here we can put a range/fiducial cut on d0 taking into account the target size
      double d0 = numer/denom;

      // DCAz, projected z-vertex
      // First check if it intercepts with z-axis?
      double vpz;
      double t1 = -v_hit1[0]/(v_hit2[0]-v_hit1[0]);
      double t2 = -v_hit1[1]/(v_hit2[1]-v_hit1[1]);
      if( abs(t1-t2) > 1.e-3 )
	vpz = -999999.;
      else
	vpz = -v_hit1[0]*(v_hit2[2]-v_hit1[2])/(v_hit2[0]-v_hit1[0]) + v_hit1[2];

      // Add track object
      THcLADGEMTrack *theGEMTrack = new ( (*fGEMTracks)[fNTracks] ) THcLADGEMTrack(fNLayers);
      theGEMTrack->SetTrackID(fNTracks); 
      theGEMTrack->AddSpacePoint(gemhit1);
      theGEMTrack->AddSpacePoint(gemhit2);
      theGEMTrack->SetTime(tmean, tdiff);
      theGEMTrack->SetD0(d0);
      theGEMTrack->SetZVertex(vpz);

      fNTracks++;
    }
  }
  return 0;
}
//____________________________________________________________
void THcLADGEM::RotateToLab(Double_t angle, TVector3& vect)
{

  // Initially defined in the local detector coordinate system (this is not transport coord)
  // Rotate along y-axis
  Double_t x_lab = vect.X()*cos(angle) + vect.Z()*sin(angle);
  Double_t y_lab = vect.Y();
  Double_t z_lab = -vect.X()*sin(angle) + vect.Z()*cos(angle);

  // Redefine the vector
  vect.SetXYZ(x_lab, y_lab, z_lab);
}

//____________________________________________________________
void THcLADGEM::Add2DHits(Int_t ilayer, Double_t x, Double_t y, Double_t z,
			  Double_t t, Double_t dt, Double_t tc,
			  Bool_t goodhit, Double_t adc, Double_t adcasy)
{
  // FIXME:Add flag for filtering good hits?

  f2DHits[ilayer].push_back( {ilayer, x, y, z, t, dt, tc, goodhit, adc, adcasy} );
}

//____________________________________________________________
Int_t THcLADGEM::FineProcess( TClonesArray& tracks )
{
  //  cout << "THcLADGEM::FineProcess" << endl;
  return 0;
}

//____________________________________________________________

ClassImp(THcLADGEM)

