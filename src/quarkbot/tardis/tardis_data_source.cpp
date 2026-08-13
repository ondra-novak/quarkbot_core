
#include "tardis_data_source.hpp"
#include <zlib.h>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
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

// Strip trailing \r\n from a string in-place
static void strip_newline(std::string &s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
        s.pop_back();
}

namespace quarkbot {

TardisCsvDataSource::TardisCsvDataSource(std::filesystem::path csv_gz_path)
    : _path(std::move(csv_gz_path)) {
    #ifdef _WIN32
    _gz = reinterpret_cast<gzFile_s *>(gzopen_w(_path.c_str(), "rb"));
    #else
    _gz = reinterpret_cast<gzFile_s *>(gzopen(_path.c_str(), "rb"));
    #endif
    if (!_gz) throw std::runtime_error("Cannot open gz file: " + _path.string());
    std::string header;
    if (read_line(header)) {
        for (auto col: split_csv(header)) _header.emplace_back(col);
    }
}

TardisCsvDataSource::~TardisCsvDataSource() {
    if (_gz) gzclose(reinterpret_cast<gzFile>(_gz));
}

TardisCsvDataSource::TardisCsvDataSource(TardisCsvDataSource &&other) noexcept
    :_symbol(std::move(other._symbol))
    ,_path(std::move(other._path))
    ,_gz(std::exchange(other._gz, nullptr))
    ,_header(std::move(other._header))
    ,_missing(std::move(other._missing))
    ,_line(other._line)
    ,_last_time(other._last_time) {}

int TardisCsvDataSource::optional_column(std::string_view name) const {
    for (std::size_t i = 0; i < _header.size(); ++i)
        if (_header[i] == name) return static_cast<int>(i);
    return -1;
}

int TardisCsvDataSource::require_column(std::string_view name) {
    int i = optional_column(name);
    if (i < 0) {
        if (!_missing.empty()) _missing.append(", ");
        _missing.append(name);
    }
    return i;
}

void TardisCsvDataSource::check_columns() const {
    if (!_missing.empty())
        throw std::runtime_error(std::format(
            "Tardis source: missing column(s) {} in file {}", _missing, _path.string()));
}

const std::string &TardisCsvDataSource::row_symbol(
        std::string_view exchange, std::string_view symbol) {
    if (_symbol.empty()) {
        _symbol.append(exchange).append(":").append(symbol);
    } else if (_symbol.size() != exchange.size() + 1 + symbol.size()
            || !_symbol.starts_with(exchange)
            || _symbol[exchange.size()] != ':'
            || !_symbol.ends_with(symbol)) {
        row_error(std::format("symbol changed from {} to {}:{}", _symbol, exchange, symbol));
    }
    return _symbol;
}

void TardisCsvDataSource::check_order(std::chrono::system_clock::time_point t) {
    if (t < _last_time) row_error("local_timestamp is lower than on the previous row");
    _last_time = t;
}

bool TardisCsvDataSource::read_line_raw(std::string &out) {
    out.clear();
    char buf[4096];
    while (true) {
        if (!gzgets(reinterpret_cast<gzFile>(_gz), buf, sizeof(buf))) {
            int errnum = 0;
            gzerror(reinterpret_cast<gzFile>(_gz), &errnum);
            // Z_OK or Z_STREAM_END both indicate clean EOF (behaviour varies by zlib version)
            if (errnum != Z_OK && errnum != Z_STREAM_END)
                throw std::runtime_error("gz read error in TardisCsvDataSource::read_line");
            return !out.empty();
        }
        out += buf;
        // gzgets stops at newline or EOF; if we got a newline we're done
        if (!out.empty() && out.back() == '\n') {
            strip_newline(out);
            return true;
        }
        // buffer was full without a newline — loop to get rest of line
    }
}

bool TardisCsvDataSource::read_line(std::string &out) {
    bool ok = read_line_raw(out);
    if (ok) ++_line;
    return ok;
}

void TardisCsvDataSource::row_error(std::string_view message) const {
    throw std::runtime_error(std::format(
        "Tardis source: {} in file {}, row {}", message, _path.string(), _line));
}

Decimal TardisCsvDataSource::parse_decimal(std::string_view value, std::string_view column) const {
    try {
        return Decimal::from_string(value);
    } catch (const std::exception &) {
        row_error(std::format("column {} is not a number: '{}'", column, value));
    }
}

std::chrono::system_clock::time_point TardisCsvDataSource::parse_us_timestamp(
        std::string_view value, std::string_view column) const {
    long long us = 0;
    auto res = std::from_chars(value.data(), value.data() + value.size(), us);
    if (res.ec != std::errc{} || res.ptr != value.data() + value.size() || us < 0) {
        row_error(std::format(
            "column {} is not a microsecond timestamp: '{}'", column, value));
    }
    //system_clock::duration is nanoseconds here, so duration_cast multiplies by
    //1000; anything above this bound overflows instead of converting
    constexpr long long max_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::duration::max()).count();
    if (us > max_us) {
        row_error(std::format("column {} is out of range: '{}'", column, value));
    }
    return std::chrono::system_clock::time_point(
        std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::microseconds(us)));
}

