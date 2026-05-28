#pragma once
#include "quote.hpp"

namespace quarkbot {

struct PeriodicSnapshotView : public Quote{
public:    
    static constexpr Type type = "periodic_snapshot";
    Decimal last_price;
    PeriodicSnapshotView &view() {return *this;}
    using ParamType =  StreamSingleParam<unsigned int>;
};


template<unsigned interval>
requires(interval >= 1)
struct PeriodicSnapshot : PeriodicSnapshotView{
    constexpr static auto params =ParamType{{},interval};
};

}