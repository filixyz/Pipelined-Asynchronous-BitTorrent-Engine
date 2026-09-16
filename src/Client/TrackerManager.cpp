#include "TrackerManager.hpp"
#include "Constants.hpp"
#include "HTTPHandler.hpp"
#include "Hasher.hpp"
#include "PeerManager.hpp"
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
 if(trkr->http_mode)
  http.add_request(trkr);
 else
// udp.add_request(trkr);
  ;
}

std::string get_hostname(std::string_view url) {
  int hostname_start_index = url.find("://") + 3;
  int port_character_index = url.find_last_of(":");
  int hostname_end_index   = (port_character_index<hostname_start_index) ? url.find_last_of('/') : port_character_index;
  return std::string(url.substr(hostname_start_index, hostname_end_index-hostname_start_index));
}

void TrackerManager::initiatlize_trackers(std::vector<std::string_view> trackers_urls) {
  auto tag_http_presence = [](std::string_view url, Tracker& trkr) {
    if(url.starts_with("http")) trkr.nest.bool_set.has_http = true;
  };

  for(auto url : trackers_urls) {
    auto key = get_hostname(url);
    if (tracker_connections.contains(key)) {
      tracker_connections[key].nest.announce_urls.push_back(std::string(url));
      tag_http_presence(url, tracker_connections[key]);
      continue;
    }
    auto& current = ( tracker_connections.emplace(key, std::string(url)) ).first->second;
    tag_http_presence(url, tracker_connections[key]);
    current.nest.time_set.min_timer_w.set(event_loop);
    current.nest.time_set.timer_w.set(event_loop);
  }
}


void TrackerManager::populate_manager_space() {

  std::cout << "populate_manager_space domain\n";

  for(auto& pair: tracker_connections) {
    auto& trkr = pair.second;
    trkr.nest.manager = this;

    // std::this_thread::sleep_for(std::chrono::milliseconds{100});
    // std::cout << "new tracker\t";
    // std::cout << trkr.get_url();

    // size_t remain = size_t{55} - trkr.get_url().length() - size_t{1};
    // for (size_t i=0; i != remain; ++i)
    //   std::cout << ' ';

    if (trkr.nest.bool_set.has_http==false) {                   // failsafe against upd protocol (remove if udp protocol implemented)
      --trkrs_in_trkrspace; continue;                           // failsafe against upd protocol (remove if udp protocol implemented)
    }                                                           // failsafe against upd protocol (remove if udp protocol implemented)
    while (trkr.http_mode==false) trkr.seek_to_next_url();      // failsafe against udp protocol (remove if udp protocol implemented)
    //std::cout << "\n\tseek to http url successful -> " << trkr.get_url() << '\n';
    manager_space.push_back(&trkr);
    //std::cout << "\tpushed to manager space\n";
    trkr.nest.queue_ptr = &manager_space.back();
    //std::cout << "\ttrackers queue ptr stored successfully within itself\n";
  }
  manager_space.shrink_to_fit();
}

void TrackerManager::initialize_info_hash_byte() {
  info_hash_byte = Hasher::byte_stringify_hash(torrent.get_info_hash_bytes());
}

void TrackerManager::initialize_tracker_context() {
  tracker_context.left = torrent.get_download_size();
}

int TrackerManager::initialize_libev() {
  return ev::recommended_backends();
}

void TrackerManager::initialize_state_system() {
  while((state_change_signal_fd = eventfd(0, EFD_CLOEXEC|EFD_NONBLOCK))==-1); // possible bug
  state_event_wtc.set(state_change_signal_fd, ev::READ);
  state_event_wtc.set(event_loop);
  state_event_wtc.set <TrackerManager, &TrackerManager::state_change_handler> (this);
  state_event_wtc.start();
}

