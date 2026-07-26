
#include "quarkbot/abstract/backtest_data_source.hpp"
#include "quarkbot/backtest/replay_csv_file.hpp"
#include <chrono>
#include <string_view>
#include <variant>
#include "check.h"
#include "quarkbot/stream/auction.hpp"
#include "quarkbot/stream/orderbook.hpp"
#include "quarkbot/stream/trade.hpp"
#include "quarkbot/types.hpp"

constexpr std::string_view source_csv 
= R"csv("timestamp","symbol","event","price","quantity","side","flags"
1598918400198000,"BTCUSD","trade",65785,0.258,"BUY",
1598918400245000,"BTCUSD","quote",65780,1.257,"BID",
1598918400245000,"BTCUSD","quote",65789,2.2457,"ASK",
1598918400285000,"LTCUSD","trade",65780,0.5,"SELL",
1598918400348000,"ETHUSD","book",65789,2.2457,"ASK",clear
1598918400385000,"ETHUSD","book",65790,0.2,"ASK",
1598918400448000,"ETHUSD","book",65779,1.025,"BID",
1598918401348000,"ETHUSD","book",65778,0.3,"BID",
1598918402348000,"ETHUSD","book",65789,0,"ASK",
1598918403348000,"TSCO.L","auction",123,456,-125,O
1598918404348000,"TSCO.L","auction",123,4.56,333,C
1598918405348000,"TSCO.L","auction",222,45.6,111,CF
)csv";

