#include "Store.h"
#include "DataModel.h"

#include "V1290.h"
#include "caen.h"

void V1290::RawEvent::merge(RawEvent& event, bool tail) {
  if (tail)
    trailer = event.trailer;
  else
    header  = event.header;
  if (ettt == 0) ettt = event.ettt;
  hits.insert(hits.end(), event.hits.begin(), event.hits.end());
};

void V1290::connect() {
  auto connections = caen_connections(m_variables);
  boards.reserve(connections.size());
  for (auto& connection : connections) {
    caen_report_connection(*m_log << ML(3), "V1290", connection);
    boards.emplace_back(Board { caen::V1290(connection) });
  };
};

template <typename T>
static bool cfg_get(
    ToolFramework::Store& variables,
    std::string name,
    int index,
    T& var
) {
  std::stringstream ss;
  ss << name << '_' << index;
  return variables.Get(ss.str(), var) || variables.Get(std::move(name), var);
};

template <typename T>
static bool cfg_get(
    ToolFramework::Store& variables,
    std::string name,
    int index1,
    int index2,
    T& var
) {
  return cfg_get(variables, name + '_' + std::to_string(index1), index2, var);
};

static uint32_t cfg_hex_to_uint(
    const std::string& field, const std::string& string
) {
  size_t end;
  uint32_t hex = std::stol(string, &end, 16);
  if (end != string.size())
    throw std::runtime_error(
        std::string("V1290: invalid hexadecimal number for ")
        + field
        + ": "
        + string
    );
  return hex;
};

static bool cfg_get_hex(
    ToolFramework::Store& variables,
    std::string name,
    int index,
    uint32_t& var
) {
  std::string string;
  if (!cfg_get(variables, name, index, string)) return false;
  size_t end;
  var = cfg_hex_to_uint(name, string);
  return true;
};

static bool cfg_get_mask(
    ToolFramework::Store& variables,
    std::string name,
    int index,
    uint32_t& mask
) {
  std::string string;
  mask = 0;
  bool set = cfg_get_hex(variables, name + 's', index, mask);
  for (int i = 0; i < 32; ++i) {
    bool flag;
    if (cfg_get(variables, name, i, index, flag)) {
      set = true;
      uint32_t bit = 1 << i;
      if (flag) mask |=  bit;
      else      mask &= ~bit;
    };
  };
  return set;
};

template <typename uint>
static inline void apply_adjusts(
    ToolFramework::Store& variables,
    caen::V1290& tdc,
    const char* name,
    int tdc_index,
    uint8_t number,
    void (caen::V1290::*adjust)(uint8_t, uint)
) {
  std::vector<uint> adjusts;
  std::string option("adjust_");
  option += name;
  cfg_get(variables, option + 's', tdc_index, adjusts);
  for (uint8_t i = 0; i < number; ++i) {
    bool set = i < adjusts.size();
    uint8_t value = set ? adjusts[i] : 0;
    set = cfg_get(variables, option, i, tdc_index, value) || set;
    if (set) (tdc.*adjust)(i, value);
  };
};

