#pragma once

#include "quarkbot/stream/closedbar.hpp"
#include "quarkbot/stream/history.hpp"
#include "quarkbot/stream/quote.hpp"
#include "quarkbot/stream/trade.hpp"
#include "quarkbot/timestamp.hpp"
#include "quarkbot/utils/csv_reader.h"
#include "quarkbot/utils/string_utils.hpp"
#include <chrono>
#include <format>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <type_traits>
namespace quarkbot {

template<typename T>
concept CSVReaderSource = std::is_invocable_r_v<int, T>;


///Parses a timestamp of a history CSV file
/**
 * Accepted forms (all interpreted in the instrument's time zone, the reader itself
 * does no zone conversion):
 *
 *   2026-06-26                    - date only, midnight
 *   2026-06-26 10:00:00           - date and time separated by a space
 *   2026-06-26T10:00:00           - ISO 8601 separator
 *   2026-06-26T10:00:00.123456    - fractional seconds
 *   2026-06-26T10:00:00Z          - trailing zone designator is accepted and ignored
 *
 * @exception std::runtime_error the text is not a timestamp in any of the accepted forms
 */
inline Timestamp string2time(const std::string &text) {
    std::chrono::sys_time<std::chrono::nanoseconds> tp;
    bool has_t = (text.find('T') != text.npos);
    bool has_time = (text.find(':') != text.npos);
    std::istringstream in{text};
    if (has_t) {
        in >> std::chrono::parse("%FT%T", tp);
    } else if (has_time) {
        in >> std::chrono::parse("%F %T", tp);
    } else {
        in >> std::chrono::parse("%F", tp);
    }
    if (in.fail()) throw std::runtime_error(std::format(
            "Invalid timestamp `{}`, expected YYYY-MM-DD[(T| )HH:MM:SS[.frac]]", text));
    return tp;
}

///Interval of a history CSV file, as written in the `interval` key of the configuration
/**
 * Accepted forms: a bare count of seconds (`900`), a unit letter (`s`,`m`,`h`,`d`,`w`)
 * or a count followed by a unit letter (`5m`, `15m`, `1h`). An empty text means
 * "no interval" (tick data) and yields HistoryDataRequest::interval_undefined.
 *
 * @exception std::runtime_error the text is not an interval in any of the accepted forms
 */
inline HistoryDataRequest::Interval parse_history_interval(std::string_view text) {
    text = trim(text);
    if (text.empty()) return HistoryDataRequest::interval_undefined;
    std::size_t pos = 0;
    std::size_t count = 0;
    while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
        count = count * 10 + static_cast<std::size_t>(text[pos] - '0');
        ++pos;
    }
    bool has_count = pos > 0;
    auto unit = text.substr(pos);
    if (unit.empty()) {
        if (!has_count) throw std::runtime_error(std::format("Invalid interval `{}`", text));
        return count;   //a bare number is a count of seconds
    }
    if (!has_count) count = 1;
    HistoryDataRequest::Interval mult;
    if (unit == "s") mult = HistoryDataRequest::interval_second;
    else if (unit == "m") mult = HistoryDataRequest::interval_minute;
    else if (unit == "h") mult = HistoryDataRequest::interval_hour;
    else if (unit == "d") mult = HistoryDataRequest::interval_day;
    else if (unit == "w") mult = HistoryDataRequest::interval_week;
    else throw std::runtime_error(std::format(
            "Invalid interval `{}`, expected a count of seconds or [count]s|m|h|d|w", text));
    return count * mult;
}

///Reads `time,open,high,low,close,volume[,trades]` as ClosedBar (config type `ohlc`)
template<CSVReaderSource _Source>
class HistoryOHLCReader {
public:

    struct Fields {
        std::string time;
        std::string open;
        std::string high;
        std::string low;
        std::string close;
        std::string volume;
        std::uint64_t trades;
    };

    HistoryOHLCReader(_Source source, std::chrono::seconds interval):_csv(std::move(source)),_interval(interval) {
        _mapping = _csv.template mapColumns<Fields>({
            {"time", &Fields::time},
            {"open", &Fields::open},
            {"high", &Fields::high},
            {"low", &Fields::low},
            {"close", &Fields::close},
            {"volume", &Fields::volume},
            {"trades", &Fields::trades},
        });
    }

    bool valid() const {
        return _mapping.isMapped(&Fields::time) && _mapping.isMapped(&Fields::close);
    }

    bool read(ClosedBar &item) {
        auto r = _csv.readRow(_mapping, _flds);
        if (!r) return false;
        item.close = Decimal::from_string(_flds.close);
        item.open = _flds.open.empty()?item.close:Decimal::from_string(_flds.open);
        item.high = _flds.high.empty()?item.close:Decimal::from_string(_flds.high);
        item.low = _flds.low.empty()?item.close:Decimal::from_string(_flds.low);
        item.volume = _flds.volume.empty()?Decimal{}:Decimal::from_string(_flds.volume);
        item.trades = static_cast<std::size_t>(_flds.trades);
        item.start_time = string2time(_flds.time);
        item.end_time = item.start_time+_interval;
        return true;
    }

protected:
    CSVReader<_Source> _csv;
    CSVFieldIndexMapping<Fields> _mapping;
    std::chrono::seconds _interval;
    Fields _flds = {};
};

