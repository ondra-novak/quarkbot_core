#pragma once

#include "quarkbot/order_defs.hpp"
#include <variant>
namespace quarkbot {


struct OrderRejectionWithText {
    OrderRejectionReason reason;
    std::string text;
};

struct OrderOpenStatus {
    std::string id;
    RecordKey key;
};


using OrderStatusUpdateVar = std::variant<Fill, OrderStatus, OrderRejectionReason, OrderRejectionWithText,  OrderOpenStatus, OrderFillStats>;
class OrderStatusUpdate: public OrderStatusUpdateVar {
public:
    using OrderStatusUpdateVar::OrderStatusUpdateVar;
};


///Status an order ends in when it is terminated with given reason
/**
 * Not every reason is a failure. Some of them mean the order did exactly what
 * it was told and stopped existing - those end as canceled, everything else as
 * rejected.
 *
 * This is the single place where that decision is made. OrderInternalData, the
 * account notification and every reporter must derive the status from here,
 * otherwise the status a strategy acts on disagrees with the status a report
 * shows for the very same event.
 */
constexpr OrderStatus rejection_reason_2_status(OrderRejectionReason rej) {
    return (rej == OrderRejectionReason::expired || rej == OrderRejectionReason::post_only_taker)
        ?OrderStatus::canceled:OrderStatus::rejected;
}

inline OrderStatus update2status(const OrderStatusUpdate &up) {
    return std::visit([=]<typename T>(const T &x){
        if constexpr(std::is_same_v<T, OrderStatus>) {
            return x;
        } else if constexpr(std::is_same_v<T, OrderRejectionReason>) {
            return rejection_reason_2_status(x);
        } else if constexpr(std::is_same_v<T, OrderRejectionWithText>) {
            return rejection_reason_2_status(x.reason);                    
        } else if constexpr(std::is_same_v<T, OrderOpenStatus>) {
            return OrderStatus::open;
        } else if constexpr(std::is_same_v<T, OrderFillStats>) {
            return OrderStatus::open;        
        } else {
            static_assert(std::is_same_v<T,Fill>);
            return OrderStatus::open;
        }
    }, up);
}


}