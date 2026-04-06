#pragma once
#include "configured_data_source.hpp"

namespace quarkbot {

class TardisTradesDataSource : public ConfiguredDataSource {
public:
    using ConfiguredDataSource::ConfiguredDataSource;
    std::optional<Event> next_event() override;
private:
    bool _header_parsed = false;
    int _col_timestamp = -1;
    int _col_price = -1;
    int _col_amount = -1;
};

class TardisQuotesDataSource : public ConfiguredDataSource {
public:
    using ConfiguredDataSource::ConfiguredDataSource;
    std::optional<Event> next_event() override;
private:
    bool _header_parsed = false;
    int _col_timestamp = -1;
    int _col_bid_price = -1;
    int _col_bid_size = -1;
    int _col_ask_price = -1;
    int _col_ask_size = -1;
};

} // namespace quarkbot