void test1() {
    quarkbot::ReplayCSVDataSource replay(source_csv);
    quarkbot::BacktestEvent ev;

    CHECK(replay(ev));
    CHECK_EQUAL(std::chrono::duration_cast<std::chrono::microseconds>(ev.time.time_since_epoch()).count(), 1598918400198000);
    CHECK_EQUAL(ev.symbol, "BTCUSD");
    CHECK(std::holds_alternative<quarkbot::Trade>(ev.data));
    CHECK_EQUAL(std::get<quarkbot::Trade>(ev.data).price , 65785_dec);
    CHECK_EQUAL(std::get<quarkbot::Trade>(ev.data).size , 0.258_dec);
    CHECK(std::get<quarkbot::Trade>(ev.data).side == quarkbot::Side::buy);

    CHECK(replay(ev));
    CHECK_EQUAL(std::chrono::duration_cast<std::chrono::microseconds>(ev.time.time_since_epoch()).count(), 1598918400245000);
    CHECK_EQUAL(ev.symbol, "BTCUSD");
    CHECK(std::holds_alternative<quarkbot::Quote>(ev.data));
    CHECK_EQUAL(std::get<quarkbot::Quote>(ev.data).bid , 65780_dec);
    CHECK_EQUAL(std::get<quarkbot::Quote>(ev.data).bid_size , 1.257_dec);
    CHECK_EQUAL(std::get<quarkbot::Quote>(ev.data).ask , 0);
    CHECK_EQUAL(std::get<quarkbot::Quote>(ev.data).ask_size , 0);
    
    CHECK(replay(ev));
    CHECK_EQUAL(std::chrono::duration_cast<std::chrono::microseconds>(ev.time.time_since_epoch()).count(), 1598918400245000);
    CHECK_EQUAL(ev.symbol, "BTCUSD");
    CHECK(std::holds_alternative<quarkbot::Quote>(ev.data));
    CHECK_EQUAL(std::get<quarkbot::Quote>(ev.data).bid , 65780_dec);
    CHECK_EQUAL(std::get<quarkbot::Quote>(ev.data).bid_size , 1.257_dec);
    CHECK_EQUAL(std::get<quarkbot::Quote>(ev.data).ask , 65789);
    CHECK_EQUAL(std::get<quarkbot::Quote>(ev.data).ask_size , 2.2457_dec);

    CHECK(replay(ev));
    CHECK_EQUAL(std::chrono::duration_cast<std::chrono::microseconds>(ev.time.time_since_epoch()).count(), 1598918400285000);
    CHECK_EQUAL(ev.symbol, "LTCUSD");
    CHECK(std::holds_alternative<quarkbot::Trade>(ev.data));
    CHECK_EQUAL(std::get<quarkbot::Trade>(ev.data).price , 65780_dec);
    CHECK_EQUAL(std::get<quarkbot::Trade>(ev.data).size , 0.5_dec);
    CHECK(std::get<quarkbot::Trade>(ev.data).side == quarkbot::Side::sell);

    CHECK(replay(ev));
    CHECK_EQUAL(std::chrono::duration_cast<std::chrono::microseconds>(ev.time.time_since_epoch()).count(), 1598918400348000);
    CHECK_EQUAL(ev.symbol, "ETHUSD");
    CHECK(std::holds_alternative<quarkbot::OrderBookSnapshot>(ev.data));
    CHECK_EQUAL(std::get<quarkbot::OrderBookSnapshot>(ev.data).asks.size() , 1);
    CHECK_EQUAL(std::get<quarkbot::OrderBookSnapshot>(ev.data).bids.size(), 0);
    CHECK_EQUAL(std::get<quarkbot::OrderBookSnapshot>(ev.data).asks[0].price , 65789_dec);
    CHECK_EQUAL(std::get<quarkbot::OrderBookSnapshot>(ev.data).asks[0].quantity , 2.2457_dec);

    CHECK(replay(ev));
    CHECK_EQUAL(std::chrono::duration_cast<std::chrono::microseconds>(ev.time.time_since_epoch()).count(), 1598918400385000);
    CHECK_EQUAL(ev.symbol, "ETHUSD");
    CHECK(std::holds_alternative<quarkbot::OrderBookIncrement>(ev.data));
    CHECK_EQUAL(std::get<quarkbot::OrderBookIncrement>(ev.data).price , 65790_dec);
    CHECK_EQUAL(std::get<quarkbot::OrderBookIncrement>(ev.data).quantity, 0.2_dec);
    CHECK(std::get<quarkbot::OrderBookIncrement>(ev.data).side == quarkbot::Side::sell);

    CHECK(replay(ev));
    CHECK_EQUAL(std::chrono::duration_cast<std::chrono::microseconds>(ev.time.time_since_epoch()).count(), 1598918400448000);
    CHECK_EQUAL(ev.symbol, "ETHUSD");
    CHECK(std::holds_alternative<quarkbot::OrderBookIncrement>(ev.data));
    CHECK_EQUAL(std::get<quarkbot::OrderBookIncrement>(ev.data).price , 65779_dec);
    CHECK_EQUAL(std::get<quarkbot::OrderBookIncrement>(ev.data).quantity, 1.025_dec);
    CHECK(std::get<quarkbot::OrderBookIncrement>(ev.data).side == quarkbot::Side::buy);

    CHECK(replay(ev));
    CHECK_EQUAL(std::chrono::duration_cast<std::chrono::microseconds>(ev.time.time_since_epoch()).count(), 1598918401348000);
    CHECK_EQUAL(ev.symbol, "ETHUSD");
    CHECK(std::holds_alternative<quarkbot::OrderBookIncrement>(ev.data));
    CHECK_EQUAL(std::get<quarkbot::OrderBookIncrement>(ev.data).price , 65778_dec);
    CHECK_EQUAL(std::get<quarkbot::OrderBookIncrement>(ev.data).quantity, 0.3_dec);
    CHECK(std::get<quarkbot::OrderBookIncrement>(ev.data).side == quarkbot::Side::buy);

    CHECK(replay(ev));
    CHECK_EQUAL(std::chrono::duration_cast<std::chrono::microseconds>(ev.time.time_since_epoch()).count(), 1598918402348000);
    CHECK_EQUAL(ev.symbol, "ETHUSD");
    CHECK(std::holds_alternative<quarkbot::OrderBookIncrement>(ev.data));
    CHECK_EQUAL(std::get<quarkbot::OrderBookIncrement>(ev.data).price , 65789_dec);
    CHECK_EQUAL(std::get<quarkbot::OrderBookIncrement>(ev.data).quantity, 0_dec);
    CHECK(std::get<quarkbot::OrderBookIncrement>(ev.data).side == quarkbot::Side::sell);
 
    CHECK(replay(ev));
    CHECK_EQUAL(std::chrono::duration_cast<std::chrono::microseconds>(ev.time.time_since_epoch()).count(), 1598918403348000);
    CHECK_EQUAL(ev.symbol, "TSCO.L");
    CHECK(std::holds_alternative<quarkbot::Auction>(ev.data));
    CHECK_EQUAL(std::get<quarkbot::Auction>(ev.data).price , 123_dec);
    CHECK_EQUAL(std::get<quarkbot::Auction>(ev.data).quantity, 456_dec);
    CHECK_EQUAL(std::get<quarkbot::Auction>(ev.data).imbalance, -125_dec);
    CHECK(std::get<quarkbot::Auction>(ev.data).auction_type == quarkbot::AuctionType::opening);
 
    CHECK(replay(ev));
    CHECK_EQUAL(std::chrono::duration_cast<std::chrono::microseconds>(ev.time.time_since_epoch()).count(), 1598918404348000);
    CHECK_EQUAL(ev.symbol, "TSCO.L");
    CHECK(std::holds_alternative<quarkbot::Auction>(ev.data));
    CHECK_EQUAL(std::get<quarkbot::Auction>(ev.data).price , 123_dec);
    CHECK_EQUAL(std::get<quarkbot::Auction>(ev.data).quantity, 4.56_dec);
    CHECK_EQUAL(std::get<quarkbot::Auction>(ev.data).imbalance, 333_dec);
    CHECK(!std::get<quarkbot::Auction>(ev.data).final);
    CHECK(std::get<quarkbot::Auction>(ev.data).auction_type == quarkbot::AuctionType::closing);
 
    CHECK(replay(ev));
    CHECK_EQUAL(std::chrono::duration_cast<std::chrono::microseconds>(ev.time.time_since_epoch()).count(), 1598918405348000);
    CHECK_EQUAL(ev.symbol, "TSCO.L");
    CHECK(std::holds_alternative<quarkbot::Auction>(ev.data));
    CHECK_EQUAL(std::get<quarkbot::Auction>(ev.data).price , 222_dec);
    CHECK_EQUAL(std::get<quarkbot::Auction>(ev.data).quantity, 45.6_dec);
    CHECK_EQUAL(std::get<quarkbot::Auction>(ev.data).imbalance, 111_dec);
    CHECK(std::get<quarkbot::Auction>(ev.data).auction_type == quarkbot::AuctionType::closing);
    CHECK(std::get<quarkbot::Auction>(ev.data).final);

}


int main() {
    test1();

}