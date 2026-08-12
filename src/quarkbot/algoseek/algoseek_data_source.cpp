#include "algoseek_data_source.hpp"

#include "quarkbot/log.hpp"
#include <array>
#include <charconv>
#include <format>
#include <memory>
#include <stdexcept>
#include <utility>
#include <zlib.h>

namespace quarkbot {

int AlgoseekDataSource::CSVSource::operator()() {
    if (cur_line.empty()) cur_line = block_reader();
    if (cur_line.empty()) return -1;
    unsigned char c = static_cast<unsigned char>(cur_line.front());
    cur_line.remove_prefix(1);
    return static_cast<int>(c);
}

AlgoseekDataSource::CSVSource AlgoseekDataSource::init_source(
        const std::filesystem::path &file) {
    #if defined(_WIN32)
        auto gzf = gzopen_w(file.c_str(), "r");
    #else
        auto gzf = gzopen(file.c_str(), "r");
    #endif
    if (gzf == nullptr) {
        throw std::runtime_error(std::format(
            "Algoseek source: failed to open gz file: {}", file.string()));
    }
    auto shared_gzf = std::shared_ptr<struct gzFile_s>(gzf, [](gzFile f){gzclose(f);});
    return CSVSource{
        [shared_gzf, buff = std::array<char, 65536>()]() mutable -> std::string_view {
            int r = gzread(shared_gzf.get(), buff.data(), static_cast<unsigned int>(buff.size()));
            if (r > 0) return {buff.data(), static_cast<std::size_t>(r)};
            if (r == 0 && gzeof(shared_gzf.get())) return {};
            int errnum;
            const char *err = gzerror(shared_gzf.get(), &errnum);
            throw std::runtime_error(std::format("Algoseek source: gz error {}: {}", errnum, err));
        },
    };
}

CSVFieldIndexMapping<AlgoseekDataSource::Data> AlgoseekDataSource::map_columns(
        CSVReader<CSVSource> &csv, const std::filesystem::path &file) {
    auto colmap = csv.mapColumns<Data>({
        {"Date", &Data::date},
        {"Timestamp", &Data::timestamp},
        {"EventType", &Data::event_type},
        {"Ticker", &Data::ticker},
        {"Price", &Data::price},
        {"Quantity", &Data::quantity},
        {"Exchange", &Data::exchange},
        {"Conditions", &Data::conditions},
    });
    if (!colmap.allMapped) {
        std::string missing;
        auto check = [&](std::string_view name, auto ptr) {
            if (!colmap.isMapped(ptr)) {
                if (!missing.empty()) missing.append(", ");
                missing.append(name);
            }
        };
        check("Date", &Data::date);
        check("Timestamp", &Data::timestamp);
        check("EventType", &Data::event_type);
        check("Ticker", &Data::ticker);
        check("Price", &Data::price);
        check("Quantity", &Data::quantity);
        check("Exchange", &Data::exchange);
        check("Conditions", &Data::conditions);
        throw std::runtime_error(std::format(
            "Algoseek source {}: missing header column(s): {}", file.string(), missing));
    }
    return colmap;
}

AlgoseekDataSource::AlgoseekDataSource(AlgoseekSpec spec)
    :_spec(std::move(spec))
    ,_tz(_spec.tz)
    ,_csv(init_source(_spec.file))
    ,_colmap(map_columns(_csv, _spec.file))
{
}

void AlgoseekDataSource::row_error(std::string_view message) const {
    throw std::runtime_error(std::format(
        "Algoseek source {}: row {}: {}", _spec.file.string(), _line, message));
}

void AlgoseekDataSource::check_required_fields() const {
    auto check = [&](std::string_view name, const std::string &value) {
        if (value.empty()) row_error(std::format("column {} is empty", name));
    };
    check("Date", _row.date);
    check("Timestamp", _row.timestamp);
    check("EventType", _row.event_type);
    check("Ticker", _row.ticker);
    check("Price", _row.price);
    check("Quantity", _row.quantity);
    check("Exchange", _row.exchange);
    check("Conditions", _row.conditions);
}

std::uint32_t AlgoseekDataSource::parse_conditions() const {
    std::string_view s = _row.conditions;
    std::uint32_t value = 0;
    auto res = std::from_chars(s.data(), s.data() + s.size(), value, 16);
    if (s.size() != 8 || res.ec != std::errc{} || res.ptr != s.data() + s.size()) {
        row_error(std::format(
            "Conditions '{}' is not 8 hexadecimal digits; the file may have shifted "
            "columns (expected 8 fields, decimal point as '.')", s));
    }
    return value;
}

std::chrono::local_time<std::chrono::nanoseconds> AlgoseekDataSource::parse_local_time() const {
    auto number = [&](std::string_view s) {
        int v = 0;
        auto res = std::from_chars(s.data(), s.data() + s.size(), v);
        if (res.ec != std::errc{} || res.ptr != s.data() + s.size()) {
            row_error(std::format("'{}' is not a number", s));
        }
        return v;
    };

    std::string_view date = _row.date;
    if (date.size() != 8) row_error(std::format("Date '{}' is not YYYYMMDD", date));
    std::chrono::year_month_day ymd{
        std::chrono::year{number(date.substr(0, 4))},
        std::chrono::month{static_cast<unsigned>(number(date.substr(4, 2)))},
        std::chrono::day{static_cast<unsigned>(number(date.substr(6, 2)))}};
    if (!ymd.ok()) row_error(std::format("Date '{}' is not a valid date", date));

    std::string_view time = _row.timestamp;
    if (time.size() != 18 || time[2] != ':' || time[5] != ':' || time[8] != '.') {
        row_error(std::format("Timestamp '{}' is not HH:MM:SS.nnnnnnnnn", time));
    }
    int hh = number(time.substr(0, 2));
    int mm = number(time.substr(3, 2));
    int ss = number(time.substr(6, 2));
    int ns = number(time.substr(9, 9));
    if (hh > 23 || mm > 59 || ss > 59) {
        row_error(std::format("Timestamp '{}' is out of range", time));
    }

    return std::chrono::local_time<std::chrono::nanoseconds>{
        std::chrono::local_days{ymd}.time_since_epoch()
        + std::chrono::hours{hh} + std::chrono::minutes{mm}
        + std::chrono::seconds{ss} + std::chrono::nanoseconds{ns}};
}

void AlgoseekDataSource::log_summary() const {
    bool suspicious = _counters.unknown_event > 0
            || (_counters.trades == 0 && _counters.auctions == 0);
    auto level = suspicious ? LogLevel::warning : LogLevel::info;
    logOutput(level, "Algoseek source {}: {} trades, {} auctions; skipped: "
            "{} cancelled, {} unknown event, {} other exchange, {} zero quantity, "
            "{} zero price, {} official print",
            _spec.file.string(), _counters.trades, _counters.auctions,
            _counters.cancelled, _counters.unknown_event, _counters.filtered_exchange,
            _counters.zero_qty, _counters.zero_price, _counters.official_print);
}

bool AlgoseekDataSource::operator()(BacktestEvent &ev) {
    if (_eof) return false;

    while (true) {
        if (!_csv.readRow(_colmap, _row)) {
            _eof = true;
            log_summary();
            return false;
        }
        ++_line;

        check_required_fields();
        if (_first_ticker.empty()) {
            _first_ticker = _row.ticker;
        } else if (_row.ticker != _first_ticker) {
            row_error(std::format("Ticker changed from '{}' to '{}'; one file must "
                    "hold one ticker", _first_ticker, _row.ticker));
        }

        //cancellations cannot be undone once replayed, so the cancelling row is
        //dropped and the cancelled trade stays; see the design spec for why a
        //look-ahead window would not work
        if (_row.event_type == "TRADE CANCELLED") {
            ++_counters.cancelled;
            continue;
        }
        if (_row.event_type != "TRADE" && _row.event_type != "TRADE NB") {
            if (_counters.unknown_event == 0) {
                logWarning("Algoseek source {}: row {}: unexpected EventType '{}', "
                        "skipping (further occurrences are only counted)",
                        _spec.file.string(), _line, _row.event_type);
            }
            ++_counters.unknown_event;
            continue;
        }
        if (!_spec.exchange.empty() && _row.exchange != _spec.exchange) {
            ++_counters.filtered_exchange;
            continue;
        }

        auto quantity = Decimal::from_string(_row.quantity);
        if (quantity <= 0) {
            ++_counters.zero_qty;
            continue;
        }
        auto price = Decimal::from_string(_row.price);
        if (price <= 0) {
            ++_counters.zero_price;
            continue;
        }

        auto flags = parse_conditions();

        AuctionType auction_type = AuctionType::unknown;
        if (has_flag(flags, AlgoseekTradeFlag::opening_prints)) {
            auction_type = AuctionType::opening;
        } else if (has_flag(flags, AlgoseekTradeFlag::closing_prints)) {
            auction_type = AuctionType::closing;
        } else if (has_flag(flags, AlgoseekTradeFlag::reopening_prints)) {
            //a reopening auction follows a halt, so it is unscheduled rather
            //than a scheduled midday auction
            auction_type = AuctionType::unscheduled;
        }

        //official open/close rows repeat the price of a real print, sometimes
        //hours later; they are not executions. Checked after the auction flags
        //so that a row carrying both is read as the auction it reports.
        if (auction_type == AuctionType::unknown
                && (has_flag(flags, AlgoseekTradeFlag::official_close)
                    || has_flag(flags, AlgoseekTradeFlag::official_open))) {
            ++_counters.official_print;
            continue;
        }

        auto time = _tz.to_sys(parse_local_time());
        if (time < _last_time) {
            row_error(std::format("timestamp {} is earlier than the previous event at {}; "
                    "MergedDataSource requires each source to be ordered", time, _last_time));
        }
        _last_time = time;
        ev.symbol = _spec.symbol.empty() ? _row.ticker : _spec.symbol;
        ev.time = time;

        if (auction_type != AuctionType::unknown) {
            Auction &a = ev.data.emplace<Auction>();
            a.auction_type = auction_type;
            //the export carries no indicative data, so the print is final and
            //the unavailable fields take defaults
            a.final = true;
            a.price = price;
            a.quantity = quantity;
            a.quantity_traded = quantity;
            a.imbalance = 0;
            a.time = time;
            ++_counters.auctions;
        } else {
            Trade &t = ev.data.emplace<Trade>();
            t.price = price;
            t.size = quantity;
            t.time = time;
            //the export carries no aggressor side
            t.side = Side::undetermined;
            ++_counters.trades;
        }
        return true;
    }
}

}
