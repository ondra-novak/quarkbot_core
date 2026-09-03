#include "history_csv_source.hpp"
#include "csv_source.hpp"
#include "formats.hpp"
#include "quarkbot/hash/class_hash.hpp"
#include "quarkbot/log.hpp"
#include "quarkbot/stream/closedbar.hpp"
#include "quarkbot/stream/history.hpp"
#include "quarkbot/stream/quote.hpp"
#include "quarkbot/stream/trade.hpp"
#include "quarkbot/timestamp.hpp"
#include "quarkbot/utils/csv_reader.h"
#include "../streaming/callback_as_stream.hpp"
#include <chrono>
#include <exception>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <type_traits>

namespace quarkbot  {

    struct IndexFormat {
        std::string symbol;
        std::string file;
    };

    void HistoryCSVSourceConfig::reload_index() {
        try {
            if (index_file.empty()) throw std::runtime_error("No index file specified");
            std::ifstream inf(index_file);
            if (!inf) throw std::runtime_error("Can't open file");
            CSVReader reader([&]{return inf.get();});
            auto map = reader.mapColumns<IndexFormat>({
                    {"symbol",&IndexFormat::symbol},
                    {"file",&IndexFormat::file},
            });
            if (!map.allMapped) throw std::runtime_error("Index file has incorrect format: Required CSV with columns: symbol,file");
            IndexFormat target;
            auto base = index_file.parent_path();
            Index index;
            while (reader.readRow(map, target)) {
                if (target.symbol.empty() || target.file.empty()) continue;
                if (symbology_translate && !symbology_translate(target.symbol)) continue;
                index.emplace(std::move(target.symbol), base/target.file);
            }
            symbol_index = std::move(index);
        } catch (const std::exception &e) {
            throw std::runtime_error(std::format("CSV history: Failed to read index file {} - {}", index_file.string(), e.what()));
        }
    }

    struct TimeRange {
        ///first reported record time (from start_date and start_time of the request)
        Timestamp from;
        ///first not reported record time (from end_date of the request)
        Timestamp to;
        ///simulation time - a record which becomes known at or after it is the future
        Timestamp cut;
    };

    ///Turns a reader into a stream of the records that fall into the range
    /**
     * @param range the requested range and the simulation time
     * @param fname name of the file, for error reporting
     * @param reader reader of the records
     * @param predicate time of a record, which start_date and end_date of the request select by
     * @param known_at_predicate time at which a record became known, which the simulation
     *        time cuts by. It differs from the record's own time whenever a record covers a
     *        span of time - a bar is known only once it has closed.
     *
     * The file must be sorted by time - the stream ends at the first record outside the
     * range, so an unsorted file would be silently truncated. Rather than truncate,
     * a record older than its predecessor is reported as an error.
     */
    template<typename Reader, typename TargetType, typename TimePredicate, typename KnownAtPredicate>
    std::shared_ptr<IEventStreamBase> create_stream(TimeRange range, std::filesystem::path fname,
                            Reader &&reader, TargetType , TimePredicate predicate,
                            KnownAtPredicate known_at_predicate) {
        if (!reader.valid()) return {};
        using T = typename TargetType::type;
        auto callback = [range, fname = std::move(fname), reader = std::move(reader),
                         predicate = std::move(predicate), known_at_predicate = std::move(known_at_predicate),
                         last = Timestamp::min()](T &ref) mutable->bool  {
            T buff;
            while (true) {
                bool b = reader.read(buff);
                if (!b) return false;
                auto tp = predicate(buff);
                if (tp < last) throw std::runtime_error(std::format(
                        "CSV history: file {} is not sorted by time", fname.string()));
                last = tp;
                if (tp >= range.from)  {
                    if (tp >= range.to) return false;
                    if (known_at_predicate(buff) > range.cut) return false;
                    ref = buff;
                    return true;
                }
            }
        };
        return std::make_shared<CallbackSourceAsStream<T, decltype(callback)> >(std::move(callback));
    }

    ///a record which is a point in time is known the moment it happens
    template<typename Reader, typename TargetType, typename TimePredicate>
    std::shared_ptr<IEventStreamBase> create_stream(TimeRange range, std::filesystem::path fname,
                            Reader &&reader, TargetType t, TimePredicate predicate) {
        return create_stream(range, std::move(fname), std::move(reader), t, predicate, predicate);
    }


