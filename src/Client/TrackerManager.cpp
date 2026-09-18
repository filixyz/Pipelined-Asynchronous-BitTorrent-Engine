#include "TrackerManager.hpp"
#include "Constants.hpp"
#include "HTTPHandler.hpp"
#include "Hasher.hpp"
#include "TorrentFile.hpp"
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <curl/curl.h>
#include <curl/multi.h>
#include <ev++.h>
#include <string>
#include <string_view>
#include <sys/time.h>
#include <unistd.h>
#include <utility>
#include <vector>
#include <sys/eventfd.h>

TrackerManager::protocol_handle_t::protocol_handle_t(ev::dynamic_loop& ev_loop) : http(ev_loop)
{}

void TrackerManager::protocol_handle_t::add_request(Tracker* trkr) {

  switch (trkr->current_proto) {

  case tracker_proto_t::http :
    http.add_request(trkr);                   break;
  case tracker_proto_t::udp :
    { /* not implemented */ }                 break;
  default:
    {}

  }

}

std::string get_hostname(std::string_view url) {
  int hostname_start_index = url.find("://") + 3;
  int port_character_index = url.find_last_of(":");
  int hostname_end_index   = (port_character_index<hostname_start_index) ? url.find_last_of('/') : port_character_index;
  return std::string(url.substr(hostname_start_index, hostname_end_index-hostname_start_index));
}

void TrackerManager::initiatlize_trackers(std::vector<std::string_view> trackers_urls) {

  auto tag_http_presence = [](std::string_view url, Tracker& trkr) {
    if (url.starts_with("http")) trkr.context.has_http = true;
  }; // udp failsafe delete lambda when udp backend implemented.

  for(auto url : trackers_urls) {
    auto key = get_hostname(url);
    auto [current_spot, fresh]  = tracker_connections.try_emplace(key, *this);
    auto& current               = current_spot->second;
    if (fresh) { // initializer should have its own seperat function
      current.announce_urls.domain_name = key;
      current.timers.maximum.set(event.loop);
      current.timers.minimum.set(event.loop);
      current.timers.maximum.set<&TrackerManager::tracker_timeout_handler> ();
      current.timers.minimum.set<&TrackerManager::tracker_timeout_handler> ();
      current.timers.maximum.data = &current;
      current.timers.minimum.data = &current;
    }
    tag_http_presence(url, current);
    auto& list = current.announce_urls.list;
    if (std::find(list.begin(), list.end(), url) == list.end()) {
      list.push_back(std::string(url));
    }
  }

}

void TrackerManager::populate_manager_space() {

  std::cout << "populate_manager_space domain\n";

  for(auto& pair: tracker_connections) {
    auto& trkr = pair.second;

    { // udp failsafe remove when udp backend implemented
      if (!trkr.context.has_http) {   continue;  }
      while (trkr.current_proto != tracker_proto_t::http)
        trkr.seek_to_next_url();
    }

    trkr.context.manager_space_idx = manager_space.size();
    manager_space.push_back(&trkr);
  }

  manager_space.shrink_to_fit();
}

void TrackerManager::initialize_info_hash_byte(TorrentFile& torrent) {
  tracker_context.escaped_info_hash_byte = Hasher::byte_stringify_hash(torrent.get_info_hash_bytes());
  HTTPHandler::escape_byte_string(tracker_context.escaped_info_hash_byte);
}

void TrackerManager::initialize_tracker_context(TorrentFile& torrent) {
  tracker_context.left = torrent.get_download_size();
}

int TrackerManager::initialize_libev() {
  return ev::recommended_backends();
}

void TrackerManager::initialize_state_system() {
  event.signal.set(event.loop);
  event.signal.set <TrackerManager, &TrackerManager::handle_event> (this);
  event.signal.start();
}

TrackerManager::TrackerManager(TorrentFile& torrent_, int port)
  : event(initialize_libev()) ,protocol(event.loop), tracker_context() {
  initialize_info_hash_byte(torrent_);
  initialize_tracker_context(torrent_);
  initiatlize_trackers(torrent_.get_tracker_urls());
  tracker_context.port = port;
  initialize_state_system();
  protocol.http.start_backend();
}

std::string TrackerManager::get_request_params() const
{
  auto make_param = [](std::string_view key, std::string_view value) {
    return std::string(key).append(1, '=').append(value);
  };

  auto& client_id = bprotocol::constants::client_id;

  std::array<std::string, 8> params{
    make_param("info_hash", tracker_context.escaped_info_hash_byte),
    make_param("peer_id", std::string_view(client_id.data(), client_id.size())),
    make_param("port", std::to_string(tracker_context.port)),
    make_param("uploaded", std::to_string(tracker_context.uploaded)),
    make_param("downloaded", std::to_string(tracker_context.downloaded)),
    make_param("left", std::to_string(tracker_context.left)),
    make_param("compact", std::to_string(tracker_context.compact)),
  };

  auto accumulate_params = [ampersand='&'](std::array<std::string, 8> params) {
    std::string loaded_paramaters;
    for(std::string& param : params) loaded_paramaters += (&param != &params[7]) ? std::move(param) + ampersand : std::move(param);
    loaded_paramaters.pop_back();
    return loaded_paramaters;
  };
  return accumulate_params(params);
}

void TrackerManager::set_announce_url_for_tracker(Tracker& trkr, tracker_event event) const {

  std::string tracker_id = trkr.tracker_id.empty() ?
    std::string{""} :
    std::string{'&'}.append("trackerid=").append(trkr.tracker_id);

  std::string request_url =
    trkr.get_url()                              +
    '?'                                         +
    get_request_params()                        +
    tracker_id                                  +
    event_strings[static_cast<size_t>(event)];

  trkr.user_space.url = std::move(request_url);
}

