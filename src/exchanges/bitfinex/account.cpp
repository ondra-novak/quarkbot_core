#include "account.hpp"
#include "defs.hpp"
#include "exchanges/bitfinex/auth_stream.hpp"
#include "exchanges/bitfinex/instrument_map.hpp"
#include "exchanges/bitfinex/signer.hpp"
#include "libs/network/string_utils.hpp"
#include "order.hpp"
#include "order_defs.hpp"
#include "tradable_instrument.hpp"
#include "types.hpp"
#include "utils/fnv1a.hpp"
#include <charconv>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <variant>

namespace quarkbot {
namespace bitfinex {


    Account::Account(std::string name, NetworkContext ctx, Signer signer)
        :_name(std::move(name))
        ,_signer(std::move(signer))
        ,_context(std::move(ctx)) {}

    PAccount Account::create_account(std::string name, const std::string &credentials, NetworkContext ctx) {
        std::string_view c(credentials);
        std::string_view apikey = network::trim(network::split(c, ":"));
        std::string_view secret = network::trim(c);
        if (apikey.empty() || secret.empty()) throw std::runtime_error("Bitfinex: invalid credentials: use <apikey>:<secret> format");
        
        auto acc = std::make_shared<Account>(
            std::move(name),ctx,Signer{std::string(apikey), std::string(secret)}
        );
        acc->start();
        return acc;
    }

    std::function<void(Json)> Account::create_callback() {
        auto me = weak_from_this();
        return [me](Json js) {
            auto lk = me.lock();
            if (lk) lk->worker(std::move(js));
        };
    }

    void Account::start() {
        auto stream = std::unique_ptr<AuthStream>();
        stream->connect(_context,_signer.get_api_key(), _signer.signChannel(),create_callback());
        _auth_stream = std::move(stream);
    }

    class OrderEx: public Order {
    public:
        Account::POrder get_state() const {
            return _state;
        }
    };

    Account::POrder Account::extract_order_state(const Order &order) {
        const OrderEx &oex = static_cast<const OrderEx &>(order);
        return oex.get_state();
    }

    std::string_view Account::get_name() const {
        return _name;
    }
    awaitable<Account::WalletInfo> Account::get_balance(UnderlyingCurrency ) const {
        return {};
    }
    awaitable<Account::WalletInfo> Account::get_total_equity(UnderlyingCurrency ) const {
        return {};
    }
    awaitable<bool> Account::transfer(UnderlyingCurrency , PAccount , Decimal )  {
        return false;
    }
  
    std::optional<Account::OrderID> Account::extract_order_id(std::string_view id) {
        if (id.empty()) return {};
        OrderID res;
        auto r = std::from_chars(id.begin(), id.end(), res);
        if (r.ec == std::errc{}) return res;
        return std::nullopt;
    }

    void Account::send_command(std::unique_lock<std::mutex> &lk, const Json &cmd) {
        while (true) {
            if (_auth_stream->send_command(cmd)) break;
            lk.unlock();
            std::this_thread::sleep_for(std::chrono::seconds(1));   //wait for reconnect
            lk.lock();
        }
    }
    void Account::cancel_order(POrder order) {
        std::unique_lock lk(_mx);
        auto id  = extract_order_id(order->id);
        if (!id) throw std::runtime_error("Bitfinex: can't cancel unconfirmed order");
        send_command(lk, {0,"oc",nullptr, {{"id",id.value()}}});
    }


    OrderParameters Account::convert_params(PMarketInstrument instrument, const OrderRequest &req) {
        const auto &info = instrument->get_info();
        int dir = static_cast<int>(req.side);
        return {
            req.side,
            req.type,
            req.quantity.get_rounded(info.lot_size_increment, sgn(req.quantity.value)),
            req.limit_price.get_rounded(InstrumentMap::calculate_tick_size(req.limit_price.value), dir),
            req.stop_price.get_rounded(InstrumentMap::calculate_tick_size(req.limit_price.value), dir),
            req.leverage,
            req.reduce_only,
            req.hedge,
            req.local_trigger,
            req.time_in_force,
            req.reason_override
        };
    }

