#ifndef MPDModule_h
#define MPDModule_h

#include "VmeModule.h"

namespace Decoder {

  class MPDModule : public VmeModule {

  public:
    MPDModule() = default;
    MPDModule(Int_t crate, Int_t slot);
    virtual ~MPDModule();

    using VmeModule::GetData;
    using VmeModule::LoadSlot;

    virtual void   Init();
    virtual void   Clear(const Option_t *opt="");    
    virtual Int_t  Decode(const UInt_t *p);
    virtual Int_t  LoadSlot( THaSlotData* sldat, const UInt_t *evbuffer, UInt_t pos, UInt_t len);
    virtual UInt_t GetData( UInt_t adc, UInt_t sample, UInt_t chan) const;
    
    virtual UInt_t GetTriggerTimeL() const;
    virtual UInt_t GetTriggerTimeH() const;
    virtual UInt_t GetTriggerTime() const;
    virtual UInt_t GetCommonModeFlag( UInt_t fiber ) const;
    virtual UInt_t GetBuildAllSamples( UInt_t fiber ) const; 
    virtual UInt_t GetCommonModeOR( UInt_t fiber ) const;
    virtual UInt_t GetData( ) const;
    // virtual std::vector<UInt_t> GetAPVSamplesData( UInt_t chan, UInt_t ievent ) const;

  private:
    struct mpd_data_structure {
      UInt_t slotid_hdr, block_num, block_size;       // Header type 0
      UInt_t slotid_trl, num_words;                   // Trailer type 1
      UInt_t trig_num;                                // Event Header type 2
      UInt_t trig_time_l, trig_time_h, trig_time;     // Trigger Time type 3, 2 words
      UInt_t fiber, mpd_id;                           // MPD Frame, type 5
      UInt_t enable_cm, build_all_samples, cm_or;     // FLAGS
      UInt_t apv_id, apv_ch_num;
      UInt_t timestamp_fine;                          // MPD timestamp, type 12
      UInt_t timestamp_coarse0, timestamp_coarse1;
      UInt_t timestamp_coarse;
      UInt_t event_count;
      //      UInt_t cm_t1, cm_t0, cm_t3, cm_t2, cm_t5, cm_t4;
      void clear() { memset(this, 0, sizeof(ssp_data_structure)); }
    } mpd_data;

    struct apv_sample_data {
      std::vector<UInt_t> fiber;
      std::vector<UInt_t> mpd_id;
      std::vector<UInt_t> apv_id;
      std::vector<UInt_t> samples;
      std::vector<UInt_t> cm_corrections;
      void clear() {
	samples.clear();
      }
    };
    std::vector<apv_sample_data> fAPVSamplesData;
    
    Bool_t block_header_found, block_trailer_found, event_header_found;
    Bool_t trig_time_found, mpd_data_found, mpd_evinfo_found, mpd_debug_found;

    void DecodeBlockHeader( UInt_t pdat, UInt_t data_type_id );
    void DecodeBlockTrailer( UInt_t pdat, UInt_t data_type_id );
    void DecodeEventHeader( UInt_t pdat, UInt_t data_type_id );
    void DecodeTriggerTime( UInt_t pdat, UInt_t data_type_id );
    void DecodeMPDDataFrame( UInt_t pdat, UInt_t data_type_id );
    void DecodeMPDEventInfo( UInt_t pdat, UInt_t data_type_id );
    void DecodeMPDDebugHeader( UInt_t pdat, UInt_t data_type_id );

    static TypeIter_t fgThisType;

    ClassDef(MPDModule,0)
 };
}

#endif
