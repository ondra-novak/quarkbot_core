#include "exchange_main.h"
#include <iostream>

#include "../../common/basic_context.h"
#include "../../common/basic_log.h"
#include "../../trading_ifc/shared_state.h"

class EventTarget: public quarkbot::IEventTarget {
public:
    virtual void on_event(const quarkbot::Instrument &i,quarkbot::AsyncStatus) {}
    virtual void on_event(const quarkbot::Account &a, quarkbot::AsyncStatus) {}
    virtual void on_event(const quarkbot::Instrument &i, const quarkbot::MarketEvent &event) {
        std::cout << i.get_id() << " ";
        std::optional<quarkbot::TickData> v = event;
        if (v) std::cout << *v << std::endl;

    }
    virtual void on_event(const quarkbot::Order &order,const quarkbot::Order::Report &report) {}
    virtual void on_event(const quarkbot::Order &order, const quarkbot::Fill &fill) {}
    virtual void on_event(const quarkbot::Instrument &i, quarkbot::AsyncStatus st, quarkbot::MarketEvent ev) {}

};

int main() {

    quarkbot::TickData dt;
    quarkbot::MarketEvent ev(quarkbot::MarketEventHolder<
                quarkbot::MarketEventType::tickdata, quarkbot::TickData>::create(dt));
    std::cout << ev << std::endl;

    quarkbot::Log log(std::make_shared<quarkbot::BasicLog>(std::cout, quarkbot::Log::Serverity::trace));
    auto context = std::make_shared<quarkbot::BasicExchangeContext>("Binance",quarkbot::Network(),log);

    quarkbot::Config exchange_config ( {
            {"server", std::string("live")}
    });

    quarkbot::Config api_key ( {
            {"api_name",std::string("kvuBDXalY0f35Myi0hdf66FZc6onDUH1ytKs2amCeAKdN3kcDZUBuHZD464YoJdC")},
            {"secret",std::string("mO5pqey9uE2tIetEIvYHXwpLcYnkVf6Zmz01tnB96ALcKNl72ciqVI12AMHy2q1d")},
    });

    context->init(std::make_unique<BinanceExchange>(), exchange_config);
    context->set_api_key("master", api_key);


    quarkbot::SharedState<std::vector<quarkbot::Instrument> > state({},[](auto &res){
        for (const quarkbot::Instrument &instr: res) {
            std::cout<<"Instrument:" << instr.get_id() << std::endl;
            std::cout<<"Label:" << instr.get_label() << std::endl;
            std::cout<<"Exchange:" << instr.get_exchange().get_label() << std::endl;
            auto cfg = instr.get_config();
            std::cout<<"LotSize:" << cfg.lot_size << std::endl;
            std::cout<<"TickSize:" << cfg.tick_size << std::endl;
            std::cout << "-----" << std::endl;
        }
    });

    context->query_instruments("BTCUSDT", "bitcoin", [&,state](quarkbot::Instrument instr){
        std::lock_guard _(state);
        state->push_back(instr);
    });
    context->query_instruments("ETHUSDT", "ethereum", [&,state](quarkbot::Instrument instr){
        std::lock_guard _(state);
        state->push_back(instr);
    });

    state.wait();

    quarkbot::SharedState<std::vector<quarkbot::Account> > astate({},[](auto &res){
        for (const quarkbot::Account &acc: res) {
            std::cout<<"Account:" << acc.get_id() << std::endl;
            std::cout<<"Label:" << acc.get_label() << std::endl;
            std::cout<<"Exchange:" << acc.get_exchange().get_label() << std::endl;
            auto info = acc.get_status();
            std::cout<<"Balance:" << info.balance << std::endl;
            std::cout<<"Blocked:" << info.initial_margin<< std::endl;
            std::cout<<"Currency:" << info.currency<< std::endl;
            std::cout << "-----" << std::endl;
        }
    });

    context->query_accounts("master", "*", "main", [&, astate](quarkbot::Account acc){
        std::lock_guard _(astate);
        astate->push_back(acc);
    });

    astate.wait();


    quarkbot::Instrument bitcoin;

    {
        quarkbot::SharedState<quarkbot::Instrument *> state(&bitcoin);
        context->query_instruments("BTCUSDT", "btc", [state](const auto &i){
            **state = i;
        });
        state.wait();
    }



    EventTarget evt;

//    context->subscribe(&evt, quarkbot::SubscriptionType::ticker, bitcoin);

    std::cout << std::cin.get();

 //   context->disconnect(&evt);

    std::this_thread::sleep_for(std::chrono::seconds(1));



}
