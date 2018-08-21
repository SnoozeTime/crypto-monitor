#include "ticker.h"

namespace cryptom {

  int kucoin_converter::ticker_from_json(const rapidjson::Document& json, ticker& t) const {

    std::cout << "INSIDE CONVERTER\n";
    /*
      {"success":true,
      "code":"OK",
      "msg":"Operation succeeded.",
      "timestamp":1534491836685,
      "data":{"coinType":"ETH",
              "trading":true,
	      "symbol":"ETH-BTC",
	      "lastDealPrice":0.04631749,
	      "buy":0.04631749,
	      "sell":0.04644194,
	      "change":8.7768E-4,
	      "coinTypePair":"BTC",
	      "sort":100,
	      "feeRate":0.001,
	      "volValue":120.95825502,
	      "high":0.046572,
	      "datetime":1534491836000,
	      "vol":2645.902635,
	      "low":0.04500002,
	      "changeRate":0.0193}}
    */

    if (!json.HasMember("data")) {
      return -1;
    }

    const rapidjson::Value& data_object = json["data"];
    if (!data_object.IsObject()) {
      return -1;
    }

    if (!data_object.HasMember("lastDealPrice") ||
	!data_object.HasMember("symbol") ||
	!data_object.HasMember("high") ||
	!data_object.HasMember("low") ||
	!data_object.HasMember("vol") ||
	!data_object.HasMember("datetime")) {
      return -1;
    }

    // ------------------------------------------------
    if (!data_object["symbol"].IsString()) {
      return -1;
    }
    t.symbol = data_object["symbol"].GetString();

    // ------------------------------------------------
    if (!data_object["high"].IsDouble()) {
      return -1;
    }
    t.high = data_object["high"].GetDouble();

    if (!data_object["low"].IsDouble()) {
      return -1;
    }
    t.low = data_object["low"].GetDouble();

    if (!data_object["lastDealPrice"].IsDouble()) {
      return -1;
    }
    t.close = data_object["lastDealPrice"].GetDouble();

    if (!data_object["vol"].IsDouble()) {
      return -1;
    }
    t.volume = data_object["vol"].GetDouble();
    return 0;
  }
}
