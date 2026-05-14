#include "stream_helpers.hpp"
#include "market_instrument.hpp"
#include <chrono>

namespace quarkbot {
namespace bitfinex{

    void ClosedBarCalc::operator()(LockFreePublisher<ClosedBar, 1> &q, const Trade &in, unsigned int param) const noexcept {
    bool nw = false;
    auto interval_index = ClosedBar::to_interval_index(in.time, param);
    q.write([&](ClosedBar &cb) noexcept -> bool{
        //if we are in different interval index
        if (cb.interval_index != interval_index) {
            //mark create new interval
            nw = true;
            //publish this one (only if interval index is filled)
            return cb.interval_index>0;
        }
        //merge values to current interval
        cb.volume+=in.size;
        cb.close = in.price;
        cb.high = std::max(cb.high, in.price);
        cb.low = std::min(cb.low, in.price);
        //do not publish yet
        return false;
    });
    //if new requested
    if (nw) {
        q.write([&](ClosedBar &cb) noexcept -> bool{
            //prepare structure
            cb.interval_index = interval_index;
            cb.open = cb.close = cb.high = cb.low = in.price;
            cb.volume = in.size;
            //do not publish yet
            return false;            
        });
    }
    
    }

    void RangeBarCalc::operator()(LockFreePublisher<RangeBarView, 1> &q, const Trade &in, Decimal range) const noexcept {
        std::optional<Decimal> new_open;
        q.write([&](RangeBarView &rb) noexcept -> bool{
            rb.close_tp = in.time;
            Decimal above = rb.low+range;
            
            if (in.price > above ) { // price is above 
                if (in.price > above+range)  {
                    above = in.price - range;
                    rb.gap = true;
                }
                rb.close = above;
                new_open = above;
                rb.high = std::max(rb.high, above);
                return rb.open_tp > std::chrono::system_clock::time_point{};
            } 
            Decimal bellow = rb.high-range; 
            if (in.price < bellow ) {
                if (in.price < bellow-range) {
                    bellow = in.price - range;
                    rb.gap = true;
                }
                rb.close = bellow;
                new_open = bellow;
                rb.low = std::min(rb.low, bellow);
                return rb.open_tp > std::chrono::system_clock::time_point{};
            } 

            rb.high = std::max(rb.high, in.price);
            rb.low = std::min(rb.low, in.price);
            rb.close = in.price;
            rb.volume += in.size;
            return false;
        });
        if (new_open.has_value()) {
            q.write([&](RangeBarView &rb) noexcept -> bool{
                rb.gap = false;
                rb.open_tp = in.time;
                rb.open = new_open.value();
                rb.high = std::max(rb.open, in.price);
                rb.low = std::min(rb.open, in.price);
                rb.close = in.price;
                rb.volume = in.size;
                return false;
            });
        }    
    }

}
}