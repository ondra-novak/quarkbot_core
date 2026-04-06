#include "tardis_data_source.hpp"
#include <chrono>
#include <string>
#include <string_view>
#include <vector>

// Split a line by comma into string_view slices (line must remain valid during use)
static std::vector<std::string_view> split_csv(std::string_view line) {
    std::vector<std::string_view> cols;
    while (true) {
        auto pos = line.find(',');
        cols.push_back(line.substr(0, pos));
        if (pos == std::string_view::npos) break;
        line.remove_prefix(pos + 1);
    }
    return cols;
}

// Parse a Tardis nanosecond unix timestamp to system_clock::time_point
static std::chrono::system_clock::time_point parse_ns_timestamp(std::string_view s) {
    long long ns = 0;
    for (char c : s) {
        if (c < '0' || c > '9') break;
        ns = ns * 10 + (c - '0');
    }
    return std::chrono::system_clock::time_point(
        std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::nanoseconds(ns)));
}

namespace quarkbot {

std::optional<IBacktestDataSource::Event> TardisTradesDataSource::next_event() {
    std::string line;

    if (!_header_parsed) {
        if (!read_line(line)) return std::nullopt;
        auto cols = split_csv(line);
        for (int i = 0; i < static_cast<int>(cols.size()); ++i) {
            if      (cols[static_cast<std::size_t>(i)] == "timestamp") _col_timestamp = i;
            else if (cols[static_cast<std::size_t>(i)] == "price")     _col_price = i;
            else if (cols[static_cast<std::size_t>(i)] == "amount")    _col_amount = i;
        }
        _header_parsed = true;
    }

    while (read_line(line)) {
        if (line.empty()) continue;
        auto cols = split_csv(line);
        int max_col = std::max({_col_timestamp, _col_price, _col_amount});
        if (max_col < 0 || static_cast<int>(cols.size()) <= max_col) continue;

        auto tp = parse_ns_timestamp(cols[static_cast<std::size_t>(_col_timestamp)]);
        Decimal price, amount;
        try {
            price  = Decimal::from_string(cols[static_cast<std::size_t>(_col_price)]);
            amount = Decimal::from_string(cols[static_cast<std::size_t>(_col_amount)]);
        } catch (...) { continue; }

        Trade trade;
        trade.price = price;
        trade.size  = amount;
        trade.time  = tp;
        return Event{tp, instrument_spec().name, trade};
    }
    return std::nullopt;
}

std::optional<IBacktestDataSource::Event> TardisQuotesDataSource::next_event() {
    std::string line;

    if (!_header_parsed) {
        if (!read_line(line)) return std::nullopt;
        auto cols = split_csv(line);
        for (int i = 0; i < static_cast<int>(cols.size()); ++i) {
            if      (cols[static_cast<std::size_t>(i)] == "timestamp") _col_timestamp = i;
            else if (cols[static_cast<std::size_t>(i)] == "bidPrice")  _col_bid_price = i;
            else if (cols[static_cast<std::size_t>(i)] == "bidSize")   _col_bid_size  = i;
            else if (cols[static_cast<std::size_t>(i)] == "askPrice")  _col_ask_price = i;
            else if (cols[static_cast<std::size_t>(i)] == "askSize")   _col_ask_size  = i;
        }
        _header_parsed = true;
    }

    while (read_line(line)) {
        if (line.empty()) continue;
        auto cols = split_csv(line);
        int max_col = std::max({_col_timestamp, _col_bid_price, _col_bid_size,
                                _col_ask_price, _col_ask_size});
        if (max_col < 0 || static_cast<int>(cols.size()) <= max_col) continue;

        auto tp = parse_ns_timestamp(cols[static_cast<std::size_t>(_col_timestamp)]);
        Decimal bid, bid_size, ask, ask_size;
        try {
            bid      = Decimal::from_string(cols[static_cast<std::size_t>(_col_bid_price)]);
            bid_size = Decimal::from_string(cols[static_cast<std::size_t>(_col_bid_size)]);
            ask      = Decimal::from_string(cols[static_cast<std::size_t>(_col_ask_price)]);
            ask_size = Decimal::from_string(cols[static_cast<std::size_t>(_col_ask_size)]);
        } catch (...) { continue; }

        Quote quote;
        quote.bid      = bid;
        quote.bid_size = bid_size;
        quote.ask      = ask;
        quote.ask_size = ask_size;
        quote.time     = tp;
        return Event{tp, instrument_spec().name, quote};
    }
    return std::nullopt;
}

} // namespace quarkbot
