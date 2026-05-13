#pragma once

#include "utils/decimal.hpp"
#include <string>
namespace quarkbot {
namespace bitfinex {

    ///interface helps to report price
    /**
        used between public streams and exchange as callback from stream to exchange and it is referneced as weak ref
        allowing to stop reporting if exchange goes away and streams are going on.
    */
    class IPriceReport {
    public:
        virtual void report_price(const std::string &id, Decimal price)= 0;
        virtual ~IPriceReport() = default;
    };


}
}