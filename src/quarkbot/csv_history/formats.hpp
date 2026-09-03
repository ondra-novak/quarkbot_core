#pragma once

#include "quarkbot/stream/closedbar.hpp"
#include "quarkbot/stream/history.hpp"
#include "quarkbot/stream/quote.hpp"
#include "quarkbot/timestamp.hpp"
#include "quarkbot/utils/csv_reader.h"
#include "quarkbot/utils/string_utils.hpp"
#include <chrono>
#include <sstream>
#include <type_traits>
namespace quarkbot {

template<typename T>
concept CSVReaderSource = std::is_invocable_r_v<int, T>;


inline Timestamp string2time(const std::string &text) {
    auto sep = text.find('T')    ;
    std::chrono::sys_time<std::chrono::nanoseconds> tp;
    bool has_t =(sep != text.npos);
    bool has_time = (text.find(':') != text.npos);    
    std::istringstream in{text};
    if (has_t) {
        in >> std::chrono::parse("%FT%T", tp);
    } else if (has_time) {
        in >> std::chrono::parse("%F %T", tp);
    } else {
        in >> std::chrono::parse("%F", tp);
    }
    return tp;    
}

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
        std::size_t trades;
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
        if (!_flds.open.empty()) item.open = Decimal::from_string(_flds.open);
        item.close = Decimal::from_string(_flds.close);
        if (!_flds.high.empty()) item.high = Decimal::from_string(_flds.high);
        if (!_flds.low.empty()) item.low = Decimal::from_string(_flds.low);
        if (!_flds.volume.empty()) item.volume = Decimal::from_string(_flds.volume);
        item.trades = _flds.trades;
        item.start_time = string2time(_flds.time);
        item.end_time = item.start_time+_interval;
        return true;
    }

protected:
    CSVReader<_Source> _csv;
    CSVFieldIndexMapping<Fields> _mapping;
    std::chrono::seconds _interval;
    Fields _flds;
};

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
        return _mapping.allMapped;
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
    Fields _flds;
};

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
    Fields _flds;
};

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
    Fields _flds;
};

}