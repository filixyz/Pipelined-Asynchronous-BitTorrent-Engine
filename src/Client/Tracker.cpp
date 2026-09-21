#include "TrackerManager.hpp"
#include "../Bencoder/Bencode.hpp"
#include <exception>
#include <memory>
#include <sstream>
#include <string>

TrackerManager::Tracker::Tracker (TrackerManager& __manager) : HTTPRequest(), manager(__manager) {};

void TrackerManager::Tracker::active_state_handler(ben::dic& parse) {

  state = tracker_state_t::active;

  { // reset failure stats
    context.failures = 0;
    announce_url.failed_idx = -1;
    announce_url.just_failed = false;
  }

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
  bool url_cycle_exhausted;
  do
    url_cycle_exhausted = seek_to_next_url();
  while ( current_proto != tracker_proto_t::http ); // udp failsafe

  if ( url_cycle_exhausted ) {
    context.failures++;
    timers.maximum_duration = get_retry_seconds(this);
  } else {
    timers.maximum_duration = retry_for_new_url;
  }

  //std::cout << get_url() << " failed tracker -> " << " online: "<< context.online << " -> "
  //  << "failed idx: " << announce_urls.failed_idx << " current idx: " << announce_urls.current_idx << " urls: " << announce_urls.list.size();
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

  std::unique_ptr<Bendata> parse;

  try  {

    std::istringstream bencode(user_space.data);
    parse = std::make_unique<Bendata>(bendecode_from_file(bencode));
    parse->get_data<ben::dic>(); // validation check.

  } catch (std::exception& e) {

    // tracker responded with rubbish bencode
    manager.tracker_connections.erase(announce_url.domain_name);
    std::cout << "bencoded exception: " << e.what() << '\n';
    return;

  }

  ben::dic& parsed_dict = parse->get_data<ben::dic>();

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