TrackerManager::TrackerManager(TorrentFile& torrent_, PeerManager& peer_manager_)
  : event_loop(initialize_libev()) ,protocol(event_loop), torrent(torrent_), tracker_context(), peer_manager(peer_manager_) {
  initialize_info_hash_byte();
  initialize_tracker_context();
  initiatlize_trackers(torrent.get_tracker_urls());
  listening_port = peer_manager.get_listening_port();
  initialize_state_system();
  protocol.http.start_backend();
  std::cout << "\nTorrent is file: " << torrent.torrent_is_file() << " Torrent size: " << torrent.get_download_size() << '\n';
  std::cout <<" Tracker context: Downloaded: " << tracker_context.downloaded << "  left " << tracker_context.left << " uploaded " << tracker_context.uploaded << " compact: "
    << tracker_context.compact <<'\n';
  std::cout << info_hash_byte << '\n';
  std::cout << event_strings[2] << '\n';
}

std::string TrackerManager::get_request_params() const
{
  auto make_param = [](std::string_view key, std::string_view value) {
    return std::string(key).append(1, '=').append(value);
  };
  std::string info_hash_byte_escaped_copy = info_hash_byte;
  auto& client_id = bprotocol::constants::client_id;
  HTTPHandler::escape_byte_string(info_hash_byte_escaped_copy);
  std::array<std::string, 8> params{
    make_param("info_hash", info_hash_byte_escaped_copy),
    make_param("peer_id", std::string_view(client_id.data(), client_id.size())),
    make_param("port", std::to_string(listening_port)),
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
  std::string tracker_id = trkr.nest.tracker_id.empty() ? "" : std::string{'&'}.append("trackerid=").append(trkr.nest.tracker_id);
  std::string request_url = trkr.get_url() + '?' + get_request_params() + tracker_id + event_strings[static_cast<size_t>(event)];
  trkr.user_space.url = std::move(request_url);
}

int TrackerManager::get_retry_seconds(const Tracker* trkr) {
  constexpr int minute = 60;
  std::array<int, 5> retry_intervals {10, 30, 5*minute, 15*minute, 45*minute};
  if(trkr->nest.failure_count==0)
    return 0;
  return retry_intervals[trkr->nest.failure_count-1 %5];
}

void TrackerManager::arm_timer(ev::timer& timer, double duration) {
  timer.set(duration);
  timer.start();
}

void TrackerManager::disarm_timer(ev::timer& timer) {
  timer.stop();
}

void TrackerManager::tracker_timeout_handler(ev::timer& timer, int revents) {
  (void)revents;
  timer.stop();
  Tracker* trkr = reinterpret_cast<Tracker*>(timer.data);
  if(!trkr->nest.bool_set.only_one_timer && !trkr->nest.bool_set.interruptible) {
    trkr->nest.bool_set.interruptible=true;
    return;
  }
  if(!trkr->nest.bool_set.update_state) {
    trkr->nest.manager->set_announce_url_for_tracker(*trkr, tracker_event::started);
  }
  else
    trkr->nest.manager->set_announce_url_for_tracker(*trkr, tracker_event::update);
  trkr->send_to_protocol_space();
  trkr->nest.manager->protocol.add_request(trkr);
}

inline void TrackerManager::block_until_ready_events_then_handle_for_transition() {
  event_loop.run(ev::ONCE);
}

void TrackerManager::state_change_handler(ev::io& watcher, int revents) {
  (void) watcher; (void) revents;
  uint64_t buffer;
  eventfd_read(state_change_signal_fd, &buffer);
}

void TrackerManager::start_state() {
  std::cout<< "Starting state\n";
  populate_manager_space();
  std::cout << "manager space populated\n";    // CHECKPOINT: VALID
  for(auto trkr : manager_space) {
    trkr->nest.bool_set.requeueable = true;
    if(trkr->nest.time_set.interval==-1 && trkr->nest.time_set.min_interval==-1) { // first time tracker will be queued
      trkr->nest.trkrs_in_trkrspace_ref = &trkrs_in_trkrspace;
      trkr->nest.bool_set.only_one_timer = true;
      trkr->nest.time_set.interval=0;
      trkr->nest.time_set.timer_w.set<&TrackerManager::tracker_timeout_handler> ();
      trkr->nest.time_set.min_timer_w.set<&TrackerManager::tracker_timeout_handler> ();
      trkr->nest.time_set.timer_w.data=trkr;
      trkr->nest.time_set.min_timer_w.data=trkr;  // CHECKPOINT: VALID : BUG FIXED.
      arm_timer(trkr->nest.time_set.timer_w, 0.0);  // timer initialized to expire immediately
      continue;
    }
    arm_timer(trkr->nest.time_set.timer_w, trkr->nest.time_set.interval);
    if(trkr->nest.bool_set.only_one_timer==false)
      arm_timer(trkr->nest.time_set.min_timer_w, trkr->nest.time_set.min_interval);
  }
  std::cout<< "Starting ended\n";
  current_state=&TrackerManager::normal_state;
}

void TrackerManager::normal_state() {
  std::cout << "Normal state\n";
  block_until_ready_events_then_handle_for_transition();
}

void TrackerManager::reannounce_state() {
  for(Tracker* trkr : manager_space) {
    if (trkr != nullptr && !trkr->nest.bool_set.only_one_timer && trkr->nest.bool_set.interruptible && trkr->nest.bool_set.update_state) {
      // since we are doing what the normal interval would had done we should disarm the main
      // interval timer. so epoll doesnt wake on it
      disarm_timer(trkr->nest.time_set.timer_w);
      // then handle non interrupt reannounce
      set_announce_url_for_tracker(*trkr, tracker_event::update);
      //request_t current_request {trkr->nest.net_set.connection, trkr, do_on_success_cllbk(), do_on_failure_cllbk()};
      trkr->send_to_protocol_space(); // same logic is dequeing implement later
      protocol.add_request(trkr);
    }
  }
  current_state=&TrackerManager::normal_state;
}

void TrackerManager::force_reannounce_state() {
  for(Tracker* trkr : manager_space) {
    if(trkr != nullptr && trkr->nest.bool_set.update_state) {
      disarm_timer(trkr->nest.time_set.timer_w);
      disarm_timer(trkr->nest.time_set.min_timer_w);
      set_announce_url_for_tracker(*trkr, tracker_event::update);
      trkr->send_to_protocol_space();
      protocol.add_request(trkr);
    }
  }
  current_state=&TrackerManager::normal_state;
}

void TrackerManager::shutdown_state() {
  for(Tracker* trkr : manager_space) {
    if (trkr == nullptr)
      continue;

    disarm_timer(trkr->nest.time_set.timer_w);
    trkr->nest.bool_set.requeueable=false;
    trkr->send_to_protocol_space();

    if(!trkr->nest.bool_set.update_state)
      continue;

    if(!trkr->nest.bool_set.only_one_timer)
      disarm_timer(trkr->nest.time_set.min_timer_w);

    tracker_event current_ev = tracker_context.left==0 ? tracker_event::completed : tracker_event::stopped;
    set_announce_url_for_tracker(*trkr, current_ev);
    protocol.add_request(trkr);
  }
  // implement condition that should hold before tracker can then
  // transition into an inactive state
  // Subtracting protocol_queue.udp.size() because since i have not implemented
  // any machanics that handles upd trackers they will always remain in
  // the tracker space (untouched).
  if (trkrs_in_trkrspace==0)
    current_state=&TrackerManager::inactive_state;
}

void TrackerManager::inactive_state() {
  ;
}

void TrackerManager::start_tracker_manager() {
  std::cout << "tracker service is online\n";
  current_state = &TrackerManager::inactive_state;
  while(running) {
    (this->*current_state)();
  }
  std::cout << "tracker service is offline\n";
};

void TrackerManager::start() {
  if(current_state!=&TrackerManager::inactive_state)
    return;
  std::cout << "Starting sequence initiated\n";
  current_state=&TrackerManager::start_state;
  eventfd_write(state_change_signal_fd, 1);
}
void TrackerManager::reannounce() {
  if(current_state!=&TrackerManager::normal_state)
    return;
  current_state=&TrackerManager::reannounce_state;
  eventfd_write(state_change_signal_fd, 1);
}
void TrackerManager::force_reannounce() {
  if(current_state!=&TrackerManager::normal_state)
    return;
  current_state=&TrackerManager::force_reannounce_state;
  eventfd_write(state_change_signal_fd, 1);
}
void TrackerManager::shutdown() {
  if(current_state!=&TrackerManager::normal_state)
    return;
  current_state=&TrackerManager::shutdown_state;
  eventfd_write(state_change_signal_fd, 1);
}

void TrackerManager::update_context(unsigned dwn, unsigned upd) {
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
