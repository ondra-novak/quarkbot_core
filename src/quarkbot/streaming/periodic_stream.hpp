#pragma once

#include "basic_coro/pending.hpp"
#include "lock_free_publisher.hpp"
#include "publisher_base.hpp"
#include "quarkbot/abstract/ieventstream.hpp"
#include "quarkbot/execution_worker.hpp"
#include "quarkbot/hash/hashable.hpp"
#include "quarkbot/strategy_fragment.hpp"
#include "quarkbot/timer.hpp"
#include <chrono>
#include <concepts>
#include <memory>
#include <mutex>
#include <type_traits>
#include <unordered_map>
namespace quarkbot {


template<typename InstrumentDef, typename Type, typename Factory>
class PeriodicStreamManager {
public:

    using Duration = std::chrono::system_clock::duration;
    using RetrieveCallback = std::invoke_result_t<Factory, InstrumentDef, Duration>;

    static_assert(std::is_invocable_r_v<bool, RetrieveCallback ,Type &>);

    using Publisher = LockFreePublisher<Type, 1>;

    PeriodicStreamManager(ExecutionWorker worker, Factory factory):_worker(std::move(worker)), _factory(std::move(factory)) {}

    std::shared_ptr<IEventStreamBase > subscribe(InstrumentDef instr, Duration dur) {
        std::scoped_lock _(_mx);
        auto &ptr = _map[Key{std::move(instr), dur}];
        auto pub = ptr.lock();
        if (!pub) {
            ptr = pub = std::make_shared<Publisher>();
            _worker.run(start_timer(_factory(instr,dur), dur, pub));
        }
        return pub->create_subscriber(pub);        
    }

protected:

    ExecutionWorker _worker;
    Factory _factory;

    struct Key {
        InstrumentDef instrument;
        Duration duration;

        std::size_t get_hash() const {
            Hasher<InstrumentDef> instr_hasher;            
            return instr_hasher(instrument) + static_cast<std::size_t>(duration.count());
        }
        bool operator==(const Key &) const = default;
    };

    std::mutex _mx;
    std::unordered_map<Key, std::weak_ptr<Publisher>, Hasher<Key> > _map;

    StrategyFragment start_timer(RetrieveCallback cb, Duration dur, std::weak_ptr<Publisher> publisher) {
        ExecutionWorker worker = ExecutionWorker::current();
        while (true) {
            auto now = worker.now();
            auto next = std::chrono::system_clock::time_point(
                 std::chrono::system_clock::duration((now.time_since_epoch().count() / dur.count()) + 1)* dur.count());
            co_await worker.sleep_until(next);
            auto lk = publisher.lock();
            if (!lk) break;
            Type val;
            if (!cb(val)) break;
            lk->publish(val);
        }
    }
};

}