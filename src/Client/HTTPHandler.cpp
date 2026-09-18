#include "HTTPHandler.hpp"
#include <cassert>
#include <cstddef>
#include <curl/curl.h>
#include <curl/easy.h>
#include <curl/multi.h>
#include <ev++.h>
#include <ev.h>
#include <iostream>

HTTPRequest::HTTPRequest(): user_space{} {
  connection = new_easy(user_space);
  curl_easy_setopt(connection, CURLOPT_PRIVATE, this);
};
HTTPRequest::~HTTPRequest() {
  curl_easy_cleanup(connection);
}

HTTPHandler::HTTPHandler(ev::dynamic_loop& ev_loop): event_loop(ev_loop), curl_timer(event_loop) {
  multi = curl_multi_init();
  if (multi) std::cout << "HTPP BACKEND INITIATED\n";
  curl_multi_setopt(multi, CURLMOPT_SOCKETFUNCTION, socket_callback);
  curl_multi_setopt(multi, CURLMOPT_SOCKETDATA, this);
  curl_multi_setopt(multi, CURLMOPT_TIMERFUNCTION, timer_callback);
  curl_multi_setopt(multi, CURLMOPT_TIMERDATA, this);
  curl_timer.set<HTTPHandler::drive_timer>();
  curl_timer.data=this;
}

HTTPHandler::~HTTPHandler() {
  curl_multi_cleanup(multi);
}

void HTTPHandler::escape_byte_string(std::string& url) {
  char * escaped_string = curl_escape(url.data(), url.size());
  url.replace(url.begin(), url.end(), escaped_string);
}

CURL* HTTPHandler::new_easy(network_data& user_field) {
  CURL* newE = curl_easy_init();
  curl_easy_setopt(newE, CURLOPT_WRITEFUNCTION, easy_callback);
  curl_easy_setopt(newE, CURLOPT_WRITEDATA, &user_field);
  //curl_easy_setopt(newE, CURLOPT_SERVER_RESPONSE_TIMEOUT, 10L); // time limit for connected tracker response: aborts transaction is limit approached
  curl_easy_setopt(newE, CURLOPT_FOLLOWLOCATION, 1L); // might handle redirecting trackers
  curl_easy_setopt(newE, CURLOPT_TIMEOUT, 30L); // total time before transaction is terminated: both connection phase and response phase (i think)
  curl_easy_setopt(newE, CURLOPT_LOW_SPEED_LIMIT, 1L);
  curl_easy_setopt(newE, CURLOPT_LOW_SPEED_TIME, 10L);
  /**EXPERIMENTAL**/curl_easy_setopt(newE, CURLOPT_CONNECTTIMEOUT, 5L);// time limit for connection to tracker; aborts transaction if limit approached
  return newE;
}

void HTTPHandler::add_request(HTTPRequest* request) {

  request->sock_wtchr.set(event_loop);
  curl_easy_setopt(request->connection, CURLOPT_URL, request->user_space.url.data());
  curl_multi_add_handle(multi, request->connection);

}

void HTTPHandler::rmv_request(HTTPRequest* request) {
  curl_multi_remove_handle(multi, request->connection);
}

void HTTPHandler::chk_finished(CURLM* multi) {

  CURLMsg *msg;
  int msg_left;

  while(( msg=curl_multi_info_read(multi, &msg_left) )) {
    if(msg->msg != CURLMSG_DONE)
      continue;

    CURL* easy = msg->easy_handle;
    HTTPRequest* request;
    curl_easy_getinfo(easy, CURLINFO_PRIVATE, &request);
    CURLcode result = msg->data.result;
    long http_res_code;
    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &http_res_code);

    if(result == CURLE_OK && http_res_code==200)
      request->do_on_success();
    else // Else block might treat trackers with redirects (http response code 3XX) as failures (fixed)
      request->do_on_failure();
    curl_multi_remove_handle(multi, easy);
  }

}

