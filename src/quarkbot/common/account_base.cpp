#include "account_base.hpp"
#include <mutex>

namespace quarkbot {
 
    RiskController AccountBase::set_risk_controller(RiskController c) {
        std::unique_lock _(_mx);
        return std::exchange(_risk, std::move(c));
    }
    


}