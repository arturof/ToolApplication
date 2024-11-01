#ifndef DIGITIZER_H
#define DIGITIZER_H

#include <functional>
#include <map>
#include <thread>
#include <vector>

#include "DataModel.h"
#include "ThreadLoop.h"
#include "Tool.h"

template <typename Packet, typename Hit>
class Digitizer: public ToolFramework::Tool {
  public:
    bool Initialise(std::string configfile, DataModel& data);
    bool Execute();
    bool Finalise();

    bool acquiring() const { return acquiring_; };

  protected:
    struct Event: VMEReadout<Hit>::Event {
      std::unique_ptr<std::mutex> mutex;

      Event(typename VMEReadout<Hit>::Time time):
        VMEReadout<Hit>::Event { time },
        mutex(new std::mutex())
      {};

      Event(Event&& event):
        VMEReadout<Hit>::Event(event),
        mutex(std::move(event.mutex))
      {};
    };

    template <typename RawEvent>
    class Chops {
      public:
        bool chop_event(size_t cycle, RawEvent& event, bool head);
        void clear() { chops.clear(); };

      private:
        std::map<size_t, RawEvent> chops;
        std::mutex                 mutex;
    };

    // Connects and configures the boards. Returns the number of boards and a
    // pointer to VMEReadout object to store data in. This method is called
    // from Initialise and is wrapped in a try-catch block.
    virtual void init(unsigned& nboards, VMEReadout<Hit>*& output) = 0;

    // This method is called from Finalise and is wrapped in a try-catch block.
    virtual void fini() {};

    virtual void start_acquisition();
    virtual void stop_acquisition();

    virtual void configure() {};

    // Reads board `board_index` and fills `board_data` with its data. This
    // method is called from the readout thread and shall be as fast as
    // possible.
    virtual void readout(
        unsigned board_index, std::vector<Packet>& board_data
    ) = 0;

    // Processes the data read from board `board_index`. The data shall be
    // converted to hits and stored in the result of `get_event` which
    // takes an event number and returns an event associated with this event
    // number. The event has a vector of hits to be filled by this function and
    // a mutex which should be locked while the vector is being changed.
    // This method is called from the worker job queues.
    virtual void process(
        size_t                                  cycle,
        const std::function<Event& (uint32_t)>& get_event,
        unsigned                                board_index,
        std::vector<Packet>                     board_data
    ) = 0;

  private:
    struct Readout {
      typename VMEReadout<Hit>::Time   time;
      std::vector<std::vector<Packet>> data;
      size_t                           cycle;
    };

    bool     standalone     = false;
    bool     acquiring_     = false;
    unsigned nboards        = 0;
    VMEReadout<Hit>* output = nullptr;

    ThreadLoop::Thread readout_thread;

    size_t readout_cycle; // number of the last readout cycle
    size_t process_cycle; // number of the next cycle to be submitted
    bool last_readout_empty;

    // `cycles` keys is a set of cycles that have been stored in `events` but
    // not submitted yet. `cycles` values are the numbers of the latest events
    // that are safe to write to the DataModel provided that:
    // for cycles[i].first:  the previous cycles (with index less than i)
    //                       have been processed;
    // for cycles[i].second: both the previous cycles and the next cycle have
    //                       been processed.
    // cycles[i].second is the largest event number seen during processing of this cycle.
    // cycles[i].first is the smallest event number across largest event number per board.
    std::map<size_t, std::pair<uint32_t, uint32_t>> cycles;
    std::mutex cycles_mutex;


    std::map<uint32_t, Event> events;
    std::mutex                events_mutex;

    void readout();
    bool process(Readout);

    void submit(
        typename std::map<uint32_t, Event>::iterator begin,
        typename std::map<uint32_t, Event>::iterator end
    );

};