    static std::shared_ptr<IEventStreamBase> create_ohlc_stream(auto &&source, const std::filesystem::path &fname,
                    const TimeRange &range, std::chrono::seconds interval) {
        HistoryOHLCReader reader(std::move(source), interval);
        //a bar is selected by the time it opens, but is known only once it has closed
        return create_stream(range, fname, std::move(reader), std::type_identity<ClosedBar>{},
                [](const ClosedBar &x){return x.start_time;},
                [](const ClosedBar &x){return x.end_time;});
    }

    static std::shared_ptr<IEventStreamBase> create_close_stream(auto &&source, const std::filesystem::path &fname, const TimeRange &range) {
        HistoryTradeReader reader(std::move(source));
        return create_stream(range, fname, std::move(reader), std::type_identity<Trade>{}, [](const Trade &x){return x.time;});
    }

    static std::shared_ptr<IEventStreamBase> create_l1_trade_stream(auto &&source, const std::filesystem::path &fname, const TimeRange &range) {
        HistoryL1TradeReader reader(std::move(source));
        return create_stream(range, fname, std::move(reader), std::type_identity<Trade>{}, [](const Trade &x){return x.time;});
    }

    static std::shared_ptr<IEventStreamBase> create_auction_daily_stream(auto &&source, const std::filesystem::path &fname, const TimeRange &range) {
        HistoryAuctionReader reader(std::move(source));
        return create_stream(range, fname, std::move(reader), std::type_identity<AuctionDailyHistory>{}, [](const AuctionDailyHistory &x){
            std::chrono::system_clock::time_point tp =
                std::chrono::time_point<std::chrono::system_clock, std::chrono::days>(std::chrono::sys_days{x.day})+std::chrono::hours(12);
            return tp;
        });
    }

    static std::shared_ptr<IEventStreamBase> create_quote_stream(auto &&source, const std::filesystem::path &fname, const TimeRange &range) {
        HistoryQuoteReader reader(std::move(source));
        return create_stream(range, fname, std::move(reader), std::type_identity<Quote>{}, [](const Quote &x){return x.time;});
    }

    std::shared_ptr<IEventStreamBase> HistoryCSVSourceConfig::get_stream(
                                                    const std::string &symbol,
                                                    std::size_t type_hash,
                                                    std::chrono::year_month_day start_date,
                                                    std::chrono::year_month_day end_date,
                                                    std::chrono::hh_mm_ss<std::chrono::seconds> start_time,
                                                    Timestamp backtest_cut_time) {

            if (!symbol_index) reload_index();

            //a source of the wrong type serves nothing, do not even open the file
            bool supported;
            switch (type_hash) {
                case class_hash<ClosedBar>: supported = type == ohlc; break;
                case class_hash<Trade>: supported = type == close || type == l1; break;
                case class_hash<AuctionDailyHistory>: supported = type == auction; break;
                case class_hash<Quote>: supported = type == quote || type == l1; break;
                default: supported = false; break;
            }
            if (!supported) return {};

            auto iter = symbol_index->find(symbol);
            if (iter == symbol_index->end()) return {};

            TimeRange r;
            auto st = std::chrono::time_point<std::chrono::system_clock, std::chrono::days>(std::chrono::sys_days{start_date});
            auto en = std::chrono::time_point<std::chrono::system_clock, std::chrono::days>(std::chrono::sys_days{end_date});
            r.from = std::chrono::time_point_cast<std::chrono::system_clock::duration>(st) + start_time.to_duration();
            r.to = std::chrono::time_point_cast<std::chrono::system_clock::duration>(en)+std::chrono::hours(24);
            r.cut = backtest_cut_time;

            const auto &fname = iter->second;
            auto stream = prepare_csv_source(fname, [&](auto source) -> std::shared_ptr<IEventStreamBase>{
                switch (type_hash) {
                    case class_hash<ClosedBar>:
                        return create_ohlc_stream(std::move(source), fname, r, std::chrono::seconds(interval));
                    case class_hash<Trade>:
                        if (type == l1) return create_l1_trade_stream(std::move(source), fname, r);
                        return create_close_stream(std::move(source), fname, r);
                    case class_hash<AuctionDailyHistory>:
                        return create_auction_daily_stream(std::move(source), fname, r);
                    case class_hash<Quote>:
                        return create_quote_stream(std::move(source), fname, r);
                    default:
                        return {};
                }
            });
            if (!stream) {
                logWarning("CSV history: file {} (symbol {}, index {}) does not have the columns of a `{}` file",
                        fname.string(), symbol, index_file.string(),
                        string_lookup<Type>(type).value_or("?"));
            }
            return stream;
        }


}
