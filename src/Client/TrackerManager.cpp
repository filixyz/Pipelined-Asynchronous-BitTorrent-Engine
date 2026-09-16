#include "TrackerManager.hpp"
#include "Constants.hpp"
#include "HTTPHandler.hpp"
#include "Hasher.hpp"
#include "TorrentFile.hpp"
#include <array>
#include <cassert>
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

void TrackerManager::protocol_handle_t::add_request(Tracker* trkr) const {
 if(trkr->current_proto == tracker_proto_t::http)
   http.add_request(trkr);
 else  {
   // not implemented.
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
    if(url.starts_with("http")) trkr.context.has_http = true;
  };

  for(auto url : trackers_urls) {
    auto key = get_hostname(url);
    if (tracker_connections.contains(key)) {
      tracker_connections[key].announce_urls.list.push_back(std::string(url));
      tag_http_presence(url, tracker_connections[key]);
      continue;
    }
    auto& current = ( tracker_connections.emplace(key, *this) ).first->second;
    tag_http_presence(url, tracker_connections[key]);
    current.timers.maximum.set(event_loop);
    current.timers.minimum.set(event_loop);
  }
}

void TrackerManager::populate_manager_space() {

  std::cout << "populate_manager_space domain\n";

  for(auto& pair: tracker_connections) {
    auto& trkr = pair.second;

    if (!trkr.context.has_http) {   continue;  }
    while (trkr.current_proto != tracker_proto_t::http)
      trkr.seek_to_next_url();

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
  while((event_signal.fd = eventfd(0, EFD_CLOEXEC|EFD_NONBLOCK))==-1); // possible bug
  event_signal.watcher.set(event_signal.fd, ev::READ);
  event_signal.watcher.set(event_loop);
  event_signal.watcher.set <TrackerManager, &TrackerManager::state_change_handler> (this);
  event_signal.watcher.start();
}

TrackerManager::TrackerManager(TorrentFile& torrent_, int port)
  : event_loop(initialize_libev()) ,protocol(event_loop), tracker_context() {
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

  const auto event = (
    clock::now() - tracker.manager.started_tp < startup_window &&
    tracker.state == tracker_state_t::inactive
  )
    ? tracker_event::started
    : tracker.manager.tracker_context.event;

  tracker.manager.set_announce_url_for_tracker(tracker, event);
  tracker.send_to_protocol_space();
  tracker.manager.protocol.add_request(&tracker);

}

inline void TrackerManager::block_until_ready_events_then_handle_for_transition() {
  event_loop.run(ev::ONCE);
}

void TrackerManager::state_change_handler(ev::io& watcher, int revents) {
  (void) watcher; (void) revents;

  uint64_t buffer;
  eventfd_read(event_signal.fd, &buffer);

}

void TrackerManager::start_state() {

  populate_manager_space();

  for(auto __tracker : manager_space) {
    auto& tracker = * __tracker;
    tracker.context.requeable_permission = true;

    if(tracker.state == tracker_state_t::null) {
      tracker.state = tracker_state_t::inactive;
      tracker.timers.maximum.set<&TrackerManager::tracker_timeout_handler> ();
      tracker.timers.minimum.set<&TrackerManager::tracker_timeout_handler> ();
      tracker.timers.maximum.data = &tracker;
      tracker.timers.minimum.data = &tracker;
      arm_timer(tracker.timers.maximum, 0.0);
      continue;
    }

    arm_timer(tracker.timers.maximum, tracker.timers.maximum_duration);
    if(tracker.timers.type == timer_count::two)
      arm_timer(tracker.timers.minimum, tracker.timers.minimum_duration);
  }

  current_state=&TrackerManager::normal_state;
}

void TrackerManager::normal_state() {
  block_until_ready_events_then_handle_for_transition();
}

void TrackerManager::reannounce_state() {
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
      set_announce_url_for_tracker(*trkr, tracker_event::update);
      trkr->send_to_protocol_space();
      protocol.add_request(trkr);
    }

  }
  current_state=&TrackerManager::normal_state;
}

void TrackerManager::force_reannounce_state() {
  for(Tracker* trkr : manager_space) {
    auto& tracker = * trkr;
    if(tracker.state == tracker_state_t::active) {
      disarm_timer(tracker.timers.minimum);
      disarm_timer(tracker.timers.maximum);
      set_announce_url_for_tracker(*trkr, tracker_event::update);
      trkr->send_to_protocol_space();
      protocol.add_request(trkr);
    }
  }
  current_state=&TrackerManager::normal_state;
}

void TrackerManager::shutdown_state() {
  for( Tracker* trkr : manager_space) {
    auto& tracker = * trkr;

    disarm_timer(tracker.timers.maximum);
    tracker.context.requeable_permission = false;
    tracker.send_to_protocol_space();

    if (tracker.state == tracker_state_t::inactive)
      continue;

    if(tracker.timers.type == timer_count::two)
      disarm_timer(tracker.timers.minimum);

    tracker_event current_ev = tracker_context.left==0 ? tracker_event::completed : tracker_event::stopped;
    set_announce_url_for_tracker(*trkr, current_ev);
    protocol.add_request(trkr);
  }

  if (manager_space.empty())  current_state=&TrackerManager::inactive_state;
}

void TrackerManager::inactive_state() {
  ;
}

void TrackerManager::start_tracker_manager() {
  std::cout << "tracker service is online\n";
  current_state = &TrackerManager::inactive_state;
  while(tracker_context.running) {
    (this->*current_state)();
  }
  std::cout << "tracker service is offline\n";
};

void TrackerManager::start() {
  if(current_state!=&TrackerManager::inactive_state)
    return;
  std::cout << "Starting sequence initiated\n";
  current_state=&TrackerManager::start_state;
  eventfd_write(event_signal.fd, 1);
}

void TrackerManager::reannounce() {
  if(current_state!=&TrackerManager::normal_state)
    return;
  current_state=&TrackerManager::reannounce_state;
  eventfd_write(event_signal.fd, 1);
}

void TrackerManager::force_reannounce() {
  if(current_state!=&TrackerManager::normal_state)
    return;
  current_state=&TrackerManager::force_reannounce_state;
  eventfd_write(event_signal.fd, 1);
}

void TrackerManager::shutdown() {
  if(current_state!=&TrackerManager::normal_state)
    return;
  current_state=&TrackerManager::shutdown_state;
  eventfd_write(event_signal.fd, 1);
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
