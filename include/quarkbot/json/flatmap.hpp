#pragma once

#include <vector>
#include <functional>
#include <utility>
#include <algorithm>



template<typename K, typename V, typename Cmp = std::less<>, typename Allocator = std::allocator<std::pair<K,V> > >
class FlatMap: public std::vector<std::pair<K,V>, Allocator > {
public:

    using Super = std::vector<std::pair<K,V>, Allocator >;
    using value_type = Super::value_type;
    using key_type = K;
    using mapped_type = V;

    struct CmpPair {
        Cmp cmp;
        template<typename A, typename B>
        bool operator()(const A &a, const B &b) const {
            return cmp(a.first,b.first);
        }
    };

    template<typename ... Args>
    requires(std::is_constructible_v<Super, Args...>)
    FlatMap(Args && ... args): Super(std::forward<Args>(args)...) {sort();}

    template<typename ... Args>
    requires(std::is_constructible_v<Super, Args...>)
    FlatMap(Args && ... args, Cmp cmp): Super(std::forward<Args>(args)...),_cmp({std::move(cmp)})  {sort();}

    using Super::erase;

    template<typename _K>
    auto find(const _K &x) const {

        auto key =  std::pair<const _K &, std::nullptr_t>(x, nullptr);

        auto iter = std::lower_bound(this->begin(), this->end(),  key, _cmp);
        if (iter == this->end()) return iter;
        if (_cmp(key, *iter)) return this->end();
        return iter;
    }

    template<typename _K>
    auto lower_bound(const _K &x) const {
        auto key =  std::pair<const _K &, std::nullptr_t>(x, nullptr);
        return std::lower_bound(this->begin(), this->end(),  key, _cmp);
    }

    template<typename _K>
    auto upper_bound(const _K &x) const {
        auto key =  std::pair<const _K &, std::nullptr_t>(x, nullptr);
        return std::lower_bound(this->begin(), this->end(),  key, _cmp);
    }

    template<typename _K>
    auto erase(const _K &x) {
        auto iter = find(x);
        if (iter == this->end()) return iter;
        return Super::erase(iter);
    }

    template<typename _K, typename ... Args>
    auto try_emplace(_K &&key, Args &&... value_args) {
        auto iter = lower_bound(key);
        if (iter != this->end() && !_cmp(std::pair<const _K &, std::nullptr_t>(key, nullptr), *iter)) {
            auto writer = this->begin();
            auto dist = std::distance(this->cbegin(), iter);
            std::advance(writer, dist);
            return std::pair(writer,false);        
        }
        auto ins = this->insert(iter, value_type(std::forward<_K>(key), V(std::forward<Args>(value_args)...)));
        return std::pair(ins, true);
    }

    template<typename _K, typename ... Args>
    auto emplace(_K &&key, Args && ... value_args) {
        return try_emplace(std::forward<_K>(key), std::forward<Args>(value_args)...);
    }

    template<typename _K>
    V &operator[](_K &&key) {
        auto ins = try_emplace(std::forward<_K>(key));
        return ins.first->second;
    }



protected:
    CmpPair _cmp;

    void sort() {
        std::sort(this->begin(), this->end(), _cmp);
    }
};
