#pragma once

#include "../common/orderdata.hpp"
#include "quarkbot/abstract/backtest_data_source.hpp"
#include "quarkbot/abstract/imarket_instrument.hpp"
#include "quarkbot/abstract/itradable_instrument.hpp"
#include "quarkbot/execution_worker.hpp"
#include "quarkbot/order_defs.hpp"
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <variant>
namespace quarkbot {


inline std::string csv_quotes(std::string_view text) {
    std::string out;
    auto n = text.find_first_of(",\"") ;
    if (n == text.npos) {
        out.append(text);
    } else {
        std::string out = "\"";
        for (char x: text) {
            if (x == '"') out.push_back(x);
            out.push_back(x);
        }
        out.push_back('"');
    }
    return out;

}

constexpr inline std::string_view to_string(Side side) {
    switch (side) {
        case Side::buy: return "BUY";
        case Side::sell: return "SELL";
        default: return "N/A";
    }
}

constexpr inline std::string_view to_string(OrderType x) {
    switch (x) {
    case OrderType::alert: return "ALERT";
    case OrderType::market: return "MARKET";
    case OrderType::limit: return "LIMIT";
    case OrderType::limit_post_only: return "LIMIT(post)";
    case OrderType::stop: return "STOP";
    case OrderType::stoplimit: return "STOPLIMIT";
//    case OrderType::oco: return "OCO(TP/SL)";
    default: return "Unknown";
    }
};

constexpr inline std::string_view csv_report_to_string(OrderStatus x) {
    switch (x) {
        default:
        case OrderStatus::unknown: return "UNKW";
        case OrderStatus::sent: return "SENT";
        case OrderStatus::pending_trigger: return "TRIG";
        case OrderStatus::open: return "NEW";
        case OrderStatus::filled: return "FILLED";
        case OrderStatus::canceled: return "CANCEL";
        case OrderStatus::rejected: return "REJECT";
        case OrderStatus::replaced: return "REPLACE";
        case OrderStatus::restored: return "RESTORE";
        case OrderStatus::lost: return "LOST";
    }
}

constexpr inline std::string_view to_string(OrderRejectionReason rsn) {
    switch (rsn) {
        default: return "undefined";
        case quarkbot::OrderRejectionReason::none: return "none";
        case quarkbot::OrderRejectionReason::not_tradable: return "not_tradable";
        case quarkbot::OrderRejectionReason::too_large: return "too_large";
        case quarkbot::OrderRejectionReason::too_small: return "too_small";
        case quarkbot::OrderRejectionReason::insufficient_funds: return "insufficient_funds";
        case quarkbot::OrderRejectionReason::invalid_params: return "invalid_params";
        case quarkbot::OrderRejectionReason::invalid_replace: return "invalid_replace";
        case quarkbot::OrderRejectionReason::order_not_found: return "order_not_found";
        case quarkbot::OrderRejectionReason::price_range: return "price_range";
        case quarkbot::OrderRejectionReason::post_only_taker: return "post_only_taker";
        case quarkbot::OrderRejectionReason::min_volume: return "min_volume";
        case quarkbot::OrderRejectionReason::too_late: return "too_late";
        case quarkbot::OrderRejectionReason::overloaded: return "overloaded";
        case quarkbot::OrderRejectionReason::rate_limited: return "rate_limited";
        case quarkbot::OrderRejectionReason::too_risky: return "too_risky";
        case quarkbot::OrderRejectionReason::permission_denied: return "permission_denied";
        case quarkbot::OrderRejectionReason::unsupported: return "unsupported";
        case quarkbot::OrderRejectionReason::timeout: return "timeout";
        case quarkbot::OrderRejectionReason::expired: return "expired";
        case quarkbot::OrderRejectionReason::reduce_doesnt_reduce: return "reduce_doesnt_reduce";
        case quarkbot::OrderRejectionReason::slippage: return "slippage";
        case quarkbot::OrderRejectionReason::exchange_issue: return "exchange_issue";
        case quarkbot::OrderRejectionReason::internal_error: return "internal_error";
        case quarkbot::OrderRejectionReason::other: return "other";
    }
}


template<std::invocable<std::string_view> LineOutput>
auto open_report(LineOutput line_output){
    line_output("time,event,q,instrument,order,name,side,quantity,type,limit-price,stop-price,fill-price,fill-quantity,note");
    return [line_output = std::move(line_output), buffer = std::string()]
                            (const Order &raw_order, const OrderStatusUpdate &update) mutable{        

        auto worker = ExecutionWorker::current();
        POrderData order = std::dynamic_pointer_cast<OrderInternalData>(raw_order.get_handle());
        const auto &params =order->get_parameters();
        auto iter = std::back_inserter(buffer);
        bool is_open = std::holds_alternative<OrderOpenStatus>(update);
        bool is_fill = std::holds_alternative<Fill>(update);
        OrderStatus act_status = update2status(update);        
        
        std::format_to(iter, "{:%Y-%m-%d %H:%M:%S},{},{},{}", 
            worker.now(), 
            is_fill?"FILL":csv_report_to_string(act_status),
            is_open?"✨":act_status==OrderStatus::filled?"✅":act_status == OrderStatus::replaced?"🔄":is_done_status(act_status)?"❌":"",
            csv_quotes(order->get_instrument()->get_instrument()->get_info().name));

        if (is_open) {
            const auto &st = std::get<OrderOpenStatus>(update);
            std::format_to(iter, ",{}", st.id);
        } else {
            std::format_to(iter, ",{}", order->get_id());
        }
        std::format_to(iter, ",{},{},{},{}", 
                params.label,
                to_string(params.side), 
                order->get_remaining_quantity(),
                to_string(params.type)                       
        );        
        buffer.push_back(',');
        if (is_limit_order(params.type)) {
            std::format_to(iter, "{}", params.limit_price);
        }
        buffer.push_back(',');
        if (is_stop_order(params.type)) {
            std::format_to(iter, "{}", params.stop_price);
        }
        if (is_fill) {
            const Fill &fill = std::get<Fill>(update);
            std::format_to(iter, ",{},{}", fill.price, fill.quantity);
        } else {
            buffer.append(",,");
        }        
        if (std::holds_alternative<OrderRejectionReason>(update)) {
            std::format_to(iter, ",{}",to_string(std::get<OrderRejectionReason>(update)));
        } else if (std::holds_alternative<OrderRejectionWithText>(update)) {
            std::format_to(iter, ",{} ({})",to_string(std::get<OrderRejectionWithText>(update).reason), std::get<OrderRejectionWithText>(update).text);
        } else if (is_open) {
            auto raw_rep = order->get_replaced_order().lock();
            if (raw_rep) {
                auto rep = std::dynamic_pointer_cast<OrderInternalData>(raw_rep);
                if (rep) {
                    std::format_to(iter, ",Replace: {}", rep->get_id());
                }
            }
        }
        
        line_output(buffer) ;
        buffer.clear();
    };
    

}

inline ReportSink open_report(const std::filesystem::path &output) {
    auto f = std::make_shared<std::ofstream>(output, std::ios::out|std::ios::trunc);
    if (!(*f)) throw std::runtime_error(std::format("Failed to open {}", output.string()));
    return open_report([f](std::string_view line) mutable{
        (*f) << line << "\n";
    });
}
inline ReportSink open_report(std::ostream &output) {
    return open_report([&output](std::string_view line) mutable{
        output << line << "\n";
    });

}

}