int TrackerManager::get_retry_seconds(const Tracker* trkr) {

  static constexpr int minute = 60;
  static std::array<int, 5> retry_intervals {10, 30, 5*minute, 15*minute, 45*minute};

  if(trkr->context.failures==0)
    return 0;
  return retry_intervals[trkr->context.failures-1 %5];

}

void TrackerManager::arm_timer(ev::timer& timer, double duration) {
  timer.set(duration);
  timer.start();
  std::cout << " :duration set "  << duration << '\n';
}

void TrackerManager::disarm_timer(ev::timer& timer) {
  timer.stop();
}

void TrackerManager::tracker_timeout_handler(ev::timer& timer, int revents) {
  (void)revents; timer.stop();

  Tracker& tracker = * reinterpret_cast<Tracker*>(timer.data);

  if (tracker.timers.type == timer_count::two && !tracker.context.interruptible) {
    tracker.context.interruptible = true;
    return;
  }

  const auto event = clock::now() - tracker.manager.started_tp < startup_window
    ? tracker_event::started
    : tracker.manager.tracker_context.event;

  tracker.manager.set_announce_url_for_tracker(tracker, event);
  tracker.send_to_protocol_space();
  tracker.manager.protocol.add_request(&tracker);
}

void TrackerManager::start_event() {

  populate_manager_space();

  for(auto __tracker : manager_space) {

    auto& tracker = * __tracker;

    assert(tracker.state == tracker_state_t::null);
    assert(tracker.timers.type == timer_count::one);

    tracker.state = tracker_state_t::inactive;
    arm_timer(tracker.timers.maximum, 0.1);

  }

  started_tp = clock::now();

}


void TrackerManager::reannounce_event() {

  for(Tracker* trkr : manager_space) {
    auto& tracker = * trkr;

    if
    ( tracker.timers.type == timer_count::two  &&
      tracker.context.interruptible            &&
      tracker.state == tracker_state_t::active
    ) {
      // since we are doing what the normal interval would had done we should disarm the main
      // interval timer. so epoll doesnt wake on it
      disarm_timer(tracker.timers.maximum);
      set_announce_url_for_tracker(tracker, tracker_context.event);
      tracker.send_to_protocol_space();
      protocol.add_request(trkr);
    }

  }

}

void TrackerManager::force_reannounce_event() {

  for(Tracker* trkr : manager_space) {
    auto& tracker = * trkr;
    if(tracker.state == tracker_state_t::active) {
      disarm_timer(tracker.timers.minimum);
      disarm_timer(tracker.timers.maximum);
      set_announce_url_for_tracker(tracker, tracker_context.event);
      tracker.send_to_protocol_space();
      protocol.add_request(trkr);
    }
  }

}

void TrackerManager::shutdown_event() {

  for( Tracker* trkr : manager_space) {
    auto& tracker = * trkr;

    disarm_timer(tracker.timers.maximum);
    tracker.send_to_protocol_space();

    if (tracker.state == tracker_state_t::inactive)
      continue;

    if(tracker.timers.type == timer_count::two)
      disarm_timer(tracker.timers.minimum);

    set_announce_url_for_tracker(tracker, tracker_context.event);
    protocol.add_request(trkr);
  }

}

inline void TrackerManager::wait_for_event() {
  event.loop.run();
}

void TrackerManager::handle_event() {

  std::uint8_t event_ = event.set.load(std::memory_order_acquire) ;
  std::uint8_t handled = 0;

  if ( event_ & start_mask && !tracker_context.active ) {
    start_event();
    tracker_context.active = true;
    handled |= start_mask;
  }

  if ( event_ & shutdown_mask && tracker_context.active ) {
    shutdown_event();
    tracker_context.active = false;
    handled |= shutdown_mask | reannounce_mask | force_reannounce_mask;
  }

  if ( tracker_context.active ) {

    if (event_ & force_reannounce_mask) {
      force_reannounce_event();
      handled |= force_reannounce_mask | reannounce_mask;
      event_ &= ~reannounce_mask;
    }

    if (event_ & reannounce_mask)  {
      reannounce_event();
      handled |= reannounce_mask;
    }

  }

  auto previous = event.set.fetch_and(~handled, std::memory_order_acq_rel);
  // incase when processing this (possibly) coalesced invocation
  // a new event arrives and libev only schedules coalesced event before
  // callback invocation
  if ( previous & ~handled )
    event.signal.feed_event(1);

}

void TrackerManager::dormant_event() {}

void TrackerManager::start_tracker_manager() {

  wait_for_event();

};

void TrackerManager::start() {

  event.set.fetch_or(start_mask, std::memory_order_acq_rel);
  event.signal.send();

}

void TrackerManager::reannounce() {

  event.set.fetch_or(reannounce_mask, std::memory_order_acq_rel);
  event.signal.send();

}

void TrackerManager::force_reannounce() {

  event.set.fetch_or(force_reannounce_mask, std::memory_order_acq_rel);
  event.signal.send();

}

void TrackerManager::shutdown() {

  event.set.fetch_or(shutdown_mask, std::memory_order_acq_rel);
  event.signal.send();

}

void TrackerManager::update_context(std::size_t dwn, std::size_t upd) {
  tracker_context.downloaded += dwn;
  tracker_context.uploaded   += upd;
  tracker_context.left       -= dwn;
}

void TrackerManager::test(int seconds) {
  std::cout << "== Test started ==\n";
  start_tracker_manager();
  std::cout << "== Test ended ==\n";
  return (void) seconds;
}
