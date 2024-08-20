#pragma once

#include <trading_api/exchange.h>


namespace trading_api {


class SimOrder: public BasicOrder {
public:


    using BasicOrder::BasicOrder;

    virtual SerializedOrder to_binary() const override;
    static SimOrder from_binary(const Account &account,
                                const Account &instrument,
                                const SerializedOrder &ord);

protected:

};


}
