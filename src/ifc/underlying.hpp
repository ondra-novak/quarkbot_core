#pragma once


#include "defs.hpp"
#include <optional>
#include "exchange.hpp" 
namespace quarkbot {

class IUnderlyingCurrency {
public:

    virtual ~IUnderlyingCurrency() = default;
    virtual std::string get_unique_id() const = 0;
    /**
        @return The ISO code of the currency, for example "USD" or "BTC"
        @return std::nullopt if the ISO code is not available
     */
    virtual std::optional<std::string> get_ISO_code() const = 0;
    ///Returns the symbol of the currency, for example "USD" or "BTC"
    /**
        @param other The currency to which the ratio is calculated (awaitable - co_await)
        @return The ratio of this currency to the other currency. Returns 0 if ratio is not available.
     */
    virtual awaitable<double> get_ratio_to(const PUnderlyingCurrency &other) const = 0;
    virtual PExchange get_exchange() const = 0;


    ///Determine ration between two underlying currencies on two exchanges
    /**
    @param from from currency
    @param to to currency
    @return async awaitable return ratio from * ratio = to. If ration cannot be determined,
    return zero. If currencies are from different exchanges, it uses intermediate currency for conversion
     */
    friend awaitable<double> get_ratio(const PUnderlyingCurrency &from, const PUnderlyingCurrency &to) {

        //same iso code, same rate
        if (from->get_ISO_code() == to->get_ISO_code()) return 1.0;

        //from same exchange, call diretcly
        PExchange ex_from = from->get_exchange();
        PExchange ex_to = to->get_exchange();
        if (ex_from == ex_to) return from->get_ratio_to(to);
    
        //initiate coroutine to perform intermediate conversion
        constexpr auto async_coro =  [](const PUnderlyingCurrency &from, const PUnderlyingCurrency &to)->awaitable<double> {
            auto ex_from = from->get_exchange();
            auto ex_to = to->get_exchange();
            auto lst_from = ex_from->get_all_iso_currencies();
            auto lst_to = ex_to->get_all_iso_currencies();
            for (const auto &x: lst_from) {
                for (const auto &y: lst_to) {
                    if (x->get_ISO_code() == y->get_ISO_code()) {
                        double r1 = co_await from->get_ratio_to(x);
                        double r2 = co_await y->get_ratio_to(to);
                        co_return r1*r2;
                    }
                }
            }
            co_return 0.0;
        };

        return async_coro(from, to);

        
    }

};

}