    static constexpr std::string_view get_order_type(OrderType type, bool leveraged, bool ioc) {
        if (leveraged) {
            switch (type) {
                case OrderType::limit: return ioc?"IOC":"LIMIT";
                case OrderType::oco:
                case OrderType::limit_post_only: return "LIMIT";
                case OrderType::market: return "MARKET";
                case OrderType::stop: return "STOP";
                case OrderType::stoplimit: return "STOP LIMIT";
                default:break;
            }
        } else {
            switch (type) {
                case OrderType::limit: return ioc?"EXCHANGE IOC":"EXCHANGE LIMIT";
                case OrderType::oco:
                case OrderType::limit_post_only: return "EXCHANGE LIMIT";
                case OrderType::market: return "EXCHANGE MARKET";
                case OrderType::stop: return "EXCHANGE STOP";
                case OrderType::stoplimit: return "EXCHANGE STOP LIMIT";
                default:break;
            }
        }
        return "";
    }

    static std::string to_decimal_string(Decimal d) {
        std::string out;
        auto decimal_places = -d.exponent() + d.mantissa_digits;
        if (decimal_places < 1) {
            out =  d.to_string_fixed(0);
        } else {
            out = d.to_string_fixed(decimal_places);
            while (out.back() == '0') out.pop_back();
            if (out.back() == '.') out.pop_back();
        }
        return out;
    }

    static std::string to_decimal_string_amount(Decimal quantity, Side side) {
        if (side == Side::sell) {
            quantity = -quantity;
        }
        return to_decimal_string(quantity);
    }

    void Account::place_order(POrder order) {
        std::unique_lock lk(_mx);
        const PTradableInstrument &trad_instr = order->instrument;
        const auto &info = trad_instr->get_info();
        const auto &params = order->parameters;
        if (params.hedge || 
            (params.time_in_force != TimeInForce::gtc && params.time_in_force != TimeInForce::ioc)) {
                order->update(OrderRejectionReason::unsupported);
                return;
        }

        POrder repl = order->replaced_order.lock();

        if (params.type == OrderType::alert) {
            if (repl) {
                cancel_order(repl); 
            }
            //todo support for alerts
        } else if (repl) {
            const auto &old_params = repl->parameters;
            if (old_params.type == params.type                 
                        && old_params.side == params.side
                        && old_params.reduce_only == params.reduce_only
                        && old_params.time_in_force == params.time_in_force
                        && repl->instrument == trad_instr
                        && !repl->id.empty()) {

                if (old_params.type == OrderType::oco) {
                    order->update(Order::RejectionWithText{OrderRejectionReason::unsupported,"Replace of OCO order is not currently supported"});
                    return;
                }

                auto id = extract_order_id(repl->id);
                if (!id) {
                    order->update(Order::RejectionWithText{OrderRejectionReason::invalid_replace,"Bad order id"});
                    return;
                }

                auto act_iter = _active_orders.find(*id);
                if (act_iter == _active_orders.end() || act_iter->second.pending_replace) {
                    order->update(OrderRejectionReason::order_not_found);
                    return;                    
                }

                Json req = {
                    {"id", *id},
                };
                if (is_limit_order(params.type)) {
                    if (params.limit_price != old_params.limit_price) {
                        req.set("price", to_decimal_string(params.limit_price));
                    }
                } else if (is_stop_order(params.type)) {
                    if (params.stop_price != old_params.stop_price) {
                        req.set("price", to_decimal_string(params.stop_price));
                    }
                    if (params.limit_price != old_params.limit_price) {
                        req.set("price_aux_limit", to_decimal_string(params.limit_price));
                    }
                }
                if (params.quantity != old_params.quantity) {
                    req.set("amount",to_decimal_string_amount(params.quantity,params.side));
                }
                if (params.leverage != old_params.leverage && info.type == InstrumentType::contract) {
                    req.set("lev", static_cast<int>(params.leverage));
                }
                act_iter->second.pending_replace = order;
                send_command(lk, {0,"ou",nullptr, req});
            } else {
                order->update(OrderRejectionReason::invalid_replace);
            }


        } else {


            OrderID cid = _next_cid++;
            _sent_orders.emplace(cid, order);
            unsigned int flags = 0;

            Json req = {
                {"cid", cid},
                {"type", get_order_type(params.type, info.is_leveraged(), params.time_in_force == TimeInForce::ioc)},
                {"symbol","t"+info.name},
                {"amount", to_decimal_string_amount(params.quantity ,params.side)}
            };
            if (info.type == InstrumentType::contract && params.leverage>0) {
                req.set("lev",static_cast<int>(params.leverage));
            }



            if (is_limit_order(params.type)) {
                req.set("price", to_decimal_string(params.limit_price));                                        
            } else if (is_stop_order(params.type)) {
                req.set("price",to_decimal_string(params.stop_price));
                if (params.type == OrderType::stoplimit) {
                    req.set("price_aux_limit", to_decimal_string(params.limit_price));                        
                }
            }

            switch (params.type) {
                case OrderType::limit_post_only: flags|=flag_post_only;break;
                case OrderType::oco: flags |= flag_oco;
                                req.set("price_oco_stop",to_decimal_string(params.stop_price));
                                break;
                default: break;
            }

            if (params.reduce_only) flags |= flag_reduce_only;

            req.set("meta", Json {
                {"protect_selfmatch", 1}
            });
            req.set("flags", flags);
            send_command(lk, Json {{0,"on",nullptr,req}});
        }                
    }

