/*
// SSP_MPD Data Format
// Based on the manual from B. Raydo (Nov. 19, 2021)

  Data type list:
  0 Block header
  1 Block trailer
  2 Event header
  3 Trigger time
  4 Reserved
  5 MPD Data Frame
  6 Reserved
  7 Reserved
  8 Reserved
  9 Reserved
  10 Reserved
  11 Reserved
  12 MPD event Info
  13 MPD debug header
  14 Data not valid (emptyu module)
  15 Filler word (non-data)

  Data words: type (bit 31) +  4-bit data for type tag
  ata type defining word: bit 31 = 1
  Data type continuation : bit 31 = 0
*/

#include "MPDModule.h"
#include "THaSlotData.h"

#include <iostream>

namespace Decoder {

  Module::TypeIter_r MPDModule::fgThisType =
    DoRegister(ModuleType("Decoder::MPDModule", 3651));

  //________________________________________________________________
  MPDModule::MPDModule( UInt_t crate, UInt_t slot )
    : VmeModule(crate, slot), mpd_data{},
      block_header_found(false), block_trailer_found(false),
      event_header_found(false), trig_time_found(false),
      mpd_data_found(false), mpd_evinfo_found(false), mpd_debug_found(false)
  {
    MPDModule::Init();
  }

  //________________________________________________________________
  MPDModule::~MPDModule()
  {
    // default destructor
  }

  //________________________________________________________________
  Int_t MPDModule::Decode( const Int_t *p )
  {
    UInt_t data_type_id = (*p >> 31) & 0x1; // data type defining, 1-bit
    if( data_type_id == 1)
      data_type_def = (*p >> 27) & 0xF; // 4-bit

    #ifdef WITH_DEBUG
    std::cout << "MPDModule::Decode "
	      << " data_type = " << data_type_def << endl;

    switch( data_type_def ){
    case 0: // Block header
      DecodeBlockHeader(*p, data_type_id);
      break;
    case 1: // Block trailer
      DecodeTrailer(*p, data_type_id);
      break;
    case 2: // Event header
      DecodeEventHeader(*p, data_type_id);
      break;
    case 3: // Trigger Time
      DecodeTriggerTime(*p, data_type_id);
      break;
    case 5: // MPD Data Frame
      DecodeMPDFrame(*p, data_type_id);
      break;
    case 12:// MPD event info
      DecodeMPDEventInfo(*p, data_type_id);
      break;
    case 13:// MPD Debug header
      DecodeMPDDebugHeader(*p, data_type_id);
      break;
    case 4: // reserved
    case 6: // reserved
    case 7: // reserved
    case 8: // reserved
    case 9: // reserved
    case 10: // reserved
    case 11: // reserved
    case 14: // not valid 
    case 15: // filler
      break;
    default:
      throw logic_error("MPDModule:: incorrect masking of data_type_def");
    }

    return block_trailer_found;
  }

  //________________________________________________________________
  void MPDModule::DecodeBlockHeader( UInt_t pdat, UInt_t data_type_id )
  {
    /*
      -------------------------------------------------
      Block Header (type 0)
      31   = 1
      30-27= 0
      26-22= SLOTID (Slot ID set by VME64x backplane)
      21-18= UNDEFINED
      17-8 = BLOCK_NUMBER (Event block number used to align blocks when building events)
      7-0  = BLOCK_SIZE (Number of events in block)
      -------------------------------------------------
    */
    if( data_type_id ) {
      block_header_found = true;
      mpd_data.slotid_hdr = (pdat >> 22) & 0x1F; // SLOTID, 5 bits (26-22)
      mpd_data.block_num = (pdat >> 8) & 0x3FF;  // BLOCK_NUMBER, 10 bits (17-8)
      mpd_data.block_size = (pdat >> 0) & 0xFF;  // BLOCK_SIZE, 8 bits (7-0)
    }
  }

  //________________________________________________________________
  void MPDModule::DecodeBlockTrailer( UInt_t pdat, UInt_t data_type_id )
  {
    /*
      -------------------------------------------------
      Block Trailer
      31   = 1
      30-27= 1
      26-22= SLOTID (Slot ID set by VME64x backplane)
      21-0 = NUM_WORDS (Total number of words in block of events)
      -------------------------------------------------
    */
    if( data_type_id ) {
      block_trailer_found = true;
      mpd_data.slotid_trl = (pdat >> 22) & 0x1F;   // Slot ID (set by VME64x backplane), 5 bits
      mpd_data.num_words = (pdat >> 0) & 0x3FFFFF; // Total number of words in block of events, 22 bits
    }
  }
  //________________________________________________________________
  void MPDModule::DecodeEventHeader( UInt_t pdat, UInt_t data_type_id )
  {
    /*
      -------------------------------------------------
      Event Header
      31   = 1
      30-27= 2
      26-0 = TRIGGER_NUMBER (27 bits), Accepted event/trigger number
      -------------------------------------------------
    */
    event_header_found = true;
    mpd_data.trig_num = (pdat >> 0) & 0x7FFFFFF;
  }

