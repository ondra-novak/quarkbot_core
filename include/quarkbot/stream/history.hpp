#pragma once

#include "../decimal.hpp"
#include "closedbar.hpp"
#include <chrono>
#include <string_view>
namespace quarkbot {

struct HistoryDataRequest {

    using Interval = std::size_t;
    static constexpr Interval interval_undefined = 1;

    static constexpr Interval interval_second = 1;
    ///predefined constant for minute interval
    static constexpr Interval interval_minute = 60;
    ///predefined constant for hour interval
    static constexpr Interval interval_hour = interval_minute*60;
    ///predefined constant for day interval
    static constexpr Interval interval_day = interval_hour*24;
    ///predefined constant for week interval
    static constexpr Interval interval_week = interval_day*7;
    ///predefined constant for month interval - regardless on how many days the actual month has.
    /** for two months, you just multiply this interval by a number count of months */
    static constexpr Interval interval_month = interval_day*30;
    ///predefined constant for year interval - regardless on how many days the actual year has
    static constexpr Interval interval_year = interval_day*365;


    ///start day - this is mandatory - specifies start day at 00:00:00 can be in UTC or local zone depends on instrument
    std::chrono::year_month_day start_date;
    ///end day - this is mandatory - specifies end day at 23:59:59 can be in UTC or local zone depends on instrument
    std::chrono::year_month_day end_date;   
    ///interval in seconds - check whether source supports this interval
    Interval interval = interval_undefined;
    ///source specification = depends on adapter, optional
    std::string_view source = {};
    ///specify true, if you need adjusted data - can be ignored by the adapter if such type of data are not available for the request
    bool adjusted = true;
    ///start time - offset from midnight of start day (optional)
    std::chrono::hh_mm_ss<std::chrono::seconds> start_time = {};
    

};

template<typename T>
concept MarketInstrumentHistoryStream = requires {
    typename T::MarketInstrumentHistoryStream;
};

template<typename T>
concept MarketInstrumentStreamOrHistory = (MarketInstrumentStream<T> || MarketInstrumentHistoryStream<T>);



///daily auction history
struct AuctionDailyHistory {
    struct MarketInstrumentHistoryStream {};
    ///open price - if zero, auction did not firm
    Decimal open_price = {};
    ///open quantity
    Decimal open_quantity = {};
    ///close price - if zero, auction did not firm
    Decimal close_price = {};
    ///close quantity
    Decimal close_quantity = {};
    ///time point should refer to the auction day (00:00 UTC) - in local
    std::chrono::year_month_day day;
};






}