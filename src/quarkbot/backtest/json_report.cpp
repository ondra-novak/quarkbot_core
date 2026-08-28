#include "json_report.hpp"
#include "quarkbot/backtest/simexchange.hpp"
#include "quarkbot/backtest/simexecutor.hpp"
#include "quarkbot/common/deserialize_resolver.hpp"
#include "quarkbot/common/mem_storage.hpp"
#include "quarkbot/common/order_internal_defs.hpp"
#include "quarkbot/common/storage_common.hpp"
#include "quarkbot/execution_worker.hpp"
#include "quarkbot/json/json.hpp"
#include "quarkbot/order_defs.hpp"
#include "quarkbot/selector.hpp"
#include "quarkbot/serializer/deserialize_from_schema.hpp"
#include "quarkbot/serializer/schema_fwd.hpp"
#include "quarkbot/serializer/serialize.hpp"
#include "quarkbot/storage_srl.hpp"
#include "quarkbot/stream/auction.hpp"
#include "quarkbot/stream/closedbar.hpp"
#include "quarkbot/market_instrument.hpp"
#include "quarkbot/event_stream.hpp"
#include "quarkbot/types.hpp"
#include "simexec_report_csv.hpp"
#include <chrono>
#include <cmath>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <variant>

namespace quarkbot {

    std::shared_ptr<std::ostream> JsonReport::open_file(std::filesystem::path filename) {
        auto f = std::make_shared<std::ofstream>(filename, std::ios::out|std::ios::trunc);
        if (!(*f)) throw std::runtime_error(std::format("Failed to open file: {}", filename.string()));
        return f;
    }


    JsonReport::JsonReport(std::ostream &outp):_outp(outp) {}



    StrategyFragment JsonReport::run_stream(EventStream<ClosedBar> stream, std::string name) {
        ClosedBar cb;
        while (co_await stream.receive(cb)) {
            out(Event::chart, {
                name, JsonNumber(cb.open.to_string()),
                          JsonNumber(cb.high.to_string()), 
                          JsonNumber(cb.low.to_string()),
                          JsonNumber(cb.close.to_string()),
                          JsonNumber(cb.volume.to_string()),
                          std::chrono::system_clock::to_time_t(cb.start_time)
                          
            });
        }
    }

    ReportSink JsonReport::create_report_sink() {
        return [this](const Order &ord, const OrderStatusUpdate &rpt) {
            auto instr = ord.get_instrument();
            const auto &info = instr.get_info();
            auto params = ord.get_parameters();
            Json order = {
                {"instrument",info.name},
                {"order_id",ord.get_id()},
                {"type",to_string(params.type)},
                {"side",string_lookup<Side>(params.side).value_or("")},
                {"quantity", JsonNumber(params.quantity.to_string())}                
            };
            if (is_limit_order(params.type)) {
                order.set("limit_price", JsonNumber(params.limit_price.to_string()));
            }
            if (is_stop_order(params.type)) {
                order.set("stop_price", JsonNumber(params.stop_price.to_string()));
            }
            auto repl = ord.get_replaced_order();
            if (repl.has_value()) {
                order.set("replaced_order_id", repl->get_id());
            }
            selector(rpt,
                [&](const Fill &f) {
                    order.set({
                        {"id",f.id},
                        {"label",f.label},
                        {"side",string_lookup<Side>(f.side).value_or("")},
                        {"price", JsonNumber(f.price.to_string())},
                        {"quantity", JsonNumber(f.quantity.to_string())},
                        {"reason", string_lookup<ExecutionReason>(f.reason).value_or("")}
                    });
                    out(Event::fill, order);
                },
                [&](OrderStatus st) {
                    order.set("status", string_lookup<OrderStatus>(st).value_or(""));
                    out(Event::order_status, order);
                },
                [&](OrderRejectionReason st) {
                    order.set("status", string_lookup<OrderStatus>(rejection_reason_2_status(st)).value_or(""));
                    order.set("rejected_reason", string_lookup<OrderRejectionReason>(st).value_or(""));
                    out(Event::order_status, order);
                },
                [&](OrderRejectionWithText st) {
                    order.set("status", string_lookup<OrderStatus>(rejection_reason_2_status(st.reason)).value_or(""));
                    order.set("rejected_reason", string_lookup<OrderRejectionReason>(st.reason).value_or(""));
                    order.set("reject_text", st.text);
                    out(Event::order_status, order);
                },
                [&](OrderOpenStatus st) {
                    order.set("status", string_lookup<OrderStatus>(OrderStatus::open).value_or(""));
                    order.set("order_id", st.id);
                    out(Event::order_status, order);
                },       
                [&](OrderFillStats st){
                    order.set({
                        {"filled",JsonNumber(st.filled.to_string())},
                        {"turnover",JsonNumber(st.turnover.to_string())},
                        {"fees",JsonNumber(st.fees.to_string())},
                        {"fees_native",JsonNumber(st.fees_native.to_string())},
                    });
                    out(Event::fill_stats, order);
                }      
            
            );
        };
    }

