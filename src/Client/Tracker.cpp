#include "ThreadMessageTypes.hpp"
#include "TrackerManager.hpp"
#include "../Bencoder/Bencode.hpp"
#include <arpa/inet.h>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <netinet/in.h>
#include <span>
#include <sstream>
#include <string>
#include <sys/socket.h>

TrackerManager::Tracker::Tracker (TrackerManager& __manager) : HTTPRequest(), manager(__manager) {};

void TrackerManager::Tracker::active_state_handler(bendecoded::dictionary& parse) {

  state = tracker_state_t::active;

  { // reset failure stats
    context.failures = 0;
    announce_url.failed_idx = -1;
    announce_url.just_failed = false;
  }

  if (parse.contains("warning message")) {
    // log here
  }

  if ( timers.type = timer_count::one; auto interval_value = find("interval", parse) ) {
    auto& interval = *interval_value;
    timers.maximum_duration = interval.get_as<bendecoded::integer>();
    if (timers.maximum_duration <= 0) timers.maximum_duration = std::int64_t{1800};
  }

  if (auto min_interval_value = find("min interval", parse)) {
    auto& min_interval = *min_interval_value;
    timers.minimum_duration = min_interval.get_as<bendecoded::integer>();
    if (timers.minimum_duration > 0)  timers.type = timer_count::two;
  }

  if (auto tracker_id_value = find("tracker id", parse)) {
    auto& tracker_id = *tracker_id_value;
    tracker_id = tracker_id.get_as<bendecoded::string>();
  }

{

  static constexpr std::size_t  peer4_unit_length = 6;
  static constexpr std::size_t  peer4_addr_length = 4;
  static constexpr std::size_t  port_length       = 2;
  static constexpr std::int64_t max_port          = std::numeric_limits<std::uint16_t>::max();

  bool notify_consumer = false;

  if (!parse.contains("peers")) return;

  auto& parsed_peers = parse["peers"];

  if (parsed_peers.type() == bencode_type::string) {

    std::span<std::byte> peer_binaries = std::as_writable_bytes (std::span(
      parsed_peers.get_as<bendecoded::string>()
    ));

    if (peer_binaries.empty() or peer_binaries.size() % peer4_unit_length != 0) return;

    while (!peer_binaries.empty()) {

      peer_contact new_endpoint; new_endpoint.is_v6 = false;

      auto current_addr = peer_binaries.first(peer4_unit_length);
      std::memcpy(new_endpoint.n_addr.data(), current_addr.data(), peer4_addr_length);
      std::memcpy(&new_endpoint.n_port, current_addr.data() + peer4_addr_length, port_length);

      peer_binaries = peer_binaries.subspan(peer4_unit_length);

      //std::cout << Hasher::hex_stringify_hash(std::as_bytes(current_addr)) << " bytes retrieved from tracker ";

      // This enqueue is lossy. if cons½umers queue is full the current address being enqueued is lost if peer_binaries
      // is not empty and the the queue has been drained somewhat the current address will successfully push

      if ( manager.discoveries.queue.push(new_endpoint) && !notify_consumer)
        notify_consumer = true;
    }
  }

  else if (parsed_peers.type() == bencode_type::list) {

    auto& peer_list = parsed_peers.get_as<bendecoded::list>();

    for (auto& peer : peer_list) {

      auto& peer_map = peer.get_as<bendecoded::dictionary>();

      peer_contact new_endpoint;

      if ( auto found_value = find( "peer id", peer_map ) ) {

        std::string& peer_id = found_value->get_as<bendecoded::string>();
        if (peer_id.size() == 20) {
          new_endpoint.peer_id.emplace();
          std::memcpy(new_endpoint.peer_id.value().data(), peer_id.data(), 20);
        }

      }

      if ( auto found_value = find( "ip", peer_map )) {

        std::string& peer_addr = found_value->get_as<bendecoded::string>();
        if (inet_pton(AF_INET, peer_addr.c_str(), &new_endpoint.n_addr) == 1) {
          new_endpoint.is_v6 = false;
        } else if (inet_pton(AF_INET6, peer_addr.c_str(), &new_endpoint.n_addr) == 1) {
          new_endpoint.is_v6 = true;
        } else
          continue; // I choose not to allow my implementation perform dns resolves for dns name responses

      } else continue;

      if ( auto found_value = find( "port", peer_map) ) {

        std::int64_t& peer_port = found_value->get_as<bendecoded::integer>();

        if (peer_port < 1 || peer_port > max_port) continue;

        new_endpoint.n_port = htons(static_cast<std::uint16_t>(peer_port));

      } else continue;

      // This enqueue is lossy. if cons½umers queue is full the current address being enqueued is lost if peer_binaries
      // is not empty and the the queue has been drained somewhat the current address will successfully push

      if ( manager.discoveries.queue.push(new_endpoint) && !notify_consumer)
        notify_consumer = true;
    }

  }

  if (notify_consumer) manager.discoveries.consumer.send();

}

  arm_timer(timers.maximum, timers.maximum_duration);

  if (timers.type == timer_count::two) {
    arm_timer(timers.minimum, timers.minimum_duration);
    context.interruptible=false;
  }

  if (manager.tracker_context.active) {
    return_to_manager_space();
  } else
    shutdown_reset();

}