  //________________________________________________________________
  void MPDModule::DecodeTriggerTime( UInt_t pdat, UInt_t data_type_id )
  {
    /*
      -------------------------------------------------
      Trigger Time (2 words)
      time measured by 48-bit counter (250MHz system clock)
      31   = 1
      30-27= 3
      26-24= 0
      23-0 = TRIGGER_TIME_L (lower 24 bits of the trigger time)

      31   = 0 (Data continuation)
      30-24= 0
      23-0 = TRIGGER_TIME_H (upper 24 bits of the trigger time)
      -------------------------------------------------
    */

    if(data_type_id == 1)
      mpd_data.trig_time_l = (pdat >> 0) & 0xFFFFFF; // mask 24 bit
    else
      mpd_data.trig_time_h = (pdat >> 0) & 0xFFFFFF; // mask 24 bit      

    // form 48bit trigger time, shift time_h by 24bits
    mpd_data.trig_time = (mpd_data.trig_time_h << 24) | mpd_data.trig_time_l;
  }

  //________________________________________________________________
  void MPDModule::DecodeMPDDataFrame( UInt_t pdat, UInt_t data_type_id )
  {
    /*
      -------------------------------------------------
      MPD Data Frame (1+3*N words)
      31   = 1
      30-27= 5
      26-22= FLAGS 5bits (5: ENABLE_CM, 4: BUILD_ALL_SAMPLES, 3:CM_OR)

      * ENABLE_CM: 1: online common-mode subtraction enabled 0: disabled
      * BUILD_ALL_SAMPLES: 1: zero-suppression disabled 0: zero-suppression enabled
      * CM_OR: 1: cm and zero-suppression diabled, force ENABLE_CM=0, BUILD_ALL_SAMPLES=1
             0: cm was commmputed successfully 

      21-15= FIBER (SSP fiber number MPD frame is receieved from (0 to 63, need 6 bits)
      4-0  = MPD_ID

      (APV channel number 0-127, APV samples: 6)
      31   = 0
      30-26= APV_CH_NUM4:0 (Channel number must be combined with next word to form full 7bit APV_CH_NUM)
      25-13= APV_SAMPLE1 (APV sample 1 for APV_CH_NUM, 13 bit signed integer)
      12-0 = APV_SAMPLE0 (APV sample 0 for APV_CH_NUM, 13 bit signed integer)

      31   = 0
      30-26= APV_CH_NUM6:5
      25-13= APV_SAMPLE3
      12-0 = APV_SAMPLE2

      31   = 0
      30-26= APV_ID
      25-13= APV_SAMPLE5
      12-0 = APV_SAMPLE4
      -------------------------------------------------
    */
    int word_count;
    UInt_t sample0, sample1, sample2;
    UInt_t sample3, sample4, sample5;
    mpd_data_found = true;

    if(data_type_id == 1) {
      // word 1
      mpd_data.enable_cm = (pdat >> 26) & 0x1;
      mpd_data.build_all_samples = (pdat >> 25) & 0x1; 
      mpd_data.cm_or = (pdat >> 24) & 0x1; 
      mpd_data.fiber = (pdat >> 15) & 0x3F; // SSP fiber number, mask 6 bit
      mpd_data.mpd_id = (pdat >> 0) & 0x1F;  // 5bits
      word_count = 0; // reset
    }
    else {
      if(word_count == 0) {
	mpd_data.apv_ch_num1 = (pdat >> 26) & 0x1F; // APV channel number 4:0
	sample1 = (pdat >> 13) & 0x1FFF; // sample1, 13bit
	sample0 = (pdat >> 0) & 0x1FFF; // sample0, 13bit
	word_count++;
      }	
      else if(word_count == 1) {
	mpd_data.apv_ch_num2 = (pdat >> 26) & 0x1F; // APV channel number 4:0
	sample3 = (pdat >> 13) & 0x1FFF; // sample1, 13bit
	sample2 = (pdat >> 0) & 0x1FFF; // sample0, 13bit
	word_count++;
      }
      else if(word_count == 2) {
	mpd_data.apv_id = (pdat >> 26) & 0x1F; // APV ID
	sample5 = (pdat >> 13) & 0x1FFF; // sample1, 13bit
	sample4 = (pdat >> 0) & 0x1FFF; // sample0, 13bit

	// Combine APV_CH_NUM(6:5) and APV_CH_NUM(4:0) and form full 7bit CH_NUM
	mpd_data.apv_ch_num = (mpd_data.apv_ch_num2 << 5) | mpd_data.apv_ch_num1;

	fAPVSamplesData[mpd_data.apv_ch_num].samples.push_back(sample0);
	fAPVSamplesData[mpd_data.apv_ch_num].samples.push_back(sample1);
	fAPVSamplesData[mpd_data.apv_ch_num].samples.push_back(sample2);
	fAPVSamplesData[mpd_data.apv_ch_num].samples.push_back(sample3);
	fAPVSamplesData[mpd_data.apv_ch_num].samples.push_back(sample4);
	fAPVSamplesData[mpd_data.apv_ch_num].samples.push_back(sample5);	
	word_count = 0; //reset
      }

    }// data dontiuation

     
  }
  //________________________________________________________________
  void MPDModule::DecodeMPDEventInfo( UInt_t pdat, UInt_t data_type_id )
  {
    /*
      -------------------------------------------------
      MPD Event Info (3 words)
      31   = 1
      30-27= 12
      23-8 = TIMESTAMP_COARSE0 (lower 16bits coarse trigger_time from MPD)
      7-0  = TIMESTAMP_FINE

      Word 2:
      31   = 0
      23-0 = TIMESTAMP_COARSE1 (upper 24bits coarse_trigger_time from MPD)

      Word 3:
      31   = 0
      19-0 = EVENT_COUNT (20bits event count from MPD)
      -------------------------------------------------
    */
    int word_count;
    if(data_type_id == 1) {
      mpd_evinfo_found = true;
      mpd_data.timestamp_coarse0 = (pdat >> 8) & 0xFFFF;
      mpd_data.timestamp_fine = (pdat >> 0) & 0xFF;
      word_count = 0;
    }      
    else {      
      if(word_count == 0) {
	mpd_data.timestamp_coarse1 = (pdat >> 0) & 0xFFFFFF;
      }
      else if(word_count == 1) {
	mpd_data.event_count = (pdat >> 0) & 0xFFFFF;
      }
      word_count++;
    }
  }

