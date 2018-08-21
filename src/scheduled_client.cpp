#include "scheduled_client.h"
#include "openssl_hostname_validation.h"
#include <iostream>
#include <cassert>
#include <event2/bufferevent_ssl.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/http.h>

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <openssl/err.h>
#include "rapidjson/document.h"

namespace cryptom {


  static void
  err(const char *msg)
  {
    fputs(msg, stderr);
  }

  static void
  err_openssl(const char *func)
  {
    fprintf (stderr, "%s failed:\n", func);

    /* This is the OpenSSL function that prints the contents of the
     * error stack to the specified file handle. */
    ERR_print_errors_fp (stderr);

    exit(1);
  }


  static int cert_verify_callback(X509_STORE_CTX *x509_ctx, void *arg)
  {
    char cert_str[256];
    const char *host = (const char *) arg;
    const char *res_str = "X509_verify_cert failed";
    HostnameValidationResult res = Error;

    /* This is the function that OpenSSL would call if we hadn't called
     * SSL_CTX_set_cert_verify_callback().  Therefore, we are "wrapping"
     * the default functionality, rather than replacing it. */
    int ok_so_far = 0;

    X509 *server_cert = NULL;

    ok_so_far = X509_verify_cert(x509_ctx);

    server_cert = X509_STORE_CTX_get_current_cert(x509_ctx);

    if (ok_so_far) {
      res = validate_hostname(host, server_cert);

      switch (res) {
      case MatchFound:
	res_str = "MatchFound";
	break;
      case MatchNotFound:
	res_str = "MatchNotFound";
	break;
      case NoSANPresent:
	res_str = "NoSANPresent";
	break;
      case MalformedCertificate:
	res_str = "MalformedCertificate";
	break;
      case Error:
	res_str = "Error";
	break;
      default:
	res_str = "WTF!";
	break;
      }
    }

    X509_NAME_oneline(X509_get_subject_name (server_cert),
		      cert_str, sizeof (cert_str));

    if (res == MatchFound) {
      printf("https server '%s' has this certificate, "
	     "which looks good to me:\n%s\n",
	     host, cert_str);
      return 1;
    } else {
      printf("Got '%s' for hostname '%s' and certificate:\n%s\n",
	     res_str, host, cert_str);
      return 0;
    }
  }


  scheduled_client::scheduled_client(event_base *base, const char* url, timeval duration,
				     boost::lockfree::queue<cryptom::ticker> *out_queue):
    base_(base),
    duration_(duration),
    converter_(new binance_converter()),
    out_queue_(out_queue){

    uri_ = evhttp_uri_parse(url);

    ssl_ctx_ = SSL_CTX_new(SSLv23_method());
    assert(ssl_ctx_);

    /* TODO: Add certificate loading on Windows as well */
    X509_STORE *store;
    /* Attempt to use the system's trusted root certificates. */
    store = SSL_CTX_get_cert_store(ssl_ctx_);
    assert(X509_STORE_set_default_paths(store) == 1);

    /* Ask OpenSSL to verify the server certificate.  Note that this
     * does NOT include verifying that the hostname is correct.
     * So, by itself, this means anyone with any legitimate
     * CA-issued certificate for any website, can impersonate any
     * other website in the world.  This is not good.  See "The
     * Most Dangerous Code in the World" article at
     * https://crypto.stanford.edu/~dabo/pubs/abstracts/ssl-client-bugs.html
     */
    SSL_CTX_set_verify(ssl_ctx_, SSL_VERIFY_PEER, NULL);

    /* This is how we solve the problem mentioned in the previous
     * comment.  We "wrap" OpenSSL's validation routine in our
     * own routine, which also validates the hostname by calling
     * the code provided by iSECPartners.  Note that even though
     * the "Everything You've Always Wanted to Know About
     * Certificate Validation With OpenSSL (But Were Afraid to
     * Ask)" paper from iSECPartners says very explicitly not to
     * call SSL_CTX_set_cert_verify_callback (at the bottom of
     * page 2), what we're doing here is safe because our
     * cert_verify_callback() calls X509_verify_cert(), which is
     * OpenSSL's built-in routine which would have been called if
     * we hadn't set the callback.  Therefore, we're just
     * "wrapping" OpenSSL's routine, not replacing it. */
    SSL_CTX_set_cert_verify_callback(ssl_ctx_, cert_verify_callback,
				     (void *) evhttp_uri_get_host(uri_));

    timer_ = evtimer_new(base, &scheduled_client::libevent_timeout, (void*) this);

    execute_query();
  }

  scheduled_client::scheduled_client(scheduled_client&& other):
    converter_(new binance_converter()) {

    // These are not owned by the other...
    base_ = other.base_;
    other.base_ = nullptr;

    ssl_ctx_ = other.ssl_ctx_;
    other.ssl_ctx_ = nullptr;

    ssl_ = other.ssl_;
    other.ssl_ = nullptr;

    duration_ = other.duration_;

    uri_ = other.uri_;
    other.uri_ = nullptr;

    bev_ = other.bev_;
    other.bev_ = nullptr;

    evcon_ = other.evcon_;
    other.evcon_ = nullptr;

    timer_ = other.timer_;
    other.timer_ = nullptr;

    out_queue_ = other.out_queue_;
    other.out_queue_ = nullptr;
  }

  // Only destroy if we hold the resources...
  scheduled_client::~scheduled_client() {
    if (ssl_ctx_)
      SSL_CTX_free(ssl_ctx_);

    if (uri_ != nullptr)
      evhttp_uri_free(uri_);

    if (timer_ != nullptr)
      event_free(timer_);
  }