TardisTradesDataSource::TardisTradesDataSource(std::filesystem::path p)
    :TardisCsvDataSource(std::move(p))
{
    _col_exchange = require_column("exchange");
    _col_symbol = require_column("symbol");
    _col_local_timestamp = require_column("local_timestamp");
    _col_price = require_column("price");
    _col_amount = require_column("amount");
    check_columns();
    _col_side = optional_column("side");
    int max_col = std::max({_col_exchange, _col_symbol, _col_local_timestamp, _col_price, _col_amount});
    // side is optional: fold it into _min_cols only when the column is present,
    // so a row too short to reach it is a truncated row, not a silent undetermined
    if (_col_side >= 0) max_col = std::max(max_col, _col_side);
    _min_cols = static_cast<std::size_t>(max_col) + 1;
}

TardisQuotesDataSource::TardisQuotesDataSource(std::filesystem::path p)
    :TardisCsvDataSource(std::move(p))
{
    _col_exchange = require_column("exchange");
    _col_symbol = require_column("symbol");
    _col_local_timestamp = require_column("local_timestamp");
    _col_bid_price  = require_column("bid_price");
    _col_bid_amount = require_column("bid_amount");
    _col_ask_price  = require_column("ask_price");
    _col_ask_amount = require_column("ask_amount");
    check_columns();
    _min_cols = static_cast<std::size_t>(std::max({_col_exchange, _col_symbol, _col_local_timestamp,
        _col_bid_price, _col_bid_amount, _col_ask_price, _col_ask_amount})) + 1;
}

bool TardisTradesDataSource::operator()(BacktestEvent &ev) {
    std::string line;
    while (read_line(line)) {
        if (line.empty()) continue;
        auto cols = split_csv(line);
        if (cols.size() < _min_cols) row_error("truncated row");

        auto tp = parse_us_timestamp(
            cols[static_cast<std::size_t>(_col_local_timestamp)], "local_timestamp");
        check_order(tp);

        Trade trade;
        trade.price = parse_decimal(cols[static_cast<std::size_t>(_col_price)], "price");
        trade.size  = parse_decimal(cols[static_cast<std::size_t>(_col_amount)], "amount");
        trade.time  = tp;
        if (_col_side >= 0 && cols.size() > static_cast<std::size_t>(_col_side)) {
            auto raw = cols[static_cast<std::size_t>(_col_side)];
            auto s = string_lookup<Side>(raw);
            if (!s) row_error(std::format("unknown side value: '{}'", raw));
            trade.side = *s;
        }
        ev.symbol = row_symbol(cols[static_cast<std::size_t>(_col_exchange)],
                               cols[static_cast<std::size_t>(_col_symbol)]);
        ev.time = tp;
        ev.data = trade;
        return true;
    }
    return false;
}

bool TardisQuotesDataSource::operator()(BacktestEvent &ev) {
    std::string line;
    while (read_line(line)) {
        if (line.empty()) continue;
        auto cols = split_csv(line);
        if (cols.size() < _min_cols) row_error("truncated row");

        auto tp = parse_us_timestamp(
            cols[static_cast<std::size_t>(_col_local_timestamp)], "local_timestamp");
        check_order(tp);

        Quote quote;
        quote.bid      = parse_decimal(cols[static_cast<std::size_t>(_col_bid_price)], "bid_price");
        quote.bid_size = parse_decimal(cols[static_cast<std::size_t>(_col_bid_amount)], "bid_amount");
        quote.ask      = parse_decimal(cols[static_cast<std::size_t>(_col_ask_price)], "ask_price");
        quote.ask_size = parse_decimal(cols[static_cast<std::size_t>(_col_ask_amount)], "ask_amount");
        quote.time     = tp;
        ev.symbol = row_symbol(cols[static_cast<std::size_t>(_col_exchange)],
                               cols[static_cast<std::size_t>(_col_symbol)]);
        ev.time = tp;
        ev.data = quote;
        return true;
    }
    return false;
}

} // namespace quarkbot
