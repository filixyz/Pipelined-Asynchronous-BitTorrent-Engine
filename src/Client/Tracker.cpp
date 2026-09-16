#include "TrackerManager.hpp"
#include "../Bencoder/Bencode.hpp"
#include <sstream>
#include <string>

TrackerManager::Tracker::Tracker (TrackerManager& __manager) : HTTPRequest(), manager(__manager) {};

void TrackerManager::Tracker::do_on_success() {
  state = tracker_state_t::active;
  context.failures = 0;
  announce_urls.failed_idx = -1;

  if (user_space.data.empty()) {
    context.requeable_permission = false;
    state = tracker_state_t::inactive;
    return;
  }

  std::istringstream bencode(user_space.data);
  Bendata parse = bendecode_from_file(bencode); // TEST
  ben::dic& parsed_dict = parse.get_data<ben::dic>();

  // ----------> EXCEPTION MAY HAPPEN HERE IF BENCODE IS ERRORNEOUS.

  if (parsed_dict.contains("failure reason"))  {
    std::cout << parsed_dict["failure reason"];
    context.requeable_permission = false;
    state = tracker_state_t::inactive;
    return;
  }

  if (parsed_dict.contains("warning message"))
    std::cout << parsed_dict["warning message"];

  if (parsed_dict.contains("interval")) {
    timers.maximum_duration = parsed_dict["interval"].get_data<ben::num>();
    timers.type = timer_count::one;
  }

  if (parsed_dict.contains("min interval")) {
    timers.minimum_duration = parsed_dict["min interval"].get_data<ben::num>();
    timers.type = timer_count::two;
  }

  if (parsed_dict.contains("tracker id"))
    tracker_id = parsed_dict["tracker id"].get_data<ben::str>();

  if (parsed_dict.contains("peers"))
    std::cout << parsed_dict["peers"];

  arm_timer(timers.maximum, timers.maximum_duration);
  if (timers.type ==  timer_count::two) {
    arm_timer(timers.minimum, timers.minimum_duration);
    context.interruptible=false;
  }

  if (context.requeable_permission)  return_to_manager_space();

}

static constexpr int retry_for_new_url = 5;

void TrackerManager::Tracker::do_on_failure() {

  state = tracker_state_t::inactive;
  timers.type = timer_count::one;

  // cache failed idx for wraparound check
  if (context.failures == 0) {
    announce_urls.failed_idx = announce_urls.current_idx;
  }
  // move to next http url
  while ( current_proto == tracker_proto_t::http ) seek_to_next_url();

  if ( announce_urls.current_idx == announce_urls.failed_idx ) {
    context.failures++;
    timers.maximum_duration = get_retry_seconds(this);
  } else {
    timers.maximum_duration = retry_for_new_url;
  }

  arm_timer(timers.maximum, timers.maximum_duration);

  if (context.requeable_permission)  return_to_manager_space();

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
  return announce_urls.list[announce_urls.current_idx];
}

void TrackerManager::Tracker::seek_to_next_url() {
  auto& index = ++announce_urls.current_idx;
  auto& urls  = announce_urls.list;
  if ( index >= urls.size() ) index=0;
  current_proto = urls[index].starts_with("http") ? tracker_proto_t::http : tracker_proto_t::udp;
}