///Reads `time,close,volume` as Trade (config type `close`)
template<CSVReaderSource _Source>
class HistoryTradeReader {
public:

    struct Fields {
        std::string time;
        std::string close;
        std::string volume;
    };

    HistoryTradeReader(_Source source):_csv(std::move(source)) {
        _mapping = _csv.template mapColumns<Fields>({
            {"time", &Fields::time},
            {"close", &Fields::close},
            {"volume", &Fields::volume}
        });
    }

    bool valid() const {
        return _mapping.isMapped(&Fields::time) && _mapping.isMapped(&Fields::close);
    }

    bool read(Trade &item) {
        auto r = _csv.readRow(_mapping, _flds);
        if (!r) return false;
        item.price = Decimal::from_string(_flds.close);
        item.size = Decimal::from_string(_flds.volume);
        item.time = string2time(_flds.time);
        return true;
    }

protected:
    CSVReader<_Source> _csv;
    CSVFieldIndexMapping<Fields> _mapping;
    Fields _flds = {};
};

///Reads `time,open_price,open_volume,close_price,close_volume` as AuctionDailyHistory (config type `auction`)
template<CSVReaderSource _Source>
class HistoryAuctionReader {
public:

    struct Fields {
        std::string time;
        std::string open_price;
        std::string open_volume;
        std::string close_price;
        std::string close_volume;
    };

    HistoryAuctionReader(_Source source):_csv(std::move(source)) {
        _mapping = _csv.template mapColumns<Fields>({
            {"time", &Fields::time},
            {"open_price",&Fields::open_price},
            {"open_volume",&Fields::open_volume},
            {"close_price",&Fields::close_price},
            {"close_volume",&Fields::close_volume}
        });
    }

    bool valid() const {
        return _mapping.allMapped;
    }

    bool read(AuctionDailyHistory &item) {
        auto r = _csv.readRow(_mapping, _flds);
        if (!r) return false;
        item.open_price = Decimal::from_string(_flds.open_price);
        item.open_quantity = Decimal::from_string(_flds.open_volume);
        item.close_price = Decimal::from_string(_flds.close_price);
        item.close_quantity = Decimal::from_string(_flds.close_volume);
        auto tp = string2time(_flds.time);
        item.day = std::chrono::time_point_cast<std::chrono::days>(tp);
        return true;
    }

protected:
    CSVReader<_Source> _csv;
    CSVFieldIndexMapping<Fields> _mapping;
    Fields _flds = {};
};

///Reads `time,ask_price,ask_volume,bid_price,bid_volume` as Quote (config type `quote` and `l1`)
template<CSVReaderSource _Source>
class HistoryQuoteReader {
public:

    struct Fields {
        std::string time;
        std::string bid_price;
        std::string bid_volume;
        std::string ask_price;
        std::string ask_volume;
    };

    HistoryQuoteReader(_Source source):_csv(std::move(source)) {
        _mapping = _csv.template mapColumns<Fields>({
            {"time", &Fields::time},
            {"bid_price",&Fields::bid_price},
            {"bid_volume",&Fields::bid_volume},
            {"ask_price",&Fields::ask_price},
            {"ask_volume",&Fields::ask_volume}
        });
    }

    bool valid() const {
        return _mapping.allMapped;
    }

    bool read(Quote &item) {
        auto r = _csv.readRow(_mapping, _flds);
        if (!r) return false;
        item.ask = Decimal::from_string(_flds.ask_price);
        item.ask_size = Decimal::from_string(_flds.ask_volume);
        item.bid = Decimal::from_string(_flds.bid_price);
        item.bid_size = Decimal::from_string(_flds.bid_volume);
        item.time = string2time(_flds.time);
        return true;
    }

protected:
    CSVReader<_Source> _csv;
    CSVFieldIndexMapping<Fields> _mapping;
    Fields _flds = {};
};

///Reads the trade half of `time,ask_price,ask_volume,bid_price,bid_volume,price,volume` (config type `l1`)
/**
 * An l1 row carries a quote and, optionally, the trade that moved it. Rows with
 * a zero `price` carry no trade and are skipped, so the stream produced by this
 * reader contains only the rows which really traded.
 */
template<CSVReaderSource _Source>
class HistoryL1TradeReader {
public:

    struct Fields {
        std::string time;
        std::string price;
        std::string volume;
    };

    HistoryL1TradeReader(_Source source):_csv(std::move(source)) {
        _mapping = _csv.template mapColumns<Fields>({
            {"time", &Fields::time},
            {"price", &Fields::price},
            {"volume", &Fields::volume}
        });
    }

    bool valid() const {
        return _mapping.isMapped(&Fields::time) && _mapping.isMapped(&Fields::price);
    }

    bool read(Trade &item) {
        while (true) {
            if (!_csv.readRow(_mapping, _flds)) return false;
            auto price = Decimal::from_string(_flds.price);
            if (!price) continue;   //no trade on this row
            item.price = price;
            item.size = Decimal::from_string(_flds.volume);
            item.time = string2time(_flds.time);
            return true;
        }
    }

protected:
    CSVReader<_Source> _csv;
    CSVFieldIndexMapping<Fields> _mapping;
    Fields _flds = {};
};

}
