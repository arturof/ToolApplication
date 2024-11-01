#include <CAENVMElib.h>

#include "Store.h"
#include "DataModel.h"

#include "V792.h"
#include "caen.h"

void V792::RawEvent::merge(RawEvent& event, bool tail) {
  if (tail)
    trailer = event.trailer;
  else
    header  = event.header;
  memcpy(hits + nhits, event.hits, event.nhits * sizeof(*hits));
  nhits += event.nhits;
};

void V792::connect() {
  auto connections = caen_connections(m_variables);
  boards.reserve(connections.size());
  for (auto& connection : connections) {
    caen_report_connection(*m_log << ML(3), "V792", connection);
    Board board { caen::V792(connection) };
    board.vme_address = connection.vme;
    uint16_t firmware = board.qdc.firmware_revision();
    *m_log
      << ML(3)
      << "V792 firmware revision is 0x"
      << std::hex
      << firmware
      << std::dec
      << std::endl;
    if (firmware <= 0x501) {
      *m_log
        << ML(2)
        << "V792: old firmware detected, using FIFOBLT for readout"
        << std::endl;
      board.vme_handle  = board.qdc.vme_handle();
      board.readout     = readout_fifoblt;
    } else {
      if (firmware < 0x904)
        *m_log
          << ML(1)
          << "V792: untested firmware revision, assuming BLTRead works"
          << std::endl;
      board.readout = readout_blt;
    };
    boards.emplace_back(std::move(board));
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

void V792::configure() {
  *m_log << ML(3) << "Configuring V792... " << std::flush;

  for (int qdc_index = 0; qdc_index < boards.size(); ++qdc_index) {
    Board& board = boards[qdc_index];
    caen::V792& qdc = board.qdc;
    qdc.reset();
    qdc.clear();

    bool  flag;
    int   i;
    float x;
    std::string s;
    std::string var;
#define cfgvar(name, var) \
    if (cfg_get(m_variables, #name, qdc_index, var)) qdc.set_ ## name(var)
#define cfgbool(name)  cfgvar(name, flag)
#define cfgint(name)   cfgvar(name, i)

    cfgint(geo_address);
    cfgint(interrupt_level);
    cfgint(interrupt_vector);

    {
      auto control = qdc.control1();
      auto old_control = control;
#define defbit(name) \
      if (m_variables.Get(#name, flag)) control.set_ ## name(flag)
      defbit(block_readout);
      defbit(panel_resets_software);
      if (m_variables.Get("enable_bus_error", flag))
        control.set_bus_error_enabled(flag);
      defbit(align_64);
#undef defbit
      if (old_control != control) qdc.set_control1(control);
    };

    cfgint(event_trigger);

    {
      auto bitset = qdc.bitset2();
      auto old_bitset = bitset;
#define defbit(name) \
      if (m_variables.Get("enable_" #name, flag)) \
        bitset.set_ ## name ## _enabled(flag)
      defbit(overflow);
      defbit(threshold);
      defbit(slide);
      if (m_variables.Get("shift_threshold", flag))
        bitset.set_shift_threshold(flag);
      defbit(empty);
      defbit(slide_subtraction);
      if (m_variables.Get("all_triggers", flag))
        bitset.set_all_triggers(flag);
#undef defbit
      if (old_bitset != bitset) qdc.set_bitset2(bitset);
    };

    cfgint(crate_number);
    cfgint(current_pedestal);
    cfgint(slide_constant);

    uint32_t mask = 0xFFFFFFFF;
    if (cfg_get(m_variables, "enable_channels", qdc_index, s)) {
      size_t j;
      mask = std::stol(s, &j, 16);
      if (j != s.size())
        throw std::runtime_error(
            std::string("V792: invalid value for enable_channels: ") + s
        );
    };
    std::stringstream ss;
    for (uint8_t channel = 0; channel < 32; ++channel) {
      ss.str({});
      ss << "channel_" << static_cast<int>(channel) << "_threshold";
      bool enable = mask & 1 << channel;
      if (cfg_get(m_variables, ss.str(), qdc_index, i))
        qdc.set_channel_settings(channel, i, enable);
      else
        qdc.set_channel_enabled(channel, enable);
    };

    board.reported_nhits = !cfg_get(
        m_variables, "expected_nhits", qdc_index, board.expected_nhits
    );
#undef cfgint
#undef cfgbool
#undef cfgvar
  };

  *m_log << ML(3) << "success" << std::endl;
};

void V792::init(unsigned& nboards, VMEReadout<QDCHit>*& output) {
  connect();
  configure();
  nboards = boards.size();
  output  = &m_data->qdc_readout;
  on_spill = m_data->AlertSubscribe(
      "SpillCount",
      [this](const char* alert, const char* payload) {
        for (auto& board : boards) board.qdc.reset_event_counter();
      }
  );
};

void V792::fini() {
  boards.clear();
  if (on_spill) {
    m_data->AlertUnsubscribe("SpillCount", on_spill);
    on_spill = nullptr;
  };
};

void V792::start_acquisition() {
  for(auto& board : boards) {
    board.qdc.clear();
    board.qdc.reset_event_counter();
  };
  Digitizer<caen::V792::Packet, QDCHit>::start_acquisition();
};

void V792::readout(unsigned qdc_index, std::vector<caen::V792::Packet>& data) {
  Board& board = boards[qdc_index];
  uint32_t n = board.readout(board, buffer);
  data.insert(data.end(), buffer.begin(), buffer.begin() + n);

  if (board.reported_nhits) return;
  for (uint32_t i = 0; i < n; ++i)
    if (buffer[i].type() == caen::V792::Packet::Header) {
      auto header = buffer[i].as<caen::V792::Header>();
      if (header.count() != board.expected_nhits) {
        *m_log
          << ML(0)
          << "QDC 0x"
          << std::hex << std::setfill('0') << std::setw(8) << board.vme_address
          << std::dec
          << " got "
          << static_cast<unsigned>(header.count())
          << " packets, while expecting "
          << static_cast<unsigned>(board.expected_nhits);
        board.reported_nhits = true;
      };
    };
};

uint32_t V792::readout_blt(Board& board, caen::V792::Buffer& buffer) {
  board.qdc.readout(buffer);
  return buffer.size();
};

uint32_t V792::readout_fifoblt(Board& board, caen::V792::Buffer& buffer) {
  int n;
  CVErrorCodes status = CAENVME_FIFOBLTReadCycle(
      board.vme_handle,
      board.vme_address,
      buffer.raw(),
      buffer.max_size() * sizeof(uint32_t),
      cvA32_U_DATA,
      cvD32,
      &n
  );

  if (status != cvSuccess && status != cvBusError) {
    CAENComm_ErrorCode code;
    switch (status) {
      case cvCommError:
        code = CAENComm_CommError;
        break;
      case cvGenericError:
        code = CAENComm_GenericError;
        break;
      case cvInvalidParam:
        code = CAENComm_InvalidParam;
        break;
      case cvTimeoutError:
        code = CAENComm_CommTimeout;
        break;
      case cvAlreadyOpenError:
        code = CAENComm_DeviceAlreadyOpen;
        break;
      case cvMaxBoardCountError:
        code = CAENComm_MaxDevicesError;
        break;
      case cvNotSupported:
        code = CAENComm_NotSupported;
        break;
      default:
        code = static_cast<CAENComm_ErrorCode>(static_cast<int>(status) - 100);
    };
    throw caen::Device::Error(code);
  };

  n /= 4;

  // Find the first Invalid packet, or skip to the end of the buffer

  if (n == 0 || buffer[n-1].type() != caen::V792::Packet::Invalid)
    return n;

  if (buffer[0].type() == caen::V792::Packet::Invalid)
    return 0;

  // binary search
  uint32_t m = 0;
  while (n - m > 1) {
    uint32_t k = (n + m) / 2;
    if (buffer[k].type() == caen::V792::Packet::Invalid)
      n = k;
    else
      m = k;
  };

  return n;
};

void V792::process(
    size_t                                  cycle,
    const std::function<Event& (uint32_t)>& get_event,
    unsigned                                qdc_index,
    std::vector<caen::V792::Packet>         qdc_data
) {
  if (qdc_data.empty()) return;

  auto packet = qdc_data.begin();

  auto skip_fillers = [&]() {
    while (   packet != qdc_data.end()
           && packet->type() == caen::V792::Packet::Invalid)
      ++packet;
  };

  skip_fillers();
  if (packet == qdc_data.end()) return;

  RawEvent* event = nullptr;

  if (packet->type() != caen::V792::Packet::Header) {
    // alloca is to avoid initialization of event --- all fields will be
    // overwritten anyway
    event = reinterpret_cast<RawEvent*>(alloca(sizeof(*event)));
    event->nhits = 0;
    while (packet->type() != caen::V792::Packet::EndOfBlock) {
      if (event->nhits >= 32)
        throw std::runtime_error("QDC: too many hits in an event");
      event->hits[event->nhits++] = packet++->as<caen::V792::Data>();
      if (packet == qdc_data.end())
        throw std::runtime_error("QDC: unexpected end of a chopped event");
    };
    event->trailer = packet->as<caen::V792::EndOfBlock>();
    if (chops.chop_event(cycle, *event, false))
      process(get_event, *event);
    skip_fillers();
  };

  while (packet != qdc_data.end()) {
    if (packet->type() != caen::V792::Packet::Header) {
      std::stringstream ss;
      ss
        << "QDC: unexpected packet 0x"
        << std::hex << *packet
        << ", expected header";
      throw std::runtime_error(ss.str());
    };

    auto header   = packet++->as<caen::V792::Header>();
    auto ptrailer = packet + header.count();
    if (ptrailer < qdc_data.end()) {
      auto trailer = ptrailer->as<caen::V792::EndOfBlock>();
      process(
          get_event, header, static_cast<caen::V792::Data*>(&*packet), trailer
      );
      packet = ptrailer;
      ++packet;
      skip_fillers();
    } else {
      if (!event) event = reinterpret_cast<RawEvent*>(alloca(sizeof(*event)));
      event->header = header;
      event->nhits  = 0;
      while (++packet != qdc_data.end())
        event->hits[event->nhits++] = packet->as<caen::V792::Data>();
      if (chops.chop_event(cycle + 1, *event, true))
        process(get_event, *event);
    };
  };
};

void V792::process(
    const std::function<Event& (uint32_t)>& get_event,
    RawEvent&                               event
) {
  if (event.nhits != event.header.count()) {
    std::stringstream ss;
    ss
      << "QDC: mismatched chopped event: expected "
      << static_cast<unsigned>(event.header.count())
      << " hits, got "
      << event.nhits;
    throw std::runtime_error(ss.str());
  };
  process(get_event, event.header, event.hits, event.trailer);
};

void V792::process(
    const std::function<Event& (uint32_t)>& get_event,
    caen::V792::Header                      header,
    caen::V792::Data*                       data,
    caen::V792::EndOfBlock                  trailer
) {
  Event& event = get_event(trailer.event());
  uint8_t nhits = header.count();

  std::lock_guard<std::mutex> lock(*event.mutex);
  size_t n = event.hits.size();
  event.hits.reserve(n + nhits);
  while (nhits--) event.hits.emplace_back(header, *data++, trailer);
};
