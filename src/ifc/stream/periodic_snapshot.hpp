#pragma once
#include "quote.hpp"

namespace quarkbot {

struct PeriodicSnapshotView : public Quote{
public:    
    static constexpr Type type = "periodic_snapshot";
    Decimal last_price;
    PeriodicSnapshotView &view() {return *this;}
    using ParamType =  StreamSingleParam<unsigned int>;

    using Param = unsigned int;

};


template<unsigned int interval>
requires(interval >= 1)
struct PeriodicSnapshot : PeriodicSnapshotView{
    constexpr static Param param = interval;

};

}