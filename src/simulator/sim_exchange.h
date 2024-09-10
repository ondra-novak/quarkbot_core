#pragma once
#include <quarkbot/exchange.h>
#include <quarkbot/weak_object_map.h>
#include <quarkbot/shared/saved_span.h>
#include "sim_instrument.h"

namespace quarkbot {

class SimExchange: public Exchange {
public:


    virtual ConfigSchema get_exchange_config_schema() const override;
    virtual ConfigSchema get_api_key_config_schema() const override;
    virtual void load_credentials(const Config &credential_config, std::string_view label, Function<void(ExchangeCredentials)> result) override;
    virtual void query_accounts(const ExchangeCredentials &creds,
            const Query &query,std::string_view label,
            Function<void(std::span<Account>)> result) override;

    virtual void query_instruments(const ExchangeCredentials &creds,
            const Query &query, std::string_view label,
            Function<void(std::span<Instrument>)> result) override;

    virtual void query_instruments(const Query &query,
            std::string_view label,
            Function<void(std::span<Instrument>)> result) override;

    virtual void subscribe(MarketEventType type, const Instrument &i) override;
    virtual void unsubscribe(MarketEventType type, const Instrument &i) override;
    virtual void update_account(const Account &a) override;
    virtual void update_instrument(const Instrument &i) override;
    virtual void update_market(const Instrument &i, MarketEventType type) override;
    virtual void batch_place(std::span<Order> orders) override;
    virtual void batch_cancel(std::span<Order> orders) override;
    virtual std::string get_name() const override;
    virtual std::string get_id() const  override;
    virtual std::optional<IExchangeInfo::Icon> get_icon() const override;
    virtual Order create_order(const Instrument &instrument, const Account &account, const Order::Setup &setup, std::string_view label) override;
    virtual Order create_order_replace(const Order &replace, const Order::Setup &setup, std::string_view label) override;
    virtual void restore_orders(const Account &acc,
            std::span<SerializedOrder> orders,
            RestoreOrdersCallback callback) override ;

    void replay_accept(std::string_view symbol, TickData &&ticker, Timestamp recvtime);

protected:


    Timestamp _cur_sim_time;
    WeakObjectMap<SimInstrument> _instruments;
    unsigned int _replay_count = 0;
    std::chrono::nanoseconds _sim_latency = {};


    void match_order(simulator::Matching &m, const Order &ord);

    struct OrderExecutor;

    virtual void on_start() override;

    bool validate_order(const Order::Setup &setup);

    void process_execution(simulator::Matching &m, simulator::Matching::Execution ex);

    void simulate_market(simulator::Matching &m);


    void start_replay(Config replay_def, Timestamp start_time);

    template<typename R>
    void run_replay(R replay);

    void batch_place_2(std::span<Order> orders);
    void batch_cancel_2(std::span<Order> orders);

};

}