// handle networks events in the supplied
// socket.data is "this" pointer of current HttpHandler object'
void HTTPHandler::drive_sockt(ev::io& socket, int revents) {

  auto actions = ( revents&ev::READ?CURL_CSELECT_IN:0 ) | ( revents&ev::WRITE?CURL_CSELECT_OUT:0 );
  HTTPHandler* http = static_cast<HTTPHandler*>(socket.data);
  CURLMcode cRes = curl_multi_socket_action(http->multi, socket.fd, actions, &http->actives);
  (void)cRes;// code to handle cRes Goes here
  chk_finished(http->multi);;

}

// Need a way to get this pointer in static function
// can think of two ways
// 1, Store this into the .data member of the ev::timer object so can be accessed here when its callback is invoked
// 2. Pointer arithmetics with C STL offset(type_name, type_member_name)
// will be going with once since easier to think about
void HTTPHandler::drive_timer(ev::timer& timer, int revents) {

  (void) revents;
  HTTPHandler* http = static_cast<HTTPHandler*>(timer.data);
  CURLMcode cRes = curl_multi_socket_action(http->multi, CURL_SOCKET_TIMEOUT, 0, &http->actives);
  (void) cRes;
  // code to handle cRes Goes here
  chk_finished(http->multi);

}

size_t HTTPHandler::easy_callback(const char* data, size_t size, size_t datalen, void *user_data) {

  network_data *mem = (network_data *) (user_data);
  if (!mem) return 0;
  mem->data += data;
  mem->size += datalen;
  return size * datalen;

}

void HTTPHandler::remove_socket(ev::io* socket_watcher) {
  socket_watcher->stop();
  socket_watcher->set(-1, ev::READ);
}

void HTTPHandler::add_socket(curl_socket_t fd, ev::io* watcher, CURL* easy, int action, HTTPHandler* httpG) {
  // from easy derive address of watcher, it should be the private data in easy object

  curl_multi_assign(easy, fd, watcher);
  watcher->set<HTTPHandler::drive_sockt>(httpG);
  set_socket(fd, watcher, action);

}

void HTTPHandler::set_socket(curl_socket_t fd, ev::io* watcher, int what) {

  auto actions = (what & CURL_POLL_IN ? ev::READ : 0) | (what & CURL_POLL_OUT ? ev::WRITE : 0);
  if (actions==0) return;
  if (watcher->active) watcher->stop();
  watcher->set(fd, actions);
  watcher->start();

}

// variable httpG is a "this" pointer to current httphandler object
// easy     is the easy handle associated with the request
// s        is the socket in which  the request occurs in
// what     is a bitmask represnting what the polling systen should wait for on socket s
// clientp  in our context is a pointer to the HTTPHandler object
// socketp  will be a pointer to the socket watcher;
//          This is the pointer stored with curl_multi_assign when first creating a socket
int HTTPHandler::socket_callback(CURL *easy, curl_socket_t sockfd, int what, void *clientp, void *socketp) {

  HTTPHandler*  httpG = static_cast<HTTPHandler*>(clientp);
  void* private_data {nullptr};
  curl_easy_getinfo(easy, CURLINFO_PRIVATE, &private_data);
  ev::io* socket_watcher = &(static_cast<HTTPRequest*>(private_data))->sock_wtchr;

  if(what==CURL_POLL_REMOVE) {
    remove_socket(socket_watcher);
    return 0;
  }
  if (!socketp)
    add_socket(sockfd, socket_watcher, easy, what, httpG);
  else
    set_socket(sockfd, socket_watcher, what);

  return 0;
  ;

}

// multi        is the pointer to the multi handle
// timeout_ms   is the timeout to wait for
// userp        in our context is a pointer to the HTTPHandler object.

int HTTPHandler::timer_callback(CURLM *multi, long timeout_ms, void *userp) {

  (void) multi;
  constexpr double ms_per_sec = 1000.0;
  HTTPHandler* httpG = static_cast<HTTPHandler*>( userp );
  ev::timer& timer =  httpG->curl_timer;
  if (timer.active)
    timer.stop();
  double timeout = timeout_ms/ms_per_sec;
  timer.set(timeout);
  timer.start();
  return 0;

}

void HTTPHandler::start_backend() {
  if (curl_multi_socket_action(multi, CURL_SOCKET_TIMEOUT, 0, &actives) == CURLM_OK)
    std::cout << "HTTP BACKEND ONLINE";
}