template <typename Packet, typename Hit>
template <typename RawEvent>
bool Digitizer<Packet, Hit>::Chops<RawEvent>::chop_event(
    size_t cycle, RawEvent& event, bool head
) {
  std::lock_guard<std::mutex> lock(mutex);
  auto pevent = chops.lower_bound(cycle);
  if (pevent == chops.end() || pevent->first != cycle) {
    chops.insert(pevent, { cycle, event });
    return false;
  };
  event.merge(pevent->second, head);
  chops.erase(pevent);
  return true;
};

template <typename Packet, typename Hit>
void Digitizer<Packet, Hit>::readout() {
  std::vector<std::vector<Packet>> data(nboards);
  bool empty = true;
  for (unsigned i = 0; i < nboards; ++i) {
    readout(i, data[i]);
    empty = empty && data[i].empty();
  };

  // If this readout is empty and the previous readout wasn't empty, send empty
  // data for processing so that the previous readout could be submitted

  if (empty && last_readout_empty) return;
  last_readout_empty = empty;

  using arg_t = std::pair<Digitizer<Packet, Hit>&, Readout>;

  Job* job = new Job("Digitizer::readout");
  job->func = [](void* job_data) -> bool {
    auto arg = static_cast<arg_t*>(job_data);
    bool result = arg->first.process(std::move(arg->second));
    delete arg;
    return result;
  };
  job->data = new arg_t {
    *this,
    Readout {
      std::chrono::time_point_cast<std::chrono::milliseconds>(
          VMEReadout<Hit>::Time::clock::now()
      ),
      std::move(data),
      readout_cycle++
    }
  };

  m_data->job_queue.AddJob(job);
};

template <typename Packet, typename Hit>
bool Digitizer<Packet, Hit>::process(Readout readout) {
  try {
    uint32_t max_event[nboards];
    std::memset(max_event, 0xFF, sizeof(max_event));

    for (unsigned iboard = 0; iboard < nboards; ++iboard)
      process(
          readout.cycle,
          [&](uint32_t ievent) -> Event& {
            // XXX: assuming no overflow of the event number
            if (max_event[iboard] == ~0U || max_event[iboard] < ievent)
              max_event[iboard] = ievent;
            std::lock_guard<std::mutex> lock(events_mutex);
            auto pevent = events.lower_bound(ievent);
            if (pevent == events.end() || pevent->first != ievent)
              pevent = events.insert(pevent, { ievent, Event(readout.time) });
            return pevent->second;
          },
          iboard,
          std::move(readout.data[iboard])
      );

    // Events read during the last readout cycle may be incomplete since the
    // boards are read sequentially at different times. We keep track of the
    // last event read for each board in max_event; we can submit events that
    // have the numbers less than the smallest stored in max_event _and_ each
    // preceding readout cycle has been processed.

    uint32_t min_max_event = ~0U;
    uint32_t max_max_event = ~0U;
    int i = 0;
    for (; i < nboards; ++i)
      if (max_event[i] != ~0U) {
        min_max_event = max_max_event = max_event[i];
        break;
      };
    for (++i; i < nboards; ++i)
      if (max_event[i] != ~0U) {
        if (max_event[i] < min_max_event)
          min_max_event = max_event[i];
        else if (max_event[i] > max_max_event)
          max_max_event = max_event[i];
      };

    std::lock_guard<std::mutex> cycles_lock(cycles_mutex);

    // Note that for an empty readout we store ~0U as both event numbers
    cycles[readout.cycle] = std::pair<uint32_t, uint32_t> {
      min_max_event, max_max_event
    };

    auto done = cycles.begin();
    if (done->first != process_cycle)
      // the next sequential cycle is not processed yet
      return true;

    // look for a gap in the cycle numbers
    size_t pcycle = process_cycle;
    while (++done != cycles.end() && done->first == ++pcycle);
    --done;

    // Now [ cycles.begin(); done ] is a continuous sequence of cycles that
    // have been processed. Only the last cycle in this sequence may have
    // incomplete events.

    if (done == cycles.begin()) {
      if (done->second.first == ~0U) cycles.erase(done);
      return true;
    };

    uint32_t last_ievent = done->second.first;
    for (auto cycle = done; last_ievent == ~0U && cycle-- != cycles.begin();)
      last_ievent = cycle->second.second;
    if (last_ievent != ~0U) {
      std::lock_guard<std::mutex> events_lock(events_mutex);
      auto last_event = events.upper_bound(last_ievent);
      submit(events.begin(), last_event);
      events.erase(events.begin(), last_event);
    };
    cycles.erase(cycles.begin(), done);
    process_cycle = pcycle;

    return true;
  } catch (std::exception& e) {
    *m_log << ML(0) << e.what() << std::endl;
    return false;
  };
};