    RecordKey Account::gen_record_key(OrderID id) {
        return {
            static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count()),
            static_cast<std::uint64_t>(id)
        };
    }

    Account::ActiveOrder Account::find_order(OrderID id, OrderID cid) {
        POrder ord;
        auto iter = _active_orders.find(id);
        if (iter == _active_orders.end()) {
            auto iter2 = _sent_orders.find(cid);
            if (iter2 != _sent_orders.end()) {
                ord = std::move(iter2->second);
                ord->update(Order::OpenStatus{std::to_string(id),gen_record_key(id)});
                auto ins = _active_orders.emplace(id, OrderReg{
                    std::move(ord),{},0, OrderStatus::open
                });
                assert(ins.second);
                iter = ins.first;                
            }
        }         
        return iter;
    }

    static constexpr std::string_view normalize_status_text(std::string_view text) {
        std::size_t pos = 0;
        for (auto x: text) {
            if ((x >= 'A' && x <= 'Z' ) || x == ' ' || x == '_') {
                ++pos;
            } else {
                break;
            }
        }
        return network::trim(text.substr(0,pos));
    }

    static Order::Update parse_order_status(std::string_view text) {
        text = normalize_status_text(text);
        switch(fnv1a_hash(text)) {
            case fnv1a_hash("ACTIVE"): return OrderStatus::open;
            case fnv1a_hash("FORCED EXECUTED"):
            case fnv1a_hash("EXECUTED"): return OrderStatus::filled;
            case fnv1a_hash("PARTIALLY FILLED"): return OrderStatus::open;
            case fnv1a_hash("POSTONLY CANCELED"): return OrderRejectionReason::post_only_taker;
            case fnv1a_hash("INSUFFICIENT BALANCE"):
            case fnv1a_hash("INSUFFICIENT MARGIN"):
                                    return OrderRejectionReason::insufficient_funds;
            case fnv1a_hash("FILLORKILL CANCELED"):
            case fnv1a_hash("IOC CANCELED"):
            case fnv1a_hash("CANCELED"):
                                    return OrderStatus::canceled;
            case fnv1a_hash("RNS_POS_REDUCE_INCR"):
            case fnv1a_hash("RNS_POS_REDUCE_FLIP"):
            case fnv1a_hash("RSN_POS_NOTFOUND"):
                                    return OrderRejectionReason::reduce_doesnt_reduce;
            case fnv1a_hash("RSN_DUST"):
                                    return OrderStatus::filled;
            case fnv1a_hash("RSN_PAUSE"):
                                    return OrderRejectionReason::exchange_issue;
            case fnv1a_hash("RSN_BOOK_SLIP"):
                                    return OrderRejectionReason::slippage;
            default:return OrderStatus::open;
                        
        }
    }

    static RecordKey fill_recordkey(std::chrono::system_clock::time_point mts, std::int64_t id) {
        return {
                    static_cast<std::uint64_t>(mts.time_since_epoch().count()),
                    static_cast<std::uint64_t>(id)
        };
    }

    Fill Account::create_fill(const Json &data, const POrder &ord) {

        auto mts = std::chrono::system_clock::time_point(
            std::chrono::duration_cast<std::chrono::system_clock::duration>(
                std::chrono::milliseconds(data[6].as_long())
            )
        );
        OrderID id = data[0].as_long();
        std::string strid = std::to_string(id);

        auto amount = Decimal::from_string(data[4].as_text());
        auto price = Decimal::from_string(data[5].as_text());
        Decimal fee = 0_dec;
        Decimal fee_ratio = 1_dec;
        if (!data[9].is_null()) {
             fee = Decimal::from_string(data[9].as_text());
            auto fee_cur = data[10].as_text();
            auto [ass,qut] = InstrumentMap::crack_instrument(data[1].as_text());
            if (ass == fee_cur) {
                fee_ratio = price;
            }
        }
        return {
                fill_recordkey(mts, id),
                strid,
                ord->name,
                mts,
                ord->instrument->get_info(),
                static_cast<Side>(sgn(amount)),
                ord->parameters.reason_override,
                abs(amount),
                price,
                fee,
                fee_ratio                    
            };
    }




    
    Order::RejectionWithText Account::rejection_by_message(std::string_view text) {
        OrderRejectionReason rsn;
        if (text.find("balance") != text.npos) rsn = OrderRejectionReason::insufficient_funds;
        else rsn = OrderRejectionReason::other;
        return {rsn, std::string(text)};        
    }

