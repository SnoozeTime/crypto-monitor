#include <iostream>
#include "scheduled_client.h"
#include <openssl/err.h>
#include "rapidjson/document.h"
#include <map>
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <thread>


const std::string kucoin_base_url = "https://api.kucoin.com/v1/open/tick?symbol=";
const std::string binance_base_url = "https://api.binance.com/api/v1/ticker/24hr?symbol=";
std::string create_kurl(std::string coin, std::string base_coin) {
  std::string url = kucoin_base_url + coin + "-" + base_coin;
  return url;
}

std::string create_burl(std::string coin, std::string base_coin) {
  std::string url = binance_base_url + coin + base_coin;
  return url;
}

/*
  Read from json input file. Contains the amount of coin in the portfolio, as well as the base coin to which we'll compare.
 */
struct config {
  std::map<std::string, double> coins;
  std::string base_currency = "BTC";
};

bool parse_config(const char* input_file, config& configuration) {
  std::ifstream myfile(input_file);

  if (myfile.is_open()) {
    std::stringstream strstream;
    strstream << myfile.rdbuf();

    rapidjson::Document json;
    json.Parse(strstream.str().c_str());

    if (json.HasParseError()) {
      std::cerr << "Invalid JSON file, check " << input_file << " for syntax\n";
      return false;
    }

    // Check the base currency and use BTC by default if not exist.
    if (!json.HasMember("base_coin") || !json["base_coin"].IsString()) {
      std::cout << "No base_coin in JSON file. Will default to BTC\n";
    } else {
      configuration.base_currency = json["base_coin"].GetString();
    }

    // Now add all the coins from the portfolio
    // ----------------------------------------
    if (!json.HasMember("portfolio")) {
      std::cerr << "JSON input file should have a portfolio array\n";
      return false;
    }

    if (!json["portfolio"].IsArray()) {
      std::cerr << "portfolio element should be an array\n";
      return false;
    }

    int nb_coins = json["portfolio"].Size();
    if (nb_coins == 0) {
      std::cerr << "portfolio array is empty. Add some coins to your portfolio before starting the monitor\n";
      return false;
    }

    const rapidjson::Value& portfolio = json["portfolio"];
    for (int i=0; i<nb_coins; i++) {
      // Each element of the array is a json object
      // {"coin": "ETH", "quantity": 2.42}
      if (!portfolio[i].IsObject()) {
	std::cerr << "Member of portfolio array should be json object {'coin':'eth', 'quantity': 2}\n";
	return false;
      }

      if (!portfolio[i].HasMember("coin") || !portfolio[i].HasMember("quantity")) {
	std::cerr << "Member of portfolio array should be json object {'coin':'eth', 'quantity': 2}\n";
	return false;
      }

      if (!portfolio[i]["coin"].IsString()) {
	std::cerr << "coin attribute should be a string\n";
	return false;
      }

      if (!portfolio[i]["quantity"].IsNumber()) {
	std::cerr << "quantity attribute should be a double\n";
	return false;
      }

      if (portfolio[i]["quantity"].IsInt()) {
	configuration.coins[portfolio[i]["coin"].GetString()] = static_cast<double>(portfolio[i]["quantity"].GetInt());
      } else {
	configuration.coins[portfolio[i]["coin"].GetString()] = portfolio[i]["quantity"].GetDouble();
      }
    }

  } else {
    std::cerr << "Cannot open file: " << input_file << std::endl;
    return false;
  }

  return true;
}


int io_thread(const config &config, event_base* base, boost::lockfree::queue<cryptom::ticker> *queue) {

#if (OPENSSL_VERSION_NUMBER < 0x10100000L) ||				\
  (defined(LIBRESSL_VERSION_NUMBER) && LIBRESSL_VERSION_NUMBER < 0x20700000L)
  // Initialize OpenSSL
  SSL_library_init();
  ERR_load_crypto_strings();
  SSL_load_error_strings();
  OpenSSL_add_all_algorithms();
#endif


  if (!base) {
    perror("event_base_new()");
    return -1;
  }

  timeval duration{2,0};

  // Create all the clients
  std::vector<cryptom::scheduled_client> clients;
  clients.reserve(2);

  for (const auto& entry: config.coins) {
    std::string url = create_burl(entry.first, config.base_currency);
    std::cout << "Will create client for " << url << std::endl;
    clients.emplace_back(base, url.c_str(), duration, queue);
  }

  event_base_dispatch(base);
  event_base_free(base);

#if (OPENSSL_VERSION_NUMBER < 0x10100000L) ||				\
  (defined(LIBRESSL_VERSION_NUMBER) && LIBRESSL_VERSION_NUMBER < 0x20700000L)
  EVP_cleanup();
  ERR_free_strings();

#if OPENSSL_VERSION_NUMBER < 0x10000000L
  ERR_remove_state(0);
#else
  ERR_remove_thread_state(NULL);
#endif

  CRYPTO_cleanup_all_ex_data();

  sk_SSL_COMP_free(SSL_COMP_get_compression_methods());
#endif /* (OPENSSL_VERSION_NUMBER < 0x10100000L) ||			\
	  (defined(LIBRESSL_VERSION_NUMBER) && LIBRESSL_VERSION_NUMBER < 0x20700000L) */

  return 0;
}

int main(int argc, char **argv) {

  const char *config_path = argv[1];

  // Queue for communication between backend and GUI
  // We are going to request tickers every few seconds so 128 should be large enough.
  boost::lockfree::queue<cryptom::ticker> queue(128);

  config conf;
  if (parse_config(config_path, conf)) {
    for (const auto& entry: conf.coins) {
      std::cout << entry.first << " -> " << entry.second << std::endl;
    }

    event_base *base = event_base_new();
    std::thread communication_thread(io_thread, conf, base, &queue);

    // wait for 5 tickers
    int nb_ticker = 0;
    while (nb_ticker < 5) {
      cryptom::ticker t;
      while (queue.pop(t)) {
	++nb_ticker;
	std::cout << "From GUI thread\n";

	std::cout << "Symbol: " << t.symbol << "\n";
	std::cout << "close: " << t.close << "\n";
	std::cout << "high: " << t.high << "\n";
	std::cout << "low: " << t.low << "\n";
	std::cout << "volume: " << t.volume << "\n";
      }
    }
    timeval onesec = {1, 0};
    event_base_loopexit(base, &onesec);

    communication_thread.join();
  } else {
    std::cout << "Error parsing the configuration\n";
  }
  return 0;
}
