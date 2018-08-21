#pragma once

#include <openssl/ssl.h>
#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/http.h>
#include "ticker.h"
#include <memory>

#include <boost/lockfree/queue.hpp>

namespace cryptom {

  class scheduled_client {

  public:
    scheduled_client(event_base *base,
		     const char* url,
		     timeval duration,
		     boost::lockfree::queue<cryptom::ticker> *out_queue);
    ~scheduled_client();

    // no copy or assignement
    scheduled_client(const scheduled_client&) = delete;
    scheduled_client& operator=(scheduled_client&) = delete;

    // enable move. I want to keep the clients in a collection of clients. e.g. vector using emplace_back
    scheduled_client(scheduled_client&& other);

  private:

    // pointer to the event loop of libevent.
    event_base *base_;

    // SSL context used for TLS requests.
    SSL_CTX *ssl_ctx_;
    SSL *ssl_;

    // how long should we wait between updates
    timeval duration_;

    // Data structure to extract host/port/scheme... and so on.
    evhttp_uri *uri_;

    bufferevent *bev_;
    evhttp_connection *evcon_;

    // timer between the requests.
    event *timer_;

    // How to convert from json to ticker?
    std::unique_ptr<json_converter> converter_;

    // Way to send the results. Not owned by this object
    boost::lockfree::queue<cryptom::ticker> *out_queue_;

    // Send a GET request to the server.
    void execute_query();

    /*
      Callback for when we receive the response to our request.
    */
    static void libevent_request_done(struct evhttp_request *req, void *ctx) {
      (static_cast<scheduled_client*>(ctx))->http_request_done(req);
    }
    void http_request_done(struct evhttp_request *req);

    /*
      callbacks for when the timer times out.
    */
    static void libevent_timeout(evutil_socket_t fd, short a, void* data) {
      (static_cast<scheduled_client*>(data))->timeout();
    }
    void timeout();
  };

}
