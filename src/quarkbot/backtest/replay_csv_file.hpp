#pragma once

#include "quarkbot/stream/orderbook.hpp"
#include <quarkbot/abstract/backtest_data_source.hpp>
#include <quarkbot/utils/csv_reader.h>
#include <filesystem>
namespace quarkbot {


/**

CSV fields

timestamp - string - microseconds timestamp (increasing, can duplicate)
symbol - string

event - quote, trade, auction, book
price - quote: bid or ask  price
        trade: trade price
        book: level price
        auction: indicative price
quantity - quote: bid or ask quantity
           trade: trade quantity
           book: quantity on level
           auction: inticative size
side -  quote: ASK or BID
        trade: BUY or SELL, or empty
        book: ASK or BID
        auction: inbalance side
flags:  quote - empty
        trade - empty
        book - clear or empty (clear = initiate snapshot)
        auction - {O,C,I,U}, F- final (OF = opening final)


*/

class ReplayCSVDataSource {
public:

    ///open inline content (plain text)
    ReplayCSVDataSource(std::string_view content);  
    ///open gzip file
    ReplayCSVDataSource(std::filesystem::path file);
    ///Retrieve next event
    /**
        @param ev reference to variable filled with event
        @retval true success
        @retval false - eof reached
    */
    bool operator()(BacktestEvent &ev);
protected:

    struct CSVSource {
        std::move_only_function<std::string_view()> block_reader;
        std::string_view cur_line = {};
        int operator()();     
       
    };

    struct Data {
        std::int64_t timestamp;
        std::string symbol;
        std::string event;
        std::string price;
        std::string quantity;
        std::string side;
        std::string flags;
    };

    CSVReader<CSVSource> csv;
    CSVFieldIndexMapping<Data> colmap;
    std::filesystem::path fname;
    std::size_t line_num;
    Data tmp;
    Quote cur_quote;    
    OrderBookLevel one_snapshot_level;

    static CSVSource init_source(std::filesystem::path source_file);
    static CSVSource init_source(std::string_view source_file);
    static CSVFieldIndexMapping<Data> map_columns(CSVReader<CSVSource> &csv);
    
    

};





}