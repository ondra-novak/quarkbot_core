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
                if (symbology_translate)  {
                    if (symbology_translate(target.symbol)) {
                        index.emplace(std::move(target.symbol), base/target.file);
                    }
                } else {
                    index.emplace(std::move(target.symbol), base/target.file);
                }
            }
            symbol_index = std::move(index);
        } catch (const std::exception &e) {
            throw std::runtime_error(std::format("CSV history: Failed to read index file {} - {}", index_file.string(), e.what()));
        }
    }

    struct TimeRange {
        Timestamp from;
        Timestamp to;
    };

    template<typename Reader, typename TargetType, typename TimePredicate>
    std::shared_ptr<IEventStreamBase> create_stream(TimeRange range, Reader &&reader, TargetType , TimePredicate predicate) {
        if (!reader.valid()) return {};
        using T = typename TargetType::type;
        auto callback = [range, reader = std::move(reader), predicate = std::move(predicate)](T &ref) mutable->bool  {
            T buff;
            while (true) {
                bool b = reader.read(buff);
                if (!b) return false;
                auto tp = predicate(buff);
                if (tp >= range.from)  {
                    if (tp >= range.to) return false;
                    ref = buff;
                    return true;
                }
            }
        };
        return std::make_shared<CallbackSourceAsStream<T, decltype(callback)> >(std::move(callback));
    }


    std::shared_ptr<IEventStreamBase> create_ohlc_stream(auto &&source, const TimeRange &range, std::chrono::seconds interval) {
        HistoryOHLCReader reader(std::move(source), interval);
        return create_stream(range, std::move(reader), std::type_identity<ClosedBar>{}, [](const ClosedBar &x){return x.start_time;});
    }

    std::shared_ptr<IEventStreamBase> create_close_stream(auto &&source, const TimeRange &range) {
        HistoryTradeReader reader(std::move(source));
        return create_stream(range, std::move(reader), std::type_identity<Trade>{}, [](const Trade &x){return x.time;});
    }

    std::shared_ptr<IEventStreamBase> create_auction_daily_stream(auto &&source, const TimeRange &range) {
        HistoryAuctionReader reader(std::move(source));
        return create_stream(range, std::move(reader), std::type_identity<AuctionDailyHistory>{}, [](const AuctionDailyHistory &x){
            std::chrono::system_clock::time_point tp = 
                std::chrono::time_point<std::chrono::system_clock, std::chrono::days>(std::chrono::sys_days{x.day})+std::chrono::hours(12);
            return tp;
        });
    }

    std::shared_ptr<IEventStreamBase> create_quote_stream(auto &&source, const TimeRange &range) {
        HistoryQuoteReader reader(std::move(source));
        return create_stream(range, std::move(reader), std::type_identity<Quote>{}, [](const Quote &x){return x.time;});
    }

    std::shared_ptr<IEventStreamBase> HistoryCSVSourceConfig::get_stream(
                                                    const std::string &symbol,
                                                    std::size_t type_hash,
                                                    std::chrono::year_month_day start_date,
                                                    std::chrono::year_month_day end_date,
                                                    std::chrono::hh_mm_ss<std::chrono::seconds> start_time,
                                                    Timestamp backtest_cut_time) {

            if (!symbol_index) reload_index();

            TimeRange r;
            auto st = std::chrono::time_point<std::chrono::system_clock, std::chrono::days>(std::chrono::sys_days{start_date});
            auto en = std::chrono::time_point<std::chrono::system_clock, std::chrono::days>(std::chrono::sys_days{end_date});
            r.from = std::chrono::time_point_cast<std::chrono::system_clock::duration>(st) + start_time.to_duration();
            r.to = std::min(backtest_cut_time, std::chrono::time_point_cast<std::chrono::system_clock::duration>(en)+std::chrono::hours(24));
            

            
            auto iter = symbol_index->find(symbol);
            if (iter == symbol_index->end()) return {};

            return prepare_csv_source(iter->second, [&](auto source) -> std::shared_ptr<IEventStreamBase>{
                switch (type_hash) {
                    case class_hash<ClosedBar>:
                        if (type == ohlc) return create_ohlc_stream(std::move(source),r, std::chrono::seconds(interval));
                        break;
                    case class_hash<Trade>:
                        if (type == close) return create_close_stream(std::move(source),r);
                        break;
                    case class_hash<AuctionDailyHistory>:
                        if (type == auction) return create_auction_daily_stream(std::move(source),r);
                        break;
                    case class_hash<Quote>:
                        if (type == quote) return create_quote_stream(std::move(source),r);
                        break;
                    default: 
                }
                logWarning("Requested history data was not found or not supported (index file {}, symbol {})", index_file.string(), symbol);
                return {};
            });
        }


}