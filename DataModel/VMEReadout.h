#ifndef VME_READOUT_H
#define VME_READOUT_H

#include <deque>
#include <mutex>
#include <vector>
#include <chrono>

#include <zmq.hpp>

#include <QDCHit.h>
#include <TDCHit.h>

template <typename Hit>
class VMEReadout {
public:
  using Time = std::chrono::time_point<
    std::chrono::system_clock,
    std::chrono::milliseconds
  >;

  struct Event {
    Time             time;
    std::vector<Hit> hits;
  };

  template <typename Iterator>
  void push(Iterator begin, Iterator end);
  
  std::deque<Event> get();

  // Should be named getHits or, better, pop_hits.
  // Keep it compatible with the upstream.
  std::vector<Hit> getEvent();

  bool Send(zmq::socket_t* sock);
  bool Receive(zmq::socket_t* sock);
  unsigned int size() const { return readout.size(); }
  bool empty() const { return readout.empty(); };
  
private:

  std::mutex mutex;
  std::deque<Event> readout;
};

template <typename Hit>
template <typename Iterator>
void VMEReadout<Hit>::push(Iterator begin, Iterator end) {
  std::lock_guard<std::mutex> lock(mutex);
  for (auto i = begin; i != end; ++i) readout.push_back(std::move(*i));
};

template <typename Hit>
std::deque<typename VMEReadout<Hit>::Event>
VMEReadout<Hit>::get() {
  std::lock_guard<std::mutex> lock(mutex);
  return std::move(readout);
};

template <typename Hit>
std::vector<Hit> VMEReadout<Hit>::getEvent() {
  std::lock_guard<std::mutex> lock(mutex);
  if (readout.empty()) return std::vector<Hit>();
  std::vector<Hit> result = std::move(readout.front().hits);
  readout.pop_front();
  return result;
};

namespace VMEReadout_ {
  template <typename Hit> struct Header {};

  template <> struct Header<QDCHit> {
    static constexpr const char signature[] = "QDC";
  };

  template <> struct Header<TDCHit> {
    static constexpr const char signature[] = "TDC";
  };
};

template <typename Hit>
bool VMEReadout<Hit>::Send(zmq::socket_t* socket) {
  if (this->readout.empty()) return true;

  std::deque<Event> readout;
  {
    std::lock_guard<std::mutex> lock(mutex);
    readout.swap(this->readout);
  };

  zmq::message_t header(4);
  memcpy(
      header.data(),
      VMEReadout_::Header<Hit>::signature,
      sizeof(VMEReadout_::Header<Hit>::signature) - 1
  );
  if (!socket->send(header, ZMQ_SNDMORE)) return false;

  auto event = readout.begin();
  while (event != readout.end()) {
    zmq::message_t time(sizeof(Time));
    memcpy(time.data(), &event->time, sizeof(Time));
    if (!socket->send(time, ZMQ_SNDMORE)) return false;

    size_t size = sizeof(Hit) * event->hits.size();
    zmq::message_t hits(size);
    memcpy(hits.data(), event->hits.data(), size);
    if (!socket->send(hits, ++event == readout.end() ? 0 : ZMQ_SNDMORE)) return false;
  };

  return true;
};

template <typename Hit>
bool VMEReadout<Hit>::Receive(zmq::socket_t* socket) {
  std::deque<Event> readout;
  while (true) {
    zmq::message_t packet;
    if (!socket->recv(&packet)) return false;

    Time time;
    if (packet.size() == sizeof(Time)) {
      memcpy(&time, packet.data(), sizeof(Time));
      if (!socket->recv(&packet)) return false;
    };

    std::vector<Hit> hits(packet.size() / sizeof(Hit));
    memcpy(hits.data(), packet.data(), packet.size());
    readout.push_back({ time, std::move(hits) });

    if (!packet.more()) break;
  };

  std::lock_guard<std::mutex> lock(mutex);
  for (auto& event : readout) this->readout.push_back(std::move(event));

  return true;
};
#endif
