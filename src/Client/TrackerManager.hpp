#ifndef TRACKER_MAN
#define TRACKER_MAN

#include "HTTPHandler.hpp"
#include "PeerManager.hpp"
#include "UDPHandler.hpp"
#include "TorrentFile.hpp"
#include <cstdint>
#include <ev++.h>
#include <unordered_map>

enum class tracker_state_t: std::uint8_t {
  null,
  inactive,
  active,
  failed
};

enum class tracker_timer_t: std::uint8_t {
  one_timer,
  two_timer,
};

enum class tracker_proto_t {
  http,
  udp,
};

struct tracker_bools_t {
  bool interruptible;
  bool requeable;
};

class TrackerManager {
  class Tracker;
  struct trkr_context_t {
    std::uint64_t uploaded{0};
    std::uint64_t downloaded{0};
    std::uint64_t left{};
    int compact{1};
  };
  struct protocol_handle_t {
    const HTTPHandler http;
    UDPHandler udp;
    protocol_handle_t(ev::dynamic_loop&);
    void add_request(Tracker*) const;
  };
  enum class tracker_event:short {
    started=0,
    stopped=1,
    completed=2,
    update=3,
  };
  const std::array<std::string, 4> event_strings {
    "&event=started", "&event=stopped", "&event=completed", ""
  };

  ev::dynamic_loop event_loop;
  ev::io state_event_wtc;
  int state_change_signal_fd;
  protocol_handle_t protocol;
  const TorrentFile& torrent;
  trkr_context_t tracker_context;
  std::unordered_map<std::string, TrackerManager::Tracker> tracker_connections;
  std::vector<Tracker*> manager_space;
  PeerManager& peer_manager;
  unsigned listening_port;
  std::string info_hash_byte;
  void (TrackerManager::*current_state)();
  bool running=true;
  int trkrs_in_trkrspace;

  void initialize_info_hash_byte();
  void initialize_tracker_context();
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

  // udp protocol failsafes; remove when udp protocol implemented

public:
  TrackerManager(TorrentFile&, PeerManager&);
  void test(int);
  void start_tracker_manager();
  void start();
  void reannounce();
  void force_reannounce();
  void shutdown();
  void update_context(unsigned, unsigned);
  void scrape_trackers();
};

class TrackerManager::Tracker: public HTTPRequest {
  struct time {
    int min_interval{-1};               int interval{-1};
    ev::timer min_timer_w{} ;           ev::timer timer_w{};
  };
  struct bools {
    // bool `has_http` is a UDP failafe (Tracker should be protocol agnostic)
    // remove (along with depenents) when UDP protocol mechanims is implemented
    bool has_http=false;                bool active_flag=true;
    bool update_state=false;            bool requeueable=true;
    bool only_one_timer=true;           bool interruptible=true;
  };
  struct data_nest_t {
    bools       bool_set;               time        time_set;
    std::string tracker_id;             unsigned    failure_count=0;
    Tracker**   queue_ptr;              std::vector<std::string> announce_urls;
    size_t current_announce_url_index;  int* trkrs_in_trkrspace_ref;
    ssize_t failed_url_index=-1;        const TrackerManager* manager;
  };
  void do_on_success() override;        void do_on_failure() override;
  void send_to_protocol_space();        void return_to_manager_space();
  void seek_to_next_url();              bool http_mode=false;

public:
  Tracker()=default; // changed this
  Tracker(Tracker&&)=delete;
  Tracker&& operator=(Tracker&&)=delete;
  Tracker& operator=(const Tracker&)=delete;

  Tracker(std::string);
  using HTTPRequest::HTTPRequest;
  Tracker(const Tracker&);
  ~Tracker()=default;

  data_nest_t nest;
  std::string get_url();
  friend TrackerManager;
};

#endif
