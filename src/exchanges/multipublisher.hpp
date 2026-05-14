#pragma once

#include <memory>
#include <mutex>
#include <utility>
#include <vector>
namespace quarkbot {
namespace bitfinex {

    template<typename T, typename Queue, typename Calc, typename Param>
    class Multipublisher {
    public:
        static auto create_subscriber(std::shared_ptr<Multipublisher> me, Param param) {
            std::lock_guard _(me->_mx);
            me->_shared_me = me;
            auto f = std::find_if(me->_publishers.begin(), me->_publishers.end(), [&](const auto &p){
                return p.first == param;
            });
            if (f != me->_publishers.end()) {
                auto p = f->second.lock();
                if (p) {
                    return p->create_subscriber(p);
                } 
                me->_publishers.erase(f);
            } 
            auto p = std::make_shared<Queue>();
            me->_publishers.push_back({param, p});
            return p->create_subscriber(p);
        }

        void publish(const T &val) {
            std::lock_guard lk(_mx);
            auto iter = std::remove_if(_publishers.begin(), _publishers.end(), [&](const auto &pub){
                auto p = pub.second.lock();
                if (p) {

                    _calc(*p, val, pub.first);

                    return true;
                }
                return false;
            });
            _publishers.erase(iter, _publishers.end());
            if (_publishers.empty()) {
                _shared_me.reset();
            }

        }


    protected:
        std::vector<std::pair<Param, std::weak_ptr<Queue> > > _publishers;
        std::shared_ptr<Multipublisher> _shared_me;
        std::mutex _mx;
        Calc _calc;

    };

}
}