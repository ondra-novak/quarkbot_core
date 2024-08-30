#include <trading_api/exchange.h>
#include <trading_api/module.h>


using namespace trading_api;

class ExampleExchange: public Exchange {
public:
    virtual ConfigSchema get_exchange_config_schema() const
            override {return {};}
    virtual Order create_order(
            const Instrument &,
            const Account &,
            const Order::Setup &, std::string_view ) override {return {};}
    virtual void unsubscribe(MarketEventType ,
            const Instrument &) override {}
    virtual void update_account(const Account &) override {}
    virtual Order create_order_replace(
            const Order &,
            const Order::Setup &, std::string_view  ) override {return {};}
    virtual std::string get_id() const override {return {};}
    virtual std::optional<IExchangeInfo::Icon> get_icon() const override {return {};}
    virtual void update_instrument(const Instrument &) override {}
    virtual void order_apply_report(const Order &, const Order::Report &) override {}
    virtual std::string get_name() const override {return {};}
    virtual void subscribe(MarketEventType,const Instrument &) override {};
    virtual void batch_place(std::span<Order> ) override {};
    virtual void batch_cancel(std::span<Order> ) override {};
    virtual void restore_orders(const Account &, std::span<SerializedOrder> , RestoreOrdersCallback cb) override {cb({});}


    virtual trading_api::ConfigSchema get_api_key_config_schema() const
            override {return {};}
    virtual void update_market(const Instrument &, MarketEventType ) override{}

    virtual void query_accounts(const ExchangeCredentials &,std::string_view , const Query &,Function<void(std::span<Account>)> ) override {}
    virtual void query_instruments(const ExchangeCredentials &,const Query &, std::string_view ,Function<void(std::span<Instrument>)> ) override {}
    virtual void query_instruments(const Query &,std::string_view ,Function<void(std::span<Instrument>)> ) override {}
    virtual void load_credentials(const Config &, std::string_view , Function<void(ExchangeCredentials)> ) override {}
};


EXPORT_EXCHANGE(ExampleExchange);

