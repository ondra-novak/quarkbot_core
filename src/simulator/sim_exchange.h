#pragma once
#include <trading_api/exchange.h>
#include <trading_api/weak_object_map.h>
#include "sim_instrument.h"

namespace trading_api {

class SimExchange: public Exchange {
public:
    virtual ConfigSchema get_exchange_config_schema() const override;
    virtual ConfigSchema get_api_key_config_schema() const override;
    virtual void load_credentials(const Config &credential_config, std::string_view label, Function<void(ExchangeCredentials)> result) override;
    virtual void query_accounts(const ExchangeCredentials &creds,
            std::string_view label, const Query &query,
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
#if 0
    virtual void restore_orders(void *context, std::span<SerializedOrder> orders) override;
    virtual void order_apply_report(const Order &order, const Order::Report &report)  =0;
    virtual void order_apply_fill(const Order &order, const Fill &fill) override;
#endif
    void replay_accept(std::string_view symbol, const TickData &ticker);

protected:


    Timestamp _cur_sim_time;
    WeakObjectMap<SimInstrument> _instruments;

    void match_order(simulator::Matching &m, const Order &ord);

    struct ExecuteInfo;

    void execute_order(ExecuteInfo ctx, const Order::Market &setup);
    void execute_order(ExecuteInfo ctx, const Order::Limit &setup);
    void execute_order(ExecuteInfo ctx, const Order::LimitPostOnly &setup);
    void execute_order(ExecuteInfo ctx, const Order::ImmediateOrCancel &setup);
    void execute_order(ExecuteInfo ctx, const Order::Stop &setup);
    void execute_order(ExecuteInfo ctx, const Order::StopLimit &setup);
    void execute_order(ExecuteInfo ctx, const Order::TrailingStop &setup);
    void execute_order(ExecuteInfo ctx, const Order::TpSl &setup);
    void execute_order(ExecuteInfo ctx, const Order::Transfer &setup);
    void execute_order(ExecuteInfo ctx, const Order::ClosePosition &setup);
    void execute_order(ExecuteInfo ctx, const IOrder::Undefined &setup);

    bool validate_order(const Order::Setup &setup);

    void process_execution(const simulator::Matching::Execution &ex);

};

}
