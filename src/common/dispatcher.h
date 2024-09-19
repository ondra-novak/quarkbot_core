#pragma once
#include "shared/cluster_alloc.h"
#include <list>
#include <unordered_set>
#include <chrono>

struct DispatcherDefaultCollapse {

    template<typename T>
    std::size_t operator()(const T &v) const {
        if constexpr(std::invocable<std::hash<T>, T>) {
            std::hash<T> hasher;
            return hasher(v);
        } else if constexpr(std::is_polymorphic_v<T> && sizeof(T) >= sizeof(void *)+sizeof(std::size_t)) {
            return *reinterpret_cast<const std::size_t *>(reinterpret_cast<const void **>(&v)+1);
        } else {
            return *reinterpret_cast<const std::size_t *>(&v);
        }
    }

    template<typename T>
    bool operator()(const T &a1, const T &a2) const {
        if constexpr(std::equality_comparable<T>) {
            return a1 == a2;
        } else {
            return (*this)(a1) == (*this)(a2);
        }
    }

};


template<typename T, typename Hasher = DispatcherDefaultCollapse, typename Compare = DispatcherDefaultCollapse >
class DispatcherCore {
public:

    class Ident {
        std::uintptr_t id = 0;
    public:
        Ident():id(0) {}
        template<typename X> requires(std::is_integral_v<X>)
        Ident(X v):id(static_cast<std::uintptr_t>(v)) {}
        template<typename X> requires(std::is_pointer_v<X>)
        Ident(X v):id(reinterpret_cast<std::uintptr_t>(v)) {}
        bool operator==(const Ident &other) const {return id == other.id;}
    };

    template<typename ... Args> requires(std::is_constructible_v<T, Args...>)
    bool post(Args && ... args) {
        bool wk = _queue.empty();
        _queue.emplace_back(std::forward<Args>(args)...);
        return wk;
    }

    template<typename ... Args> requires(std::is_constructible_v<T, Args...>)
    bool post_collapse(Args && ... args) {
        bool wk = _queue.empty();
        _queue.emplace_back(std::forward<Args>(args)...);
        if (!wk) {
            QueueIter iter = _queue.end();
            std::advance(iter,-1);
            auto f = _collaps_map.find(iter);
            if (f != _collaps_map.end()) {
                _queue.erase(*f);
                _collaps_map.erase(f);
            }
            _collaps_map.insert(iter);
        }
        return wk;
    }

    template<std::invocable<T &&> Executor>
    bool process_message(Executor &&executor) {
        static_assert(std::is_convertible_v<std::invoke_result_t<Executor, T &&>, bool>);
        auto f = _queue.begin();
        while (_queue_recurse &&  f != _queue.end()) {
            --_queue_recurse;
            ++f;
        }
        if (f == _queue.end()) return false;
        _collaps_map.erase(f);
        ++_queue_recurse;
        if (!executor(std::move(*f))) return false;
        --_queue_recurse;
        _queue.pop_front();
        return true;
    }

    using TimePoint = std::chrono::system_clock::time_point;



    template<typename ... Args>
    bool post_timed(Ident ident, TimePoint tp, Args && ... args ) {
        T *ax = _alloc.allocate(1);
        std::construct_at(ax, std::forward<Args>(args)...);
        return post_timed_allocated(tp, {ax,{&_alloc}}, ident);
    }

    template<typename ... Args>
    bool post_timed(TimePoint tp, Args && ... args) {
        T *ax = _alloc.allocate(1);
        std::construct_at(ax, std::forward<Args>(args)...);
        return post_timed_allocated(tp, {ax,{&_alloc}}, ax);
    }

    template<std::invocable<T &&> Executor>
    bool process_message(TimePoint tp, Executor &&executor) {
        static_assert(std::is_convertible_v<std::invoke_result_t<Executor, T &&>, bool>);
        if (_tqueue.empty()) {
            _near_tp = TimePoint::max();
        } else  if (_tqueue.front()._tp > tp) {
            _near_tp = _tqueue.front()._tp;
        } else {
            auto fn = std::move(_tqueue.front()._item);
            _executing_ident = _tqueue.front()._ident;
            std::pop_heap(_tqueue.begin(), _tqueue.end(), cmp_timed);
            _tqueue.pop_back();
            _near_tp = tp;
            if (!executor(std::move(*fn))) return false;
            _executing_ident = {};

            if (_tqueue.empty()) {
                _near_tp = TimePoint::max();
            } else {
                _near_tp = _tqueue.front()._tp;
            }

            return true;
        }
        return process_message(std::forward<Executor>(executor));
    }

    bool cancel(Ident ident) {
        auto iter = std::find_if(_tqueue.begin(), _tqueue.end(), [&](const TimedItem &item){
            return item._ident == ident;
        });
        if (iter == _tqueue.end()) return false;
        if (iter == _tqueue.begin()) {
            std::pop_heap(_tqueue.begin(), _tqueue.end(), cmp_timed);
            _tqueue.pop_back();
        }
        auto last = _tqueue.end();
        --last;
        if (iter != last) {
            std::swap(*iter, *last);
            _tqueue.pop_back();
            ++iter;
            while (iter != _tqueue.end()) {
                std::push_heap(_tqueue.begin(),  iter, cmp_timed);
                ++iter;
            }
        } else {
            _tqueue.pop_back();
        }

        return true;
    }

    void clear() {
        _queue.clear();
        _collaps_map.clear();
        _tqueue.clear();
    }

    TimePoint get_nearest_schedule() const {return _queue.empty()?_near_tp: TimePoint::min();}

    bool empty() const {return _queue.empty() && _tqueue.empty();}

    Ident get_executing_ident() const {return _executing_ident;}

protected:

    static constexpr int _cluster_size = 16;
    using Queue = std::list<T, ClusterAlloc<T, _cluster_size> >;
    using QueueIter = Queue::iterator;
    struct HasherIter{
        Hasher h = {};
        bool operator()(const QueueIter &other) const {return h(*other);}
    };
    struct CmpIter{
        Compare _cmp = {};
        bool operator()(const QueueIter &a, const QueueIter &b) const {return _cmp(*a,*b);}
    };
    using CollapsMap = std::unordered_set<QueueIter,HasherIter, CmpIter, ClusterAlloc<QueueIter,_cluster_size> >;
    struct ClusterDeleter {
        ClusterAlloc<T, _cluster_size> *_alloc;
        void operator()(T *item) {std::destroy_at(item); _alloc->deallocate(item,1);}
    };
    struct TimedItem {
        TimePoint _tp;
        std::unique_ptr<T, ClusterDeleter>  _item;
        Ident _ident;
    };
    using TimedQueue = std::vector<TimedItem>;
    static bool cmp_timed(const TimedItem &a, const TimedItem &b) {return a._tp > b._tp;}


    Queue _queue;
    CollapsMap _collaps_map;
    TimedQueue _tqueue;
    TimePoint _near_tp = TimePoint::max();
    ClusterAlloc<T, _cluster_size> _alloc;
    int _queue_recurse = 0;
    Ident _executing_ident = {};

    bool post_timed_allocated(TimePoint tp, std::unique_ptr<T, ClusterDeleter> ptr, Ident ident) {
        bool r = _near_tp > tp;
        if (r) _near_tp = tp;
        _tqueue.push_back({tp, std::move(ptr), ident});
        std::push_heap(_tqueue.begin(), _tqueue.end(), cmp_timed);
        return r;
    }

};

