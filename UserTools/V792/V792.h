#ifndef V792_H
#define V792_H

#include <memory>

#include <caen++/v792.hpp>

#include "Digitizer.h"
#include "QDCHit.h"

class V792: public Digitizer<caen::V792::Packet, QDCHit> {
  private:
    struct Board {
      caen::V792 qdc;
      uint32_t (*readout)(Board&, caen::V792::Buffer&);
      int      vme_handle;
      uint32_t vme_address;
      bool     reported_nhits;
      int      expected_nhits;
    };

    struct RawEvent {
      caen::V792::Header     header;
      caen::V792::EndOfBlock trailer;
      caen::V792::Data       hits[32];
      unsigned               nhits;

      void merge(RawEvent& event, bool tail);
    };

    std::vector<Board> boards;
    caen::V792::Buffer buffer;
    Chops<RawEvent>    chops;
    void*              on_spill;

    void chop_event(size_t cycle, RawEvent&, bool head);

    void connect();
    void configure() final;

    void init(unsigned& nboards, VMEReadout<QDCHit>*& output) final;
    void fini() final;

    void start_acquisition() final;

    void readout(
        unsigned                         qdc_index,
        std::vector<caen::V792::Packet>& data
    ) final;

    static uint32_t readout_blt(Board&, caen::V792::Buffer&);
    static uint32_t readout_fifoblt(Board&, caen::V792::Buffer&);

    void process(
        size_t                                  cycle,
        const std::function<Event& (uint32_t)>& get_event,
        unsigned                                qdc_index,
        std::vector<caen::V792::Packet>         qdc_data
    ) final;

    void process(
        const std::function<Event& (uint32_t)>& get_event,
        RawEvent&
    );

    void process(
        const std::function<Event& (uint32_t)>& get_event,
        caen::V792::Header,
        caen::V792::Data*,
        caen::V792::EndOfBlock
    );
};

#endif
