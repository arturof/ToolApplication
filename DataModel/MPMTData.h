#ifndef MPMT_DATA_H
#define MPMT_DATA_H

#include <WCTEMPMTHit.h>
#include <WCTEMPMTLED.h>
#include <WCTEMPMTWaveform.h>
#include <WCTEMPMTPPS.h>

class TriggerInfo;

struct MPMTData{

  unsigned int coarse_counter; //last 6 bits are zero
  std::mutex mpmt_hits_mtx;
  std::mutex mpmt_leds_mtx;
  std::mutex mpmt_waveforms_mtx;
  std::mutex mpmt_pps_mtx;
  std::mutex mpmt_triggers_mtx;
  std::mutex extra_hits_mtx;
  std::mutex extra_waveforms_mtx;
  
  std::vector<WCTEMPMTHit> mpmt_hits;
  std::vector<WCTEMPMTLED> mpmt_leds;
  std::vector<WCTEMPMTWaveform> mpmt_waveforms;
  std::vector<WCTEMPMTPPS> mpmt_pps;
  std::vector<WCTEMPMTHit> mpmt_triggers;
  std::vector<WCTEMPMTHit> extra_hits;
  std::vector<WCTEMPMTWaveform> extra_waveforms;
  unsigned int cumulative_sum[4194304U];
  std::mutex unmerged_triggers_mtx;
  std::vector<TriggerInfo> unmerged_triggers;
  void Print(){
    printf("coarse_counter=%u\n", coarse_counter);
    printf("mpmt_hits.size()=%ld\n", mpmt_hits.size());
    printf("mpmt_waveforms.size()=%ld\n", mpmt_waveforms.size());
    printf("mpm_leds.size()=%ld\n", mpmt_leds.size());
    printf("mpm_pps.size()=%ld\n", mpmt_pps.size());
    printf("mpm_triggers.size()=%ld\n", mpmt_triggers.size());
    printf("extra_hits.size()=%ld\n", extra_hits.size());
    printf("extra_waveforms.size()=%ld\n", extra_waveforms.size());
    printf("unmerged_triggers.size()=%ld\n\n", unmerged_triggers.size());
  }
};

#endif
