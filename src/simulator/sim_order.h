#pragma once

#include <trading_api/exchange_api.h>


namespace trading_api {


class SimOrder: public BasicOrder {
public:


    using BasicOrder::BasicOrder;

    virtual SerializedOrder to_binary() const override;
    static std::pair<std::shared_ptr<SimOrder>,Order::Report> from_binary(const Account &account,
            const SerializedOrder &ord,
            Function<Instrument(std::string_view)> instrument_lookup
    );

protected:

};


}
