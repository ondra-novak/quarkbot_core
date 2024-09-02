#include "replay.h"
#include <quarkbot/csv.h>

#include <ctime>
#include <fstream>
namespace quarkbot {

namespace Replay {


class CSVReplaySource {
public:

    CSVReplaySource(std::istream &infile,
            std::chrono::system_clock::time_point initial_time,
            double speed,
            double offset)
    :_csv(Src(check_file(infile)))
    ,_initial_time_point(initial_time)
    ,_speed(speed)
    ,_offset(offset) {
        open_csv();
    }

    struct CSVLayout {
        double time;
        std::string symbol;
        std::string bid_price;
        double bid_size;
        std::string ask_price;
        double ask_size;
        std::string trade_price;
        double trade_size;
        std::string index_price;
    };

    const Data * operator()();

protected:

    struct Src {
        std::istream &in;
        Src(std::istream &in):in(in) {}
        int operator()() {return in.get();}
    };

    CSVReader<Src> _csv;
    std::chrono::system_clock::time_point _initial_time_point;
    double _speed;
    double _offset;
    CSVFieldIndexMapping<CSVLayout> _mapping;
    CSVLayout _data;

    std::unordered_map<std::string,Data> _result_map = {};
    std::chrono::system_clock::time_point _prevtp = std::chrono::system_clock::time_point::min();
    void open_csv();

    std::istream &check_file(std::istream &f);
};


class CSVReplaySourceFile {
public:
    CSVReplaySourceFile(const std::string &f,
            std::chrono::system_clock::time_point initial_time,
            double speed,
            double offset)
        :fin(f),src(fin, initial_time, speed, offset) {}
    const Data * operator()() {
        return src();
    }
protected:
    std::ifstream fin;
    CSVReplaySource src;
};

Source create(const std::string &fname,
        std::chrono::system_clock::time_point initial_time,
        double speed, double offset) {
    return CSVReplaySourceFile(fname, initial_time, speed, offset);
}
Source create(std::istream &infile,
        std::chrono::system_clock::time_point initial_time,
        double speed, double offset) {
    return CSVReplaySource(infile, initial_time, speed, offset);
}

void CSVReplaySource::open_csv() {
    _mapping = _csv.mapColumns<CSVLayout>({
        {"timestamp", &CSVLayout::time},
        {"symbol", &CSVLayout::symbol},
        {"bid", &CSVLayout::bid_price},
        {"ask", &CSVLayout::ask_price},
        {"bid_size", &CSVLayout::bid_size},
        {"ask_size", &CSVLayout::ask_size},
        {"trade", &CSVLayout::trade_price},
        {"volume", &CSVLayout::trade_size},
        {"index", &CSVLayout::index_price}
    });

    if (!_mapping.isMapped(&CSVLayout::time)) {
        throw std::runtime_error("REPLAY FILE: missing 'timestamp' column");
    }
    if (!_mapping.isMapped(&CSVLayout::symbol)) {
        throw std::runtime_error("REPLAY FILE: missing 'symbol' column");
    }
}

std::chrono::system_clock::time_point parseTimestamp(const std::string& timestamp) {
    int year = 1970;
    int month = 1;
    int day = 1;
    int hour = 0;
    int minute = 0;
    double seconds = 0.0;
    if (sscanf(timestamp.c_str(), "%d-%d-%d %d:%d:%lf", &year, &month, &day, &hour, &minute, &seconds) != 6) {
        return std::chrono::system_clock::time_point::min();
    }
    std::tm tminfo = {};
    tminfo.tm_year = year-1900;
    tminfo.tm_mon = month-1;
    tminfo.tm_mday = day;
    tminfo.tm_hour = hour;
    tminfo.tm_min = minute;
    std::time_t time_t_value = std::mktime(&tminfo);
    auto time_point = std::chrono::system_clock::from_time_t(time_t_value);
    std::uint64_t nanos = static_cast<std::uint64_t>(seconds * 1e9);
    time_point += std::chrono::nanoseconds(nanos);
    return time_point;
}

inline const Data * CSVReplaySource::operator ()() {
    while (true) {
        if (!_csv.readRow(_mapping, _data)) return {};
        constexpr auto cast_multiplier = std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::seconds(1)).count();
        std::chrono::system_clock::duration tprel( static_cast<std::uint64_t>((_data.time+_offset)*_speed * cast_multiplier));
        auto tp = _initial_time_point + tprel;

        if (tp == std::chrono::system_clock::time_point::min() || tp < _prevtp)
            continue;

        _prevtp = tp;
        auto &result = _result_map[_data.symbol];
        result.tp = tp;
        result.ask = Decimal(_data.ask_price);
        result.bid = Decimal(_data.bid_price);
        if (!_data.trade_price.empty()) {
            auto d = Decimal(_data.trade_price);
            if (d) {
                result.last = d;
                ++result.cum_trades;
            }
        }
        result.index = Decimal(_data.index_price);
        result.ask_volume = _data.ask_size;
        result.bid_volume = _data.bid_size;
        result.cum_volume += _data.trade_size;
        result.symbol_id = _data.symbol;
        return &result;
    }
}

std::istream& CSVReplaySource::check_file(std::istream &f) {
    if (!f) throw std::runtime_error("Replay file open failed");
    return f;
}

}

}

