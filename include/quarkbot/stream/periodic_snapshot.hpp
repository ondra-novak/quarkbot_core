#pragma once
#include "quote.hpp"

namespace quarkbot {

struct PeriodicSnapshotView : public Quote{
public:    
    struct MarketInstrumentStream {};
    Decimal last_price;
    PeriodicSnapshotView &view() {return *this;}
   
    using Param = unsigned int;

};


template<unsigned int interval>
requires(interval >= 1)
struct PeriodicSnapshot : PeriodicSnapshotView{
    
    constexpr static Param param = interval;

};

}