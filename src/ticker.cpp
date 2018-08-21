#include "ticker.h"

namespace cryptom {

  int kucoin_converter::ticker_from_json(const rapidjson::Document& json, ticker& t) const {

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

  int binance_converter::ticker_from_json(const rapidjson::Document& json, ticker& t) const {

    /*
      {
      "symbol": "BNBBTC",
      "priceChange": "-94.99999800",
      "priceChangePercent": "-95.960",
      "weightedAvgPrice": "0.29628482",
      "prevClosePrice": "0.10002000",
      "lastPrice": "4.00000200",
      "lastQty": "200.00000000",
      "bidPrice": "4.00000000",
      "askPrice": "4.00000200",
      "openPrice": "99.00000000",
      "highPrice": "100.00000000",
      "lowPrice": "0.10000000",
      "volume": "8913.30000000",
      "quoteVolume": "15.30000000",
      "openTime": 1499783499040,
      "closeTime": 1499869899040,
      "firstId": 28385,   // First tradeId
      "lastId": 28460,    // Last tradeId
      "count": 76         // Trade count
      }
    */


    if (!json.HasMember("lastPrice") ||
	!json.HasMember("symbol") ||
	!json.HasMember("highPrice") ||
	!json.HasMember("lowPrice") ||
	!json.HasMember("volume") ||
	!json.HasMember("closeTime")) {
      return -1;
    }

    // ------------------------------------------------
    if (!json["symbol"].IsString()) {
      return -1;
    }
    t.symbol = json["symbol"].GetString();

    // ------------------------------------------------
    if (!json["highPrice"].IsString()) {
      return -1;
    }
    t.high = std::stod(json["highPrice"].GetString());

    if (!json["lowPrice"].IsString()) {
      return -1;
    }
    t.low = std::stod(json["lowPrice"].GetString());

    if (!json["lastPrice"].IsString()) {
      return -1;
    }
    t.close = std::stod(json["lastPrice"].GetString());

    if (!json["volume"].IsString()) {
      return -1;
    }
    t.volume = std::stod(json["volume"].GetString());
    return 0;
  }

}