template <typename Packet, typename Hit>
void Digitizer<Packet, Hit>::submit(
    typename std::map<uint32_t, Event>::iterator begin,
    typename std::map<uint32_t, Event>::iterator end
) {
  class values_iterator {
    public:
      values_iterator(typename std::map<uint32_t, Event>::iterator iterator):
        iterator(iterator)
      {};

      bool operator!=(const values_iterator& i) {
        return iterator != i.iterator;
      };

      values_iterator& operator++() {
        ++iterator;
        return *this;
      };

      values_iterator  operator++(int) {
        values_iterator i(iterator);
        ++iterator;
        return i;
      };

      Event& operator*() const {
        return iterator->second;
      };

    public:
      typename std::map<uint32_t, Event>::iterator iterator;
  };

  output->push(values_iterator(begin), values_iterator(end));
};

template <typename Packet, typename Hit>
void Digitizer<Packet, Hit>::start_acquisition() {
  readout_thread = m_data->vme_readout_loop.add(
      [this]() -> bool {
        try {
          readout();
          return true;
        } catch (std::exception& e) {
          *m_log << ML(0) << e.what() << std::endl;
          return false;
        }
      }
  );
  acquiring_ = true;
};

template <typename Packet, typename Hit>
void Digitizer<Packet, Hit>::stop_acquisition() {
  acquiring_ = false;
  readout_thread.terminate();
};

template <typename Packet, typename Hit>
bool Digitizer<Packet, Hit>::Initialise(std::string configfile, DataModel& data) {
  try {
    InitialiseTool(data);
    InitialiseConfiguration(std::move(configfile));

    if (!m_variables.Get("verbose", m_verbose)) m_verbose = 1;
    m_variables.Get("standalone", standalone);

    init(nboards, output);
    readout_cycle = 0;
    process_cycle = 0;
    last_readout_empty = true;
    acquiring_ = false;

    ExportConfiguration();

    return true;
  } catch (std::exception& e) {
    if (m_log)
      *m_log << ML(0) << e.what() << std::endl;
    else
      fprintf(stderr, "%s\n", e.what());
    return false;
  };
};

template <typename Packet, typename Hit>
bool Digitizer<Packet, Hit>::Execute() {
  if (nboards == 0) return true;
  try {
    if (standalone) {
      if (acquiring_) stop_acquisition();
      start_acquisition();
      return true;
    };

    if (m_data->run_start && !acquiring_) start_acquisition();

    if (m_data->change_config) {
      bool acq = acquiring_;
      if (acq) stop_acquisition();
      InitialiseConfiguration();
      configure();
      if (acq) start_acquisition();
    };

    if (m_data->run_stop && acquiring_) stop_acquisition();

    return true;
  } catch (std::exception& e) {
    *m_log << ML(0) << e.what() << std::endl;
    return false;
  };
};

template <typename Packet, typename Hit>
bool Digitizer<Packet, Hit>::Finalise() {
  try {
    if (acquiring_) stop_acquisition();
    fini();
    return true;
  } catch (std::exception& e) {
    *m_log << ML(0) << e.what() << std::endl;
    return false;
  };
};

#endif
