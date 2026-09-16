#ifndef TRACKER_MAN
#define TRACKER_MAN

#include "HTTPHandler.hpp"
#include "UDPHandler.hpp"
#include "TorrentFile.hpp"
#include <cstdint>
#include <ev++.h>
#include <unordered_map>
#include <chrono>

enum class tracker_state_t: std::uint8_t {
  null,
  inactive,
  active,
};

enum class tracker_proto_t {
  null,
  http,
  udp,
};

enum class timer_count {
  one,
  two,
};

struct tracker_context_t {
  bool interruptible {false};
  bool requeable_permission {false};
  bool has_http {false}; // udp failsafe
  std::size_t failures {0};
  std::size_t manager_space_idx{0};
};

struct tracker_timers_t {
  int minimum_duration{0};
  ev::timer minimum {};
  int maximum_duration{0};
  ev::timer maximum {};
  timer_count type {timer_count::one};
};

struct announce_list_t {
  std::vector<std::string> list;
  std::size_t current_idx;
  std::size_t failed_idx;
};

enum class tracker_event:std::size_t {
  started=0,
  stopped=1,
  completed=2,
  update=3,
};

struct manager_context_t {
  std::uint64_t uploaded{0};
  std::uint64_t downloaded{0};
  std::uint64_t left{};
  tracker_event event {tracker_event::update};
  int port;
  int compact{1};
  std::string escaped_info_hash_byte;
  bool running=true;
};


const std::array<std::string, 4> event_strings {
  "&event=started",
  "&event=stopped",
  "&event=completed",
  ""
};

struct event_signal_t {
  ev::io watcher;
  int fd;
};

class TrackerManager {

  class Tracker;

  struct protocol_handle_t {
    const HTTPHandler http;
    UDPHandler udp;
    protocol_handle_t(ev::dynamic_loop&);
    void add_request(Tracker*) const;
  };

  static constexpr std::chrono::seconds startup_window {30};
  using clock = std::chrono::steady_clock;
  using tracker_store_t = std::unordered_map<std::string, TrackerManager::Tracker>;
  using manager_space_t = std::vector<Tracker*>;

private:

  std::chrono::steady_clock::time_point started_tp {};

  ev::dynamic_loop event_loop;

  event_signal_t event_signal;
  protocol_handle_t protocol;
  manager_context_t tracker_context;
  tracker_store_t tracker_connections;
  manager_space_t manager_space;

  void (TrackerManager::*current_state)();

  void initialize_info_hash_byte(TorrentFile&);
  void initialize_tracker_context(TorrentFile&);
  void initiatlize_trackers(std::vector<std::string_view>);
  int  initialize_libev();
  void initialize_state_system();

  void populate_manager_space();

  void static arm_timer(ev::timer&, double);
  void static disarm_timer(ev::timer&);
  int  static get_retry_seconds(const Tracker*);
  void static tracker_timeout_handler(ev::timer& timer, int revents);
  void state_change_handler(ev::io&, int revents);
  void block_until_ready_events_then_handle_for_transition();

  void start_state();
  void normal_state();
  void reannounce_state();
  void force_reannounce_state();
  void shutdown_state();
  void inactive_state();

  std::string get_request_params() const;
  void set_announce_url_for_tracker(Tracker&, tracker_event) const;

  void send_requests();
  void parse_responses();
  void feed_peer_manager();

public:
  TrackerManager(TorrentFile&, int port);
  void test(int);
  void start_tracker_manager();
  void start();
  void reannounce();
  void force_reannounce();
  void shutdown();
  void update_context(std::size_t, std::size_t);
  void scrape_trackers();
};

class TrackerManager::Tracker: public HTTPRequest {

  TrackerManager& manager;
  std::string tracker_id;
  tracker_state_t state = tracker_state_t::null;
  tracker_proto_t current_proto = tracker_proto_t::null;
  tracker_context_t context;
  tracker_timers_t timers;
  announce_list_t announce_urls;

  void do_on_success() override;
  void do_on_failure() override;

  void send_to_protocol_space();
  void return_to_manager_space();

  void seek_to_next_url();
  friend TrackerManager;

public:

  ~Tracker()=default;
  using HTTPRequest::HTTPRequest;

public:

  Tracker (TrackerManager&);
  std::string get_url();
};

#endif