    // Send a GET request to the server.
  void scheduled_client::execute_query() {

    ssl_ = SSL_new(ssl_ctx_);
    if (ssl_ == NULL) {
      err_openssl("SSL_new()");
      return;
    }

    const char *scheme, *host, *path, *query;
    char uri[256];
    int port;
    int retries = 0;
    int timeout = -1;

    struct evkeyvalq *output_headers;
    struct evbuffer *output_buffer;

    int i;
    int ret = 0;
    enum { HTTP, HTTPS } type = HTTP;

    scheme = evhttp_uri_get_scheme(uri_);
    if (scheme == NULL || (strcasecmp(scheme, "https") != 0 &&
			   strcasecmp(scheme, "http") != 0)) {
      err("url must be http or https");
      return;
    }

    host = evhttp_uri_get_host(uri_);
    if (host == NULL) {
      err("url must have a host");
      return;
    }

    port = evhttp_uri_get_port(uri_);
    if (port == -1) {
      port = (strcasecmp(scheme, "http") == 0) ? 80 : 443;
    }

    path = evhttp_uri_get_path(uri_);
    if (strlen(path) == 0) {
      path = "/";
    }

    query = evhttp_uri_get_query(uri_);
    if (query == NULL) {
      snprintf(uri, sizeof(uri) - 1, "%s", path);
    } else {
      snprintf(uri, sizeof(uri) - 1, "%s?%s", path, query);
    }
    uri[sizeof(uri) - 1] = '\0';

#ifdef SSL_CTRL_SET_TLSEXT_HOSTNAME
    // Set hostname for SNI extension
    SSL_set_tlsext_host_name(ssl_, host);
#endif

    if (strcasecmp(scheme, "http") == 0) {
      bev_ = bufferevent_socket_new(base_, -1, BEV_OPT_CLOSE_ON_FREE);
    } else {
      type = HTTPS;
      bev_ = bufferevent_openssl_socket_new(base_, -1, ssl_,
					    BUFFEREVENT_SSL_CONNECTING,
					    BEV_OPT_CLOSE_ON_FREE|BEV_OPT_DEFER_CALLBACKS);
    }

    if (bev_ == NULL) {
      fprintf(stderr, "bufferevent_openssl_socket_new() failed\n");
      return;
    }

    bufferevent_openssl_set_allow_dirty_shutdown(bev_, 1);

    // For simplicity, we let DNS resolution block. Everything else should be
    // asynchronous though.
    evcon_ = evhttp_connection_base_bufferevent_new(base_, NULL, bev_,
						    host, port);
    if (evcon_ == NULL) {
      fprintf(stderr, "evhttp_connection_base_bufferevent_new() failed\n");
      return;
    }

    if (retries > 0) {
      evhttp_connection_set_retries(evcon_, retries);
    }
    if (timeout >= 0) {
      evhttp_connection_set_timeout(evcon_, timeout);
    }

    // Fire off the request
    evhttp_request *req = evhttp_request_new(&scheduled_client::libevent_request_done, (void*) this);
    if (req == NULL) {
      fprintf(stderr, "evhttp_request_new() failed\n");
      return;
    }

    output_headers = evhttp_request_get_output_headers(req);
    evhttp_add_header(output_headers, "Host", host);
    evhttp_add_header(output_headers, "Connection", "close");

    int r = evhttp_make_request(evcon_, req, EVHTTP_REQ_GET, uri);
    if (r != 0) {
      fprintf(stderr, "evhttp_make_request() failed\n");
    }
  }

  void scheduled_client::http_request_done(struct evhttp_request *req)
  {
    if (req == NULL) {
      /* If req is NULL, it means an error occurred, but
       * sadly we are mostly left guessing what the error
       * might have been.  We'll do our best... */
      char error_buffer[256];
      unsigned long oslerr;
      int printed_err = 0;
      int errcode = EVUTIL_SOCKET_ERROR();
      fprintf(stderr, "some request failed - no idea which one though!\n");
      /* Print out the OpenSSL error queue that libevent
       * squirreled away for us, if any. */
      while ((oslerr = bufferevent_get_openssl_error(bev_))) {
	ERR_error_string_n(oslerr, error_buffer, sizeof(error_buffer));
	fprintf(stderr, "%s\n", error_buffer);
	printed_err = 1;
      }
      /* If the OpenSSL error queue was empty, maybe it was a
       * socket error; let's try printing that. */
      if (! printed_err)
	fprintf(stderr, "socket error = %s (%d)\n",
		evutil_socket_error_to_string(errcode),
		errcode);
      return;
    }

    fprintf(stderr, "Response line: %d %s\n",
	    evhttp_request_get_response_code(req),
	    evhttp_request_get_response_code_line(req));

    size_t length = evbuffer_get_length(evhttp_request_get_input_buffer(req));
    char buffer[length+1];
    evbuffer_remove(evhttp_request_get_input_buffer(req), buffer, length);
    buffer[length] = '\0';

    // try to parse as JSON if response 200:
    if (evhttp_request_get_response_code(req) == 200) {
      rapidjson::Document json;
      json.Parse(buffer);
      std::cout << "Ticker\n";
      std::cout << "------\n";
      ticker t;

      if (converter_ == nullptr || converter_.get() == nullptr) {
	std::cout << "CONVERTER IS NULL\n";
      }

      converter_->ticker_from_json(json, t);
      while (!out_queue_->push(t))
            ;
    }

    SSL_free(ssl_);
    //evhttp_connection_free(evcon_);

    evtimer_add(timer_, &duration_);
  }

  void scheduled_client::timeout() {
    std::cout << "\n";
    execute_query();
  }

}