    void Account::worker(Json msg) {
        std::unique_lock lk(_mx);
        if (msg.is_null()) {//reconnect
            this->start();
            return;
        }

        const auto &data = msg[2];
        auto cmd = fnv1a_hash(msg[1].as_text());

        switch (cmd) {
            case fnv1a_hash("oc"):
            case fnv1a_hash("ou"): 
            case fnv1a_hash("on"): {
                OrderID cid = data[3].as_long();
                OrderID id = data[1].as_long();
                auto orditer = find_order(id, cid);
                if (orditer == _active_orders.end()) return;

                //handle replace now 
                //as there is no good way to find out when replace occured, let assume, that happened now
                //when "ou" is sent
                if (cmd == fnv1a_hash("ou") && orditer->second.pending_replace) {
                    //open new order
                    orditer->second.pending_replace->update(Order::OpenStatus{std::to_string(id), gen_record_key(id)});
                    //finish current order
                    orditer->second.ord->update(OrderStatus::replaced);
                    //activate new order
                    orditer->second.ord = std::move(orditer->second.pending_replace);                    
                }
                //parse order status
                auto upd = parse_order_status(data[13].as_text());
                //conver new status
                orditer->second.final_status = upd;
                //if event is "oc"
                if (cmd == fnv1a_hash("oc")) {
                    //finish order
                    finish_order(orditer);
                } else {
                    //otherwise just send update
                    orditer->second.ord->update(std::move(upd));
                }            
            } break;
            case fnv1a_hash("n"): {
                auto type = data[1].as_text();
                if (type == "on-req" || type == "ou-req") {
                    auto status = data[6].as_text();
                    if (status != "SUCCESS") {
                        auto text = data[7].as_text();
                        const auto &orddata = data[4];
                        auto id = orddata[0].as_long();
                        auto cid = orddata[2].as_long();
                        auto ord = _sent_orders.find(cid);
                        if (ord != _sent_orders.end()) {
                            ord->second->update(rejection_by_message(text));
                            _sent_orders.erase(ord);
                        } else {
                            auto iter = find_order(id, cid);
                            if (iter != _active_orders.end()) {
                                if (type == "on-req") {
                                    iter->second.final_status = rejection_by_message(text);
                                    finish_order(iter);                                    
                                } else if (type == "ou-req" && iter->second.pending_replace) {
                                    iter->second.pending_replace->update(rejection_by_message(text));
                                    iter->second.pending_replace.reset();
                                }
                            }
                        }
                    }
                }
            } break;

            case fnv1a_hash("te"):
            case fnv1a_hash("tu"):{ //fill 
                //NOTE: As long as bitfinex has zero fees, we can process "te" messages 
                //However this causes that fills uses generated fill id
                //because fill id is not known yet                
                OrderID orderid = data[3].as_long();
                OrderID cid = data[11].as_long();
                auto orditer = find_order(orderid, cid);
                if (orditer != _active_orders.end()) {
                    if (cmd == fnv1a_hash("te")) {
                        //pending fills creates buffer for lost fills after order is closed
                        orditer->second.add_pending_fill();
                    } else {                                      
                        orditer->second.ord->update(create_fill(data, orditer->second.ord));
                        if (orditer->second.release_pending_fill()) {
                            //check whether order is finished
                            finish_order(orditer);
                        }
                    }
                }
            }break;

        }
    
    }

    

    void Account::finish_order(ActiveOrder iter) {
        if (iter->second.pending_fills == 0 && Order::is_done_update(iter->second.final_status)) {
            iter->second.ord->update(std::move(iter->second.final_status));
            if (iter->second.pending_replace) {
                iter->second.pending_replace->update(OrderRejectionReason::order_not_found);
            }
            _active_orders.erase(iter);
        }        
    }

}}