static constexpr double retry_for_new_url = 5.0;

void TrackerManager::Tracker::inactive_state_handler() {

  state = tracker_state_t::inactive;
  timers.type = timer_count::one;

  // cache failed idx for wraparound check
  if (!announce_url.just_failed) {
    announce_url.failed_idx = announce_url.current_idx;
    announce_url.just_failed = true;
  }

  // cycle to next http url
  bool url_cycle_exhausted; do
    url_cycle_exhausted = seek_to_next_url();
  while ( current_proto != tracker_proto_t::http ); // udp failsafe

  if ( url_cycle_exhausted ) {
    context.failures++;
    timers.maximum_duration = get_retry_seconds(this);
  } else {
    timers.maximum_duration = retry_for_new_url;
  }

  arm_timer(timers.maximum, timers.maximum_duration);

  if (manager.tracker_context.active) {
    return_to_manager_space();
  } else {
    shutdown_reset();
  }

}

void TrackerManager::Tracker::do_on_success() {
  context.online = true;

  if (user_space.data.empty()) {
    inactive_state_handler();
    return;
  }

  std::istringstream bencoded_response(user_space.data);
  auto parse = bendecode(bencoded_response);
  if (!parse) {
    //manager.tracker_connections.erase(announce_url.domain_name);
    std::cout << "failed to parse " << user_space.data << '\n';
    return;
  }

  bendecoded::dictionary& parsed_dict = parse.value().get_as<bendecoded::dictionary>();

  if (parsed_dict.contains("failure reason"))  {
    // log here.
    inactive_state_handler();
    return;
  }

  active_state_handler(parsed_dict);

}

void TrackerManager::Tracker::do_on_failure() {
  context.online = false;
  inactive_state_handler();
}

void TrackerManager::Tracker::return_to_manager_space() {
  context.manager_space_idx = manager.manager_space.size();
  manager.manager_space.push_back(this);
}

void TrackerManager::Tracker::send_to_protocol_space() {
  Tracker* last = manager.manager_space.back();
  last->context.manager_space_idx = context.manager_space_idx;
  manager.manager_space[context.manager_space_idx] = last;
  manager.manager_space.pop_back();
}

std::string TrackerManager::Tracker::get_url() {
  return announce_url.list[announce_url.current_idx];
}

bool TrackerManager::Tracker::seek_to_next_url() {

  announce_url.current_idx = announce_url.current_idx + 1;
  auto& urls  = announce_url.list;

  if ( announce_url.current_idx >= urls.size() )
    announce_url.current_idx=0;

  current_proto = urls[announce_url.current_idx].starts_with("http") ?
    tracker_proto_t::http :
    tracker_proto_t::udp;

  if (announce_url.failed_idx == announce_url.current_idx)
    return true;
  return false;

}