void V1290::configure() {
  *m_log << ML(3) << "Configuring V1290... " << std::flush;

  for (int tdc_index = 0; tdc_index < boards.size(); ++tdc_index) {
    Board& board = boards[tdc_index];
    caen::V1290& tdc = board.tdc;
    tdc.reset();
    sleep(1); // the board takes approximately 1 second to reset

    bool     flag;
    int      i;
    float    x;
    uint32_t mask;
    std::string s;
    std::string var;
#define cfgvar(name, var) \
    if (cfg_get(m_variables, #name, tdc_index, var)) tdc.set_ ## name(var)
#define cfgbool(name)  cfgvar(name, flag)
#define cfgenable(name) \
    if (cfg_get(m_variables, "enable_" #name, tdc_index, flag)) \
      tdc.set_ ## name ## _enabled(flag)
#define cfgint(name)   cfgvar(name, i)
#define cfgfloat(name) cfgvar(name, x)

    cfgbool(triggered_mode);

    cfgenable(bus_error);
    cfgbool(sw_termination);
    cfgenable(sw_termination);
    cfgbool(emit_empty_events);
    cfgbool(align_64);
    cfgenable(compensation);
    cfgenable(ettt);
    cfgint(interrupt_level);
    cfgint(interrupt_vector);
    cfgint(geo_address);
    cfgfloat(window_width);
    cfgfloat(window_offset);
    cfgfloat(search_margin);
    cfgfloat(reject_margin);
    cfgbool(trigger_time_subtraction);
    cfgint(blt_event_number);

    if (cfg_get(m_variables, "edge_detection", tdc_index, s))
      if (s == "leading")
        tdc.set_edge_detection(true, false);
      else if (s == "trailing")
        tdc.set_edge_detection(false, true);
      else if (s == "both")
        tdc.set_edge_detection(true, true);
      else
        throw std::runtime_error(
            std::string("V1290: invalid edge_detection setting: ") + s
        );

    {
      float edge  = 0;
      float pulse = 0;
      bool fedge =  cfg_get(m_variables, "edge_resolution",  tdc_index, edge)
                 || cfg_get(m_variables, "resolution",       tdc_index, edge);
      bool fpulse = cfg_get(m_variables, "pulse_resolution", tdc_index, pulse);
      if (fedge || fpulse) {
        if (!fedge || !fpulse) {
          auto r = tdc.resolution();
          if (!fedge)  edge  = r.edge;
          if (!fpulse) pulse = r.pulse;
        };
        tdc.set_resolution(edge, pulse);
      };
    };

    cfgfloat(dead_time);
    cfgenable(header_and_trailer);
    cfgint(event_size);
    if (cfg_get(m_variables, "enable_error_mark", tdc_index, flag))
      tdc.enable_error_mark(flag);
    if (cfg_get(m_variables, "enable_error_bypass", tdc_index, flag))
      tdc.enable_error_bypass(flag);

    {
      auto mask = tdc.internal_errors();
      auto old_mask = mask;
#define defbit(name) \
      if (cfg_get(m_variables, "enable_" #name "_error", tdc_index, flag)) \
        mask.set_ ## name(flag)
      defbit(vernier);
      defbit(coarse);
      defbit(channel);
      defbit(l1_parity);
      defbit(trigger_fifo);
      defbit(trigger);
      defbit(readout_fifo);
      defbit(readout);
      defbit(setup);
      defbit(control);
      defbit(jtag);
#undef defbit
      if (mask != old_mask) tdc.set_internal_errors(mask);
    };

    cfgint(fifo_size);

    mask = 0xFFFFFFFF;
    cfg_get_mask(m_variables, "enable_channel", tdc_index, mask);
    tdc.enable_channels(mask);

    {
      std::vector<std::string> tdc_channels;
      cfg_get(m_variables, "enable_tdc_channels", tdc_index, tdc_channels);

      std::stringstream ss;
      for (uint8_t j = 0; j < 4; ++j) {
        ss.str({});
        ss << "enable_tdc_" << static_cast<int>(j) << "_channel";
        s = ss.str();
        bool set = cfg_get_mask(m_variables, s, tdc_index, mask);
        if (!set && j < tdc_channels.size()) {
          mask = cfg_hex_to_uint(s, tdc_channels[j]);
          set = true;
        };
        if (set) tdc.enable_tdc_channels(j, mask);
      };
    };

    {
      uint16_t coarse = 0;
      uint16_t fine   = 0;
      bool set = cfg_get(
          m_variables, "global_offset_coarse", tdc_index, coarse
      );
      set = cfg_get(m_variables, "global_offset_fine", tdc_index, fine) || set;
      if (set) tdc.set_global_offset(coarse, fine);
    };

    apply_adjusts(
        m_variables, tdc, "channel", tdc_index, 32, &caen::V1290::adjust_channel
    );
    apply_adjusts(
        m_variables, tdc, "rc", tdc_index, 4, &caen::V1290::adjust_rc
    );

    if (cfg_get(m_variables, "load_scan_path", tdc_index, flag))
      if (flag)
        tdc.scan_path_load();

    for (uint8_t j = 0; j < 4; ++j)
      if (cfg_get(m_variables, "load_scan_path_tdc", j, tdc_index, flag))
        if (flag) tdc.scan_path_load(j);

    if (cfg_get(m_variables, "dll_clock", tdc_index, s)) {
      uint8_t clock;
      if (s == "direct_40")
        clock = 0;
      else if (s == "PLL_40")
        clock = 1;
      else if (s == "PLL_160")
        clock = 2;
      else if (s == "PLL_320")
        clock = 3;
      else
        throw std::runtime_error(
            std::string("V1290: invalid value for dll_clock: ") + s
        );
      tdc.set_dll_clock(clock);
    };
#undef cfgfloat
#undef cfgint
#undef cfgenable
#undef cfgbool
#undef cfgvar
  };

  *m_log << ML(3) << "success" << std::endl;
};

void V1290::init(unsigned& nboards, VMEReadout<TDCHit>*& output) {
  connect();
  configure();
  nboards = boards.size();
  output  = &m_data->tdc_readout;
  on_spill = m_data->AlertSubscribe(
      "SpillNum",
      [this](const char* alert, const char* payload) {
        std::cout << "TDC: spill count received" << std::endl;
        //for (auto& board : boards) board.tdc.reset_event();
      }
  );
};

void V1290::fini() {
  boards.clear();
  if (on_spill) {
    m_data->AlertUnsubscribe("SpillCount", on_spill);
    on_spill = nullptr;
  };
};

void V1290::start_acquisition() {
  chops.clear();
  for (auto& board : boards) board.tdc.clear();
  Digitizer<caen::V1290::Packet, TDCHit>::start_acquisition();
};

void V1290::stop_acquisition() {
  Digitizer<caen::V1290::Packet, TDCHit>::stop_acquisition();
};

void V1290::process(
    size_t                                   cycle,
    const std::function<Event& (uint32_t)>&  get_event,
    unsigned                                 tdc_index,
    std::vector<caen::V1290::Packet>         tdc_data
) {
  if (tdc_data.empty()) return;

  RawEvent event;
  event.ettt  = 0;
  auto copy_hits = [&](
      std::vector<caen::V1290::Packet>::const_iterator header,
      std::vector<caen::V1290::Packet>::const_iterator trailer,
      unsigned nhits
  ) {
    event.hits.resize(nhits);
    unsigned i = 0;
    for (auto packet = header == tdc_data.end() ? tdc_data.begin() : header + 1;
         packet != trailer;
         ++packet)
      if (packet->type() == caen::V1290::Packet::TDCMeasurement)
        event.hits[i++] = packet->as<caen::V1290::TDCMeasurement>();
  };

  unsigned nhits = 0;
  auto header = tdc_data.end();
  for (auto packet = tdc_data.begin(); packet != tdc_data.end(); ++packet)
    switch (packet->type()) {
      case caen::V1290::Packet::GlobalHeader:
        header = packet;
        nhits  = 0;
        event.header = packet->as<caen::V1290::GlobalHeader>();
        event.ettt   = 0;
        break;

      case caen::V1290::Packet::TDCError:
        report_error(tdc_index, packet->as<caen::V1290::TDCError>());
        break;

      case caen::V1290::Packet::ExtendedTriggerTimeTag:
        event.ettt = packet->as<caen::V1290::ExtendedTriggerTimeTag>();
        break;

      case caen::V1290::Packet::TDCMeasurement:
        ++nhits;
        break;

      case caen::V1290::Packet::GlobalTrailer:
        copy_hits(header, packet, nhits);
        if (header != tdc_data.end() || chops.chop_event(cycle, event, false))
          process(get_event, event);
        header = tdc_data.end();
        break;
    }

  if (header != tdc_data.end()) {
    copy_hits(header, tdc_data.end(), nhits);
    if (chops.chop_event(cycle + 1, event, true))
      process(get_event, event);
  };
};

void V1290::process(
    const std::function<Event& (uint32_t)>& get_event,
    RawEvent& raw
) {
  Event& event = get_event(raw.header.event());

  std::lock_guard<std::mutex> lock(*event.mutex);
  size_t n = event.hits.size();
  event.hits.reserve(n + raw.hits.size());
  for (auto& hit : raw.hits)
    event.hits.emplace_back(raw.header, hit, raw.ettt, raw.trailer);
};

void V1290::readout(
    unsigned                          tdc_index,
    std::vector<caen::V1290::Packet>& data
) {
  boards[tdc_index].tdc.readout(buffer);
  data.insert(data.end(), buffer.begin(), buffer.end());
};

void V1290::report_error(unsigned tdc_index, caen::V1290::TDCError error) {
  uint16_t flags = error.errors();
  volatile uint16_t& reported = boards[tdc_index].errors;
  if ((reported & flags) == flags) return;
  std::lock_guard<std::mutex> lock(tdc_errors_mutex);
  if ((reported & flags) == flags) return;
  uint16_t unreported = flags & ~reported;
  reported |= flags;
  *m_log
    << ML(0)
    << "V1290 " << tdc_index
    << " reports error 0x" << std::hex << unreported << std::dec
    << " in TDC " << static_cast<int>(error.tdc())
    << ": ";

  bool comma_ = false;
  auto comma = [&]() mutable {
    if (comma_)
      *m_log << ", ";
    else
      comma_ = true;
  };

  auto report_group_errors = [&](
      unsigned    bit,
      const char* singular,
      const char* plural,
      const char* reason
  ) {
    int groups[4];
    int n = 0;
    for (int group = 0; group < 4; ++group)
      if (unreported & 1 << 3 * group + bit)
        groups[n++] = group;
    if (n == 0) return;

    comma();

    *m_log << (n == 1 ? singular : plural);
    bool comma2 = false;
    for (int i = 0; i < n; ++i) {
      if (comma2)
        *m_log << ", ";
      else
        comma2 = true;
      *m_log << groups[i];
    };
    *m_log << reason;
  };

  report_group_errors(
      0,
      "hit lost in group ",
      "hits lost in groups ",
      " due to readout FIFO overflow"
  );
  report_group_errors(
      1,
      "hit lost in group ",
      "hits lost in groups ",
      " due to L1 buffer overflow"
  );
  report_group_errors(
      2,
      "hit error has been detected in group ",
      "hits errors have been detected in groups ",
      ""
  );

  if (unreported & 1 << 12) {
    comma();
    *m_log << "hits rejected because of programmed event size limit";
  };

  if (unreported & 1 << 13) {
    comma();
    *m_log << "event lost due to trigger FIFO overflow";
  };

  if (unreported & 1 << 14) {
    comma();
    *m_log << "internal fatal chip error has been detected";
  };
  // TODO: send alarm
};
