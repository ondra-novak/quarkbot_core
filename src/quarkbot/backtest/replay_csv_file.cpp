

#include "replay_csv_file.hpp"
#include "quarkbot/decimal.hpp"
#include "quarkbot/stream/auction.hpp"
#include "quarkbot/stream/orderbook.hpp"
#include "quarkbot/utils/string_utils.hpp"
#include <chrono>
#include <exception>
#include <stdexcept>
#include <utility>
#include <zlib.h>
namespace quarkbot {


CSVFieldIndexMapping<ReplayCSVDataSource::Data> ReplayCSVDataSource::map_columns(CSVReader<CSVSource> &csv) {
return csv.mapColumns<ReplayCSVDataSource::Data>({
        {"timestamp",&ReplayCSVDataSource::Data::timestamp},
        {"symbol",&ReplayCSVDataSource::Data::symbol},
        {"event",&ReplayCSVDataSource::Data::event},
        {"price",&ReplayCSVDataSource::Data::price},
        {"quantity",&ReplayCSVDataSource::Data::quantity},
        {"side",&ReplayCSVDataSource::Data::side},
        {"flags",&ReplayCSVDataSource::Data::flags}
    });
}

ReplayCSVDataSource::ReplayCSVDataSource(std::filesystem::path file) 
    :csv(init_source(file))
    ,colmap(map_columns(csv))
    ,fname(file)
    ,line_num(0)
    {
        if (!colmap.allMapped) throw std::runtime_error(std::format("Invalid format for file: {}", file.string()));
    }

ReplayCSVDataSource::ReplayCSVDataSource(std::string_view content)
    :csv({[content]() mutable {
        return std::exchange(content, {});
    }})
    ,colmap(map_columns(csv))
    ,fname("inline")
    ,line_num(0)
  {
        if (!colmap.allMapped) throw std::runtime_error("Invalid format");
  }



ReplayCSVDataSource::CSVSource ReplayCSVDataSource::init_source(std::filesystem::path source_file) {
#ifdef _WIN32
    auto gzf = gzopen_w(source_file.c_str(), "r");
#else
    auto gzf = gzopen(source_file.c_str(), "r");
#endif 
    if (gzf == nullptr) throw std::runtime_error(std::format("Failed to open gz file: {}", source_file.string()));
    auto shared_gzf = std::shared_ptr<struct gzFile_s>(gzf, [](gzFile f){gzclose(f);});
    return CSVSource{
        [shared_gzf, buff = std::array<char, 65536>()]() mutable -> std::string_view {
            int r = gzread(shared_gzf.get(), buff.data(), static_cast<unsigned int>(buff.size()));
            if (r > 0) return {buff.data(), static_cast<std::size_t>(r)};
            if (r == 0 && gzeof(shared_gzf.get())) return {};
            int errnum;
            const char *err = gzerror(shared_gzf.get(), &errnum);
            throw std::runtime_error(std::format("GZ error: {} - {}", errnum, err));
        },
    };
}

bool ReplayCSVDataSource::operator()(BacktestEvent &ev) {
    try {
        if (!csv.readRow(colmap, tmp)) return false;
        ++line_num;
        ev.time = std::chrono::system_clock::time_point(std::chrono::microseconds(tmp.timestamp));
        ev.symbol =  tmp.symbol;
        if (compare_icase(tmp.event, "quote") == 0) {
            cur_quote.time = ev.time;
            if (compare_icase(tmp.side ,"ASK") == 0) {
                cur_quote.ask = Decimal::from_string(tmp.price);
                cur_quote.ask_size = Decimal::from_string(tmp.quantity);
            } else if (compare_icase(tmp.side ,"BID") == 0) {
                cur_quote.bid = Decimal::from_string(tmp.price);
                cur_quote.bid_size = Decimal::from_string(tmp.quantity);
            } else {
                throw std::runtime_error("Expected BID or ASK");
            }
            ev.data = cur_quote;
            return true;
        } else if (compare_icase(tmp.event, "trade") == 0) {
            Trade tr;
            tr.price = Decimal::from_string(tmp.price);
            tr.size = Decimal::from_string(tmp.quantity);
            if (compare_icase(tmp.side,"BUY") ==0) tr.side = Side::buy;
            else if (compare_icase(tmp.side,"SELL") ==0) tr.side = Side::sell;
            else tr.side = Side::undetermined;
            tr.time = ev.time;
            ev.data = tr;
            return true;
        } else if (compare_icase(tmp.event, "auction") == 0) {
            Auction a;
            a.price = Decimal::from_string(tmp.price);
            a.quantity = Decimal::from_string(tmp.quantity);
            a.final = false;
            a.quantity_traded = 0;
            if (compare_icase(tmp.side,"BUY") ==0) a.imbalance=1;
            else if (compare_icase(tmp.side,"SELL") ==0) a.imbalance=-1;
            else if (!tmp.side.empty()) a.imbalance = Decimal::from_string(tmp.side);
            a.time = ev.time;
            a.auction_type = AuctionType::unknown;
            for (auto c: tmp.flags) {
                switch (c) {
                    case 'o':
                    case 'O': a.auction_type = AuctionType::opening;break;
                    case 'c':
                    case 'C': a.auction_type = AuctionType::closing;break;
                    case 'i':
                    case 'I': a.auction_type = AuctionType::intraday;break;
                    case 'u':
                    case 'U': a.auction_type = AuctionType::unscheduled;break;
                    case 'f':
                    case 'F': a.final = true;a.quantity_traded = a.quantity;break;
                    default:break;
                }
            }
            ev.data = a;
            return true;
        } else if (compare_icase(tmp.event, "book") == 0) {
            if (compare_icase(tmp.flags, "clear")==0) {
                one_snapshot_level.price = Decimal::from_string(tmp.price);
                one_snapshot_level.quantity = Decimal::from_string(tmp.quantity);
                OrderBookSnapshot res;
                if (compare_icase(tmp.side,"BID") ==0) res.bids = {&one_snapshot_level, 1};
                else if (compare_icase(tmp.side,"ASK") ==0) res.asks = {&one_snapshot_level, 1};
                else throw std::runtime_error("Expected BID or ASK");
            
                res.time = ev.time;
                ev.data = res;
                return true;                
            } else {
                OrderBookIncrement inc;
                if (compare_icase(tmp.side,"BID") ==0) inc.side = Side::buy;
                else if (compare_icase(tmp.side,"ASK") ==0) inc.side = Side::sell;
                else throw std::runtime_error("Expected BID or ASK");
                inc.time = ev.time;
                inc.price = Decimal::from_string(tmp.price);
                inc.quantity = Decimal::from_string(tmp.quantity);
                ev.data = inc;
                return true;
            }
        } else {
            throw std::runtime_error(std::format("Unknown event {}", tmp.event));
        }
    } catch (const std::exception &e) {
        throw std::runtime_error(std::format("Parse exception: {} at {}:{}", e.what(), fname.string(), line_num));
    }
}


int ReplayCSVDataSource::CSVSource::operator()() {
    if (cur_line.empty()) cur_line = block_reader();
    if (cur_line.empty()) return -1;
    unsigned char c = static_cast<unsigned char>(cur_line.front());
    cur_line.remove_prefix(1);
    return static_cast<int>(c);
}

}