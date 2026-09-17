#include "TrackerManager.hpp"
#include "../Bencoder/Bencode.hpp"
#include <exception>
#include <sstream>
#include <string>

TrackerManager::Tracker::Tracker (TrackerManager& __manager) : HTTPRequest(), manager(__manager) {};

void TrackerManager::Tracker::active_state_handler(ben::dic& parse) {

  state = tracker_state_t::active;
  context.failures = 0;
  announce_urls.failed_idx = -1;

  if (parse.contains("warning message"))
    std::cout << parse["warning message"];

  if (parse.contains("interval")) {
    timers.maximum_duration = parse["interval"].get_data<ben::num>();
    timers.type = timer_count::one;
  }

  if (parse.contains("min interval")) {
    timers.minimum_duration = parse["min interval"].get_data<ben::num>();
    timers.type = timer_count::two;
  }

  if (parse.contains("tracker id"))
    tracker_id = parse["tracker id"].get_data<ben::str>();

  if (parse.contains("peers"))
    std::cout << parse["peers"];

  arm_timer(timers.maximum, timers.maximum_duration);

  if (timers.type == timer_count::two) {
    arm_timer(timers.minimum, timers.minimum_duration);
    context.interruptible=false;
  }

  if (manager.tracker_context.active)
    return_to_manager_space();
  else
    shutdown_reset();

}

static constexpr int retry_for_new_url = 5;

void TrackerManager::Tracker::inactive_state_handler() {

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

  if (manager.tracker_context.active)
    return_to_manager_space();
  else
    shutdown_reset();

}

void TrackerManager::Tracker::do_on_success() {
  context.online = true;

  if (user_space.data.empty()) {
    inactive_state_handler();
    return;
  }

  ben::dic* parsed_dict_ptr;
  try  {

    std::istringstream bencode(user_space.data);
    Bendata parse = bendecode_from_file(bencode);
    parsed_dict_ptr = &parse.get_data<ben::dic>();

  } catch (std::exception& e) {

    // tracker responded with rubbish bencode
    std::string domain_name = announce_urls.domain_name;
    manager.tracker_connections.erase(domain_name);
    std::cout << "deleted tracker -> " << domain_name << '\n';
    std::cout << "bencoded exception: " << e.what() << '\n';

    return;
  }

  ben::dic& parsed_dict = * parsed_dict_ptr;

  if (parsed_dict.contains("failure reason"))  {
    std::cout << parsed_dict["failure reason"];
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
  return announce_urls.list[announce_urls.current_idx];
}

void TrackerManager::Tracker::seek_to_next_url() {
  auto& index = ++announce_urls.current_idx;
  auto& urls  = announce_urls.list;
  if ( index >= urls.size() ) index=0;
  current_proto = urls[index].starts_with("http") ? tracker_proto_t::http : tracker_proto_t::udp;
}
