#pragma once

#include <quarkbot/exchange_api.h>
#include <quarkbot/orderbook.h>

namespace quarkbot {

namespace Replay {


struct Data : TickData {
    std::string symbol_id;
};



///ReplaySource is function, which returns new replay record on each call.
/**
 * @return new TickData, empty optional in case, that EOF reached
 */
using Source = Function<std::optional<Data>()>;


///creates replay source reading CSV file
/**
 * CSV file must have following columns
 * timestamp,symbol,bid,ask,bid_size,ask_size,trade,volume,index
 *
 * The timestamp must have format YYYY-MM-DD HH:MM:SS.sss
 * All numbers must use dot as decimal separator
 * CSV must use comma `,` as field separator
 * CSV should use quotes `"` for string
 *
 * @param fname name of file
 * @return source object
 */
Source create(const std::string &fname);
///creates replay source by reading CSV file from iostream
/**
 * CSV file must have following columns
 * timestamp,symbol,bid,ask,bid_size,ask_size,trade,volume,index
 *
 * The timestamp must have format YYYY-MM-DD HH:MM:SS.sss
 * All numbers must use dot as decimal separator
 * CSV must use comma `,` as field separator
 * CSV should use quotes `"` for string
 *
 * @param infile reference input file
 * @return source object
 */
Source create(std::istream &infile);

///Aggregates multiple sources into one
/**
 * Function uses timestamp to order events
 * @param sources sources - object should be created or moved
 * @return source object
 */
Source aggregate(std::vector<Source> sources);


}

}
