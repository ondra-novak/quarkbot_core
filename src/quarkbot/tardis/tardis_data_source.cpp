
#include "tardis_data_source.hpp"
#include <zlib.h>
#include <algorithm>
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

// Parse a Tardis microsecond unix timestamp to system_clock::time_point
static std::chrono::system_clock::time_point parse_us_timestamp(std::string_view s) {
    long long us = 0;
    for (char c : s) {
        if (c < '0' || c > '9') break;
        us = us * 10 + (c - '0');
    }
    return std::chrono::system_clock::time_point(
        std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::microseconds(us)));
}

// Strip trailing \r\n from a string in-place
static void strip_newline(std::string &s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
        s.pop_back();
}

namespace quarkbot {

TardisCsvDataSource::TardisCsvDataSource(std::string instrument, std::filesystem::path csv_gz_path)
    : _instrument(std::move(instrument)), _path(std::move(csv_gz_path)) {
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
    :_instrument(std::move(other._instrument))
    ,_path(std::move(other._path))
    ,_gz(std::exchange(other._gz, nullptr))
    ,_header(std::move(other._header))
    ,_missing(std::move(other._missing)) {}

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

bool TardisCsvDataSource::read_line(std::string &out) {
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

TardisTradesDataSource::TardisTradesDataSource(std::string instrument, std::filesystem::path p)
    :TardisCsvDataSource(std::move(instrument), std::move(p))
{
    _col_local_timestamp = require_column("local_timestamp");
    _col_price = require_column("price");
    _col_amount = require_column("amount");
    check_columns();
    _min_cols = static_cast<std::size_t>(
        std::max({_col_local_timestamp, _col_price, _col_amount})) + 1;
}

TardisQuotesDataSource::TardisQuotesDataSource(std::string instrument, std::filesystem::path p)
    :TardisCsvDataSource(std::move(instrument), std::move(p))
{
    _col_local_timestamp = require_column("local_timestamp");
    _col_bid_price  = require_column("bid_price");
    _col_bid_amount = require_column("bid_amount");
    _col_ask_price  = require_column("ask_price");
    _col_ask_amount = require_column("ask_amount");
    check_columns();
    _min_cols = static_cast<std::size_t>(std::max({_col_local_timestamp,
        _col_bid_price, _col_bid_amount, _col_ask_price, _col_ask_amount})) + 1;
}

bool TardisTradesDataSource::operator()(BacktestEvent &ev) {
    std::string line;
    while (read_line(line)) {
        if (line.empty()) continue;
        auto cols = split_csv(line);
        if (cols.size() < _min_cols) continue;

        auto tp = parse_us_timestamp(cols[static_cast<std::size_t>(_col_local_timestamp)]);
        Decimal price, amount;
        try {
            price  = Decimal::from_string(cols[static_cast<std::size_t>(_col_price)]);
            amount = Decimal::from_string(cols[static_cast<std::size_t>(_col_amount)]);
        } catch (...) { continue; }

        Trade trade;
        trade.price = price;
        trade.size  = amount;
        trade.time  = tp;
        ev.symbol = instrument();
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
        if (cols.size() < _min_cols) continue;

        auto tp = parse_us_timestamp(cols[static_cast<std::size_t>(_col_local_timestamp)]);
        Decimal bid, bid_size, ask, ask_size;
        try {
            bid      = Decimal::from_string(cols[static_cast<std::size_t>(_col_bid_price)]);
            bid_size = Decimal::from_string(cols[static_cast<std::size_t>(_col_bid_amount)]);
            ask      = Decimal::from_string(cols[static_cast<std::size_t>(_col_ask_price)]);
            ask_size = Decimal::from_string(cols[static_cast<std::size_t>(_col_ask_amount)]);
        } catch (...) { continue; }

        Quote quote;
        quote.bid      = bid;
        quote.bid_size = bid_size;
        quote.ask      = ask;
        quote.ask_size = ask_size;
        quote.time     = tp;
        ev.symbol = instrument();
        ev.time = tp;
        ev.data = quote;
        return true;
    }
    return false;
}

} // namespace quarkbot
