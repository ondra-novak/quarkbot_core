#pragma once

#include "defs.hpp"
#include "exchanges/bitfinex/auth_stream.hpp"
#include "exchanges/bitfinex/network_context.hpp"
#include "market_instrument.hpp"
#include "order.hpp"
#include "order_defs.hpp"
#include "order_storage.hpp"
#include "signer.hpp"
#include "ifc/account.hpp"
#include <chrono>
#include <memory>
#include <new>
#include <random>
#include <stop_token>
#include <unordered_map>
namespace quarkbot {
namespace bitfinex {

class Account: public IAccount , public std::enable_shared_from_this<Account> {
public:
    virtual std::string_view get_name() const override;
    virtual awaitable<WalletInfo> get_balance(UnderlyingCurrency currency) const override;
    virtual awaitable<WalletInfo> get_total_equity(UnderlyingCurrency currency) const override;
    virtual awaitable<bool> transfer(UnderlyingCurrency currency, PAccount to_account, Decimal amount)  override;

    using POrder = std::shared_ptr<Order::State>;

    void place_order(POrder order);
    void cancel_order(POrder order);

    static POrder extract_order_state(const Order &order);

    static PAccount create_account(std::string name, const std::string &credentials, NetworkContext ctx);

    
    Account(std::string name, NetworkContext ctx, Signer signer);

protected:
    using OrderID = std::int64_t;

    std::mutex _mx;
    std::string _name;
    Signer _signer;
    NetworkContext _context;
    std::int64_t _next_cid = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch()).count();


    struct OrderReg {
        POrder ord;
        POrder pending_replace = {};
        unsigned int pending_fills = 0;
        Order::Update final_status = OrderStatus::open;
        void add_pending_fill() {++pending_fills;}
        bool release_pending_fill() {if (pending_fills) --pending_fills; return !pending_fills;}
    };

    using ActiveOrderMap = std::unordered_map<OrderID, OrderReg >;
    using ActiveOrder = ActiveOrderMap::iterator;

    ///active orders by their UID
    ActiveOrderMap _active_orders = {};
    ///sent orders by their CID
    std::unordered_map<OrderID, POrder > _sent_orders = {};
    std::default_random_engine _rnd;
    
    


    std::uint64_t _timestamp_of_last_fill = 0;

    std::unique_ptr<AuthStream> _auth_stream;

    void start();
    void worker(Json msg);
    std::function<void(Json)> create_callback();   
    void send_command(std::unique_lock<std::mutex> &lk, const Json &cmd);
    ActiveOrder find_order(OrderID id, OrderID cid);    
    

    static OrderParameters convert_params(PMarketInstrument instrument, const OrderRequest &req);
    static RecordKey gen_record_key(OrderID id);
    static std::optional<OrderID> extract_order_id(std::string_view id);
    static Order::RejectionWithText rejection_by_message(std::string_view text);
    static Fill create_fill(const Json &data, const POrder &order);
    void finish_order(ActiveOrder iter);


    static constexpr unsigned int flag_hidden = 64;
    static constexpr unsigned int flag_close = 512;
    static constexpr unsigned int flag_reduce_only = 1024;
    static constexpr unsigned int flag_post_only = 4096;
    static constexpr unsigned int flag_oco = 16384;
    
}

;


}
}