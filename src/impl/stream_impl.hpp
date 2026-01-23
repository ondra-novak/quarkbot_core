#pragma once
#include "../ifc/stream.hpp"
#include "ifc/defs.hpp"
#include "utils/coro_dispatch.hpp"
#include <functional>
#include <memory>
#include <mutex>
#include <vector>



namespace quarkbot {

    template<StreamType T>
    class StreamClient;

    template<StreamType T>
    class StreamServer {
    public:

        using Event = typename IMarketEventStream<T>::Event;

        bool await(std::size_t counter, std::shared_ptr<StreamClient<T> > client);
        Event get_last_event(std::size_t &my_counter) const;
        bool event_ready(std::size_t counter) const;
        void post(const T &value);
        void post(T &&value);
        
        template<typename Fn>
        void sync(Fn &&fn) {
            std::lock_guard _(_mx);
            std::invoke(std::forward<Fn>(fn));
        }

    protected:
        std::size_t _tick_counter = 0;
        mutable std::mutex _mx;
        std::vector<std::weak_ptr<StreamClient<T> > > _awaiters;
        std::optional<T> _event;

        void broadcast();
    };



    template<StreamType T>
    class StreamClient : public IMarketEventStream<T>, public std::enable_shared_from_this<StreamClient<T> >{
    public:
    
        using Event = typename IMarketEventStream<T>::Event;
        
        virtual coro::awaitable<Event> read();
        virtual void close();
        void wakeup();

    protected:
        std::shared_ptr<StreamServer<T> > _server;
        std::size_t _tick_counter = 0;
        CoroDispatchProxy<Event> _result;


    };


}