    void JsonReport::attach_exchange(Exchange exchange, std::stop_token token, std::size_t interval) {
        for (auto instr: exchange.get_market_instruments()) {
            auto info = instr.get_info();
            out(Event::instrument_info, {
                {"name", info.name},
                {"leverage", JsonNumber(info.leverage.to_string())},
                {"type",string_lookup<InstrumentType>(info.type).value_or("")},
                {"multiplier",  JsonNumber(info.multiplier.to_string())},
                {"tick_scale",  JsonNumber(info.tick_scale.to_string())},
                {"lot",  JsonNumber(info.quantity_increment.to_string())}            });
            auto stream = instr.subscribe<ClosedBar>(static_cast<unsigned int>(interval*60));
            run_stream(stream.stop_on(token), info.name);
        }
        out(Event::chart_setup,{
            {"interval",interval}
        });
        auto ex = std::dynamic_pointer_cast<SimExchange>(exchange.get_handle());
        if (ex) {
            ex->set_reporter(create_report_sink());
        }
    }
    void JsonReport::attach_storage(Storage storage) {
        auto schema_map = std::unordered_map<srl::SchemaHash, Json>();
        _storage = storage;
        _storage_report = storage.add_replicator([this, schema_map](const Storage::ReplicatorEvent &ev)mutable noexcept{
            if (ev.schema_hash) {
                auto sch = schema_key_to_hash(ev.key);
                if (sch.has_value()) {
                    try {
                        auto js = Json::from_string(ev.value);
                        schema_map[*sch] = std::move(js);
                    } catch (...) {

                    }
                }
            } else if (!ev.erase && ev.key.size() > sizeof(RecordKey)) {
                    std::string_view s = ev.key;
                    s.remove_suffix(16);
                    if (s.back() == 0) {
                        s.remove_suffix(1);
                        auto rc = string_to_record_key(ev.key.substr(ev.key.size()-16));
                        srl::SchemaHash sch;
                        Json jval;
                        extract_srl(ev.value, sch, sch);
                        auto schiter = schema_map.find(sch);
                        if (schiter == schema_map.end()) {
                            jval = binary_content(ev.value);
                        } else {
                            auto arch = srl::string_deserializer(ev.value);
                            jval = srl::deserialize_from_schema(schiter->second, arch, get_desrl_resolver());
                        }            
                        out(Event::var_update,{
                            {"name", s},
                            {"rev", {rc.ordered,rc.random}},
                            {"val",std::move(jval)}
                        });
                    }
            }
        });
    }

    void JsonReport::out(Event event, const Json &json) {
        auto &f = _outp;
        auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(ExecutionWorker::current().required().now().time_since_epoch()).count();
        f << "[" << now / 1'000'000'000LL << ',' << (now % 1'000'000'000LL) << ",\"" << static_cast<char>(event) << "\"," ;
        json.serialize([&](char c){f.put(c);});
        f << "]\n";

    }


}