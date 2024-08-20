#include "replay.h"
#include <trading_api/csv.h>

#include <ctime>
#include <fstream>
namespace trading_api {

namespace Replay {


class CSVReplaySource {
public:

    CSVReplaySource(std::istream &infile):_csv(Src(check_file(infile))) {
        open_csv();
    }

    struct CSVLayout {
        std::string time;
        std::string symbol;
        std::string bid_price;
        double bid_size;
        std::string ask_price;
        double ask_size;
        std::string trade_price;
        double trade_size;
        std::string index_price;
    };

    std::optional<Data> operator()();

protected:

    struct Src {
        std::istream &in;
        Src(std::istream &in):in(in) {}
        int operator()() {return in.get();}
    };

    CSVReader<Src> _csv;
    CSVFieldIndexMapping<CSVLayout> _mapping;
    CSVLayout _data;
    std::chrono::system_clock::time_point _prevtp = std::chrono::system_clock::time_point::min();
    void open_csv();

    std::istream &check_file(std::istream &f);
};


class CSVReplaySourceFile {
public:
    CSVReplaySourceFile(const std::string &f)
        :fin(f),src(fin) {}
    std::optional<Data> operator()() {
        return src();
    }
protected:
    std::ifstream fin;
    CSVReplaySource src;
};

Source create(const std::string &fname) {
    return CSVReplaySourceFile(fname);
}
Source create(std::istream &infile) {
    return CSVReplaySource(infile);
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

inline std::optional<Data> CSVReplaySource::operator ()() {
    while (true) {
        if (!_csv.readRow(_mapping, _data)) return {};
        auto tp = parseTimestamp(_data.time);

        if (tp == std::chrono::system_clock::time_point::min() || tp < _prevtp)
            continue;

        _prevtp = tp;
        Data ret{
            {
                tp,
                Decimal(_data.bid_price),
                Decimal(_data.ask_price),
                Decimal(_data.trade_price),
                Decimal(_data.index_price),
                _data.bid_size,
                _data.ask_size,
                _data.trade_size,
            },
            _data.symbol
        };
        return ret;
    }
}

std::istream& CSVReplaySource::check_file(std::istream &f) {
    if (!f) throw std::runtime_error("Replay file open failed");
    return f;
}

class Aggregator {
public:

    Aggregator (std::vector<Source> sources)
        :sources(std::move(sources)) {
        std::transform(sources.begin(), sources.end(), std::back_inserter(next),
                [](Source &s){return s();});
    }
    std::optional<Data> operator()() {
        auto iter = std::min_element(next.begin(), next.end(),
                [](const std::optional<Data> &a,const std::optional<Data> &b) {
            auto ta = a.has_value()?std::chrono::system_clock::time_point::max():a->tp;
            auto tb = b.has_value()?std::chrono::system_clock::time_point::max():b->tp;
            return ta < tb;
        });
        auto idx = std::distance(next.begin(), iter);
        std::optional<Data> r = std::move(next[idx]);
        next[idx] = sources[idx]();
        return r;
    }

protected:
    std::vector<Source> sources;
    std::vector<std::optional<Data> > next;


};

Source aggregate(std::vector<Source> sources) {
    if (sources.empty()) return []{return std::optional<Data>();};
    return Aggregator(std::move(sources));


}

}

}

