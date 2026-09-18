#ifndef LIBCURL_HANDLER
#define LIBCURL_HANDLER

#include <cstddef>
#include <curl/curl.h>
#include <string>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <ev++.h>

class HTTPHandler {

  CURLM *multi;
  ev::dynamic_loop& event_loop;
  ev::timer curl_timer;
  int actives;

  struct network_data;
  static CURL* new_easy(network_data&);
  static size_t easy_callback(const char* data, size_t size, size_t datalen,void *user_data);
  static int timer_callback(CURLM *multi, long timeout_ms, void *userp);
  static int socket_callback(CURL *easy, curl_socket_t s, int what, void *clientp, void *socketp);
  static void remove_socket(ev::io*);
  static void add_socket(curl_socket_t, ev::io*, CURL*, int, HTTPHandler*);
  static void set_socket(curl_socket_t, ev::io*, int);
  static void chk_finished(CURLM*);
  static void drive_sockt(ev::io&, int);
  static void drive_timer(ev::timer&, int);

public:
  class HTTPRequest;
  HTTPHandler(ev::dynamic_loop&);
  ~HTTPHandler();
  static void escape_byte_string(std::string&);
  void add_request(HTTPRequest*);
  void rmv_request(HTTPRequest*);
  void start_backend();
  void reset();
};

struct HTTPHandler::network_data {
  std::string url{};
  std::string data{};
  std::size_t size{0};
};

class HTTPHandler::HTTPRequest {

  virtual void do_on_success()=0;
  virtual void do_on_failure()=0;
  CURL* connection;
  ev::io sock_wtchr;

public:
  network_data user_space;
  HTTPRequest();      virtual ~HTTPRequest();
  friend HTTPHandler;
};

using HTTPRequest = class HTTPHandler::HTTPRequest;

#endif
