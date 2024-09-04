#pragma once

#include <quarkbot/exchange.h>
#include <quarkbot/orderbook.h>

namespace quarkbot {

namespace Replay {


struct Data : TickData {
    std::string symbol_id;
};

///ReplaySource is function, which returns new replay record on each call.
/**
 * @return new TickData, null, if EOF
 */
using Source = Function<const Data *()>;


///creates replay source reading CSV file
/**
 * CSV file must have following columns
 * timestamp,symbol,bid,ask,bid_size,ask_size,trade,volume,index
 *
 * The timestamp must be in seconds (decimals allowed), relative to begin of replay
 * All numbers must use dot as decimal separator
 * CSV must use comma `,` as field separator
 * CSV should use quotes `"` for string
 *
 * @param fname name of file
 * @param initial_time start time (base timepoint, because all timestamps are relative)
 * @param speed multiplies time by this constant
 * @param offset adjusts relative time (before multiplication)
 * @return source object
 */
Source create(const std::string &fname,
        std::chrono::system_clock::time_point initial_time,
        double speed = 1.0,
        double offset = 0.0);
///creates replay source by reading CSV file from iostream
/**
 * CSV file must have following columns
 * timestamp,symbol,bid,ask,bid_size,ask_size,trade,volume,index
 *
 * The timestamp must be in seconds (decimals allowed), relative to begin of replay
 * All numbers must use dot as decimal separator
 * CSV must use comma `,` as field separator
 * CSV should use quotes `"` for string
 *
 * @param infile reference input file
 * @param initial_time start time (base timepoint, because all timestamps are relative)
 * @param speed multiplies time by this constant
 * @param offset adjusts relative time (before multiplication)
 * @return source object
 */
Source create(std::istream &infile,
        std::chrono::system_clock::time_point initial_time,
        double speed = 1.0,
        double offset = 0.0);



}

}
