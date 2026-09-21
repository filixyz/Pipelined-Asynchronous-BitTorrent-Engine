#ifndef TRACKER_MAN
#define TRACKER_MAN

#include "HTTPHandler.hpp"
#include "UDPHandler.hpp"
#include "TorrentFile.hpp"
#include <atomic>
#include <cstdint>
#include <ev++.h>
#include <unordered_map>
#include <chrono>

enum class tracker_state_t: std::uint8_t {
  null,
  inactive,
  active,
};

enum class tracker_proto_t: std::uint8_t {
  null,
  http,
  udp,
};

enum class timer_count: std::uint8_t {
  one,
  two,
};

struct tracker_context_t {
  bool online {false};
  bool interruptible {false};
  bool has_http {false}; // udp failsafe
  std::size_t failures {0};
  std::size_t manager_space_idx{0};

  inline void reset() {
    online = false;
    interruptible = false;
    failures = 0;
    manager_space_idx = 0;
  };

};

struct tracker_timers_t {
  int minimum_duration{0};
  ev::timer minimum {};
  int maximum_duration{0};
  ev::timer maximum {};
  timer_count type {timer_count::one};

  inline void reset() {
    minimum_duration = 0;
    maximum_duration = 0;
    type = timer_count::one;
    minimum.stop();
    maximum.stop();
  }

};

struct announce_list_t {
  std::vector<std::string> list;
  std::string domain_name;
  std::size_t current_idx {0};
  std::size_t failed_idx {0};
  bool just_failed{false};

  inline void reset() {
    failed_idx = 0;
    current_idx = 0;
    failed_idx = 0;
    just_failed=false;
  }
};

enum class tracker_event:std::size_t {
  started=0,
  stopped=1,
  completed=2,
  update=3,
};

struct manager_context_t {
  std::size_t uploaded{0};
  std::size_t downloaded{0};
  std::size_t left{};
  tracker_event event {tracker_event::update};
  int port;
  int compact{1};
  std::string escaped_info_hash_byte;
  bool active = false;
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

enum manager_event : std::uint8_t {
  start_mask             = std::uint8_t{1}<<0,
  reannounce_mask        = std::uint8_t{1}<<1,
  force_reannounce_mask  = std::uint8_t{1}<<2,
  shutdown_mask          = std::uint8_t{1}<<3,
};

struct event_system_t {
  ev::dynamic_loop loop;
  ev::async signal;
  std::atomic<std::uint8_t> set {0};
  event_system_t(int x ) : loop(x){}
};

class TrackerManager {

  class Tracker;

  struct protocol_handle_t {
    HTTPHandler http;
    UDPHandler udp;
    protocol_handle_t(ev::dynamic_loop&);
    void add_request(Tracker*);
  };

  static constexpr std::chrono::seconds startup_window {30};
  using clock           = std::chrono::steady_clock;
  using tracker_store_t = std::unordered_map<std::string, TrackerManager::Tracker>;
  using manager_space_t = std::vector<Tracker*>;

private:

  std::chrono::steady_clock::time_point started_tp {};

  event_system_t event;
  protocol_handle_t protocol;
  manager_context_t tracker_context;
  tracker_store_t tracker_connections;
  manager_space_t manager_space;

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

  void handle_event();
  void wait_for_event();

  void start_event();
  void dormant_event();
  void reannounce_event();
  void force_reannounce_event();
  void shutdown_event();

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
};

class TrackerManager::Tracker: public HTTPRequest {

  TrackerManager& manager;
  std::string tracker_id;
  tracker_state_t state = tracker_state_t::null;
  tracker_proto_t current_proto = tracker_proto_t::null;
  tracker_context_t context;
  tracker_timers_t timers;
  announce_list_t announce_url;

  void do_on_success() override;
  void do_on_failure() override;

  void active_state_handler(ben::dic& parsed);
  void inactive_state_handler();

  void send_to_protocol_space();
  void return_to_manager_space();

  inline void shutdown_reset() {
    state = tracker_state_t::null;
    current_proto = tracker_proto_t::null;
    context.reset();
    timers.reset();
    announce_url.reset();
  }

  bool seek_to_next_url();

  friend TrackerManager;

public:

  ~Tracker()=default;
  using HTTPRequest::HTTPRequest;

public:

  Tracker (TrackerManager&);
  std::string get_url();

};

#endif
