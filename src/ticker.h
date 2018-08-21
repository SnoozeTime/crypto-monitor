#pragma once

#include <string>
#include "rapidjson/document.h"

namespace cryptom {

  struct ticker {
    double high;
    double low;
    double close;
    double volume;
    int date;
    std::string symbol;
  };

  class json_converter {
  public:
    /**
       Should parse the JSON document in a ticker structure. Will return 0 if ok.
     */
    virtual int ticker_from_json(const rapidjson::Document& json, ticker& t) const = 0;
  };

  class kucoin_converter: public json_converter {
  public:
    int ticker_from_json(const rapidjson::Document& json, ticker& t) const;
  };

  class binance_converter: public json_converter {
  public:
    int ticker_from_json(const rapidjson::Document& json, ticker& t) const;
  };

}