  //________________________________________________________________
  void MPDModule::DecodeMPDDebugHeader( UInt_t pdat, UInt_t data_type_id )
  {
    /*
      -------------------------------------------------
      MPD debug header 
      (contains SSP computed CM offsets for each time sample of the previous fiber/apv data, data_type=5)
      31   = 1
      25-13= CM_T1 (13bit signed CM correction value for time sample 1)
      12-0 = CM_T0 (13bit signed CM correction value for time sample 0)

      Word 2:
      31   = 0
      25-13= CM_T3
      12-0 = CM_T2

      Word 3:
      31   = 0
      25-13= CM_T5
      12-0 = CM_T4
      -------------------------------------------------
    */

    int word_count;
    if(data_type_id == 1) {
      mpd_debug_found = true;
      UInt_t cm_t1 = (pdat >> 13) & 0x1FFF; 
      UInt_t cm_t0 = (pdat >> 0) & 0x1FFF; 
      word_count = 0; // reset
      fAPVSamplesData[mpd.apv_ch_num].cm_corrections.push_back(cm_t0);
      fAPVSamplesData[mpd.apv_ch_num].cm_corrections.push_back(cm_t1);
    }
    else {
      if(word_count == 0) {
	UInt_t cm_t3 = (pdat >> 13) & 0x1FFF; 
	UInt_t cm_t2 = (pdat >> 0) & 0x1FFF; 
	fAPVSamplesData[mpd.apv_ch_num].cm_corrections.push_back(cm_t2);
	fAPVSamplesData[mpd.apv_ch_num].cm_corrections.push_back(cm_t3);

      }
      else if(word_count == 1) {      
	UInt_t cm_t5 = (pdat >> 13) & 0x1FFF; 
	UInt_t cm_t4 = (pdat >> 0) & 0x1FFF; 
	fAPVSamplesData[mpd.apv_ch_num].cm_corrections.push_back(cm_t4);
	fAPVSamplesData[mpd.apv_ch_num].cm_corrections.push_back(cm_t5);

      }
      word_count++;
    }
  }
  //________________________________________________________________
  UInt_t MPDModule::GetTriggerTimeL() const
  {
    return mpd_data.trig_time_l;
  }

  //________________________________________________________________
  UInt_t MPDModule::GetTriggerTimeH() const
  {
    return mpd_data.trig_time_h;
  }

  //________________________________________________________________
  UInt_t MPDModule::GetTriggerTime() const
  {
    return mpd_data.trig_time;
  }

  //________________________________________________________________
  UInt_t MPDModule::GetCommonModeFlag( UInt_t fiber ) const
  {
    return mpd_data.enable_cm;
  }

  //________________________________________________________________
  UInt_t MPDModule::GetBuildAllSamples( UInt_t fiber ) const
  {
    return mpd_data.build_all_samples;
  }

  //________________________________________________________________
  UInt_t MPDModule::GetCommonModeOR( UInt_t fiber ) const
  {
    return mpd_data.cm_or;
  }

  //________________________________________________________________
  UInt_t MPDModule::LoadSlot( THaSlotData *sldat, const UInt_t *evbuffer,
				 UInt_t pos, UInt_t len)
  {
    const auto* p = evbuffer +  pos;
    const auto* q = p + len;
    while( p != q ) {
      if( Decode(p++) == 1 )
	break;
    }
    LoadTHaSlotDataObj(sldat);
    return fWordsSeen = p - (evbuffer + pos);
  }

  //________________________________________________________________
  void MPDModule::LoadTHaSlotDataObj( THaSlotDataObj* sldat )
  {


  }

  //________________________________________________________________
}

ClassImp(Decoder::MPDModule)
