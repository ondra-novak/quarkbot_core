#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
namespace quarkbot {


template<typename InstrumentRef, typename Publisher>
class SingleStreamMap {
public:

    auto create_subscriber(const InstrumentRef &instr) {
        std::scoped_lock _(_mx);
        auto &itm = _map[instr];
        auto ref = itm.lock();
        if (!ref) {
            ref = std::make_shared<Publisher>();
            itm = ref;
        }
        return ref->create_subscriber(ref);
    }


    std::shared_ptr<Publisher> get_publisher(const InstrumentRef &instr) {
        std::scoped_lock _(_mx);
        auto iter = _map.find(instr);
        if (iter == _map.end()) return  {};
        auto ref = iter->second.lock();
        if (!ref) {
            _map.erase(iter);
        }
        return ref;
    }
    template<std::invocable<Publisher &>  Fn>
    bool with_publisher(const InstrumentRef &instr, Fn &&fn) {
        auto pub = get_publisher(instr);
        if (pub) {
            std::invoke(std::forward<Fn>(fn),*pub);
            return true;
        }
        return false;
    }

protected:
    std::mutex _mx;
    std::unordered_map<InstrumentRef, std::weak_ptr<Publisher> > _map;
};

template<typename InstrumentRef, typename Publisher, typename Param>
class ParametrizedStreamMap {
public:

    auto create_subscriber(const InstrumentRef &instr, const Param &param) {
        std::scoped_lock _(_mx);
        std::vector<Record>  &itm = _map[instr];
        auto fnd = std::find_if(itm.begin(), itm.end(), [&](const Record &rec){
            return rec.param == param;
        });
        std::shared_ptr<Publisher> pub;
        if (fnd == itm.end()) {
            pub = std::make_shared<Publisher>();
            itm.push_back({pub, param});
        } else {
            pub = fnd->_pub.lock();
            if (!pub) {
                pub = std::make_shared<Publisher>();
                fnd->_pub = pub;
            }
        }
        return pub->create_subscriber(pub);
    }


    template<std::invocable<Param, Publisher &> Fn>
    bool enum_publisher(const InstrumentRef &instr, Fn &&fn) {
        std::scoped_lock _(_cache_lock);
        _cache.clear();
        if (!fill_cache(instr)) return false;
        for (auto &[pub, param]:_cache) {
            std::invoke(std::forward<Fn>(fn), param, *pub);
        }
        return true;
    }
   


protected:
    std::mutex _mx;
    std::mutex _cache_lock;
     struct List {
        std::shared_ptr<Publisher> publisher;
        Param param;
    };

    struct Record {
        std::weak_ptr<Publisher> _pub;
        Param param;
    };

    std::unordered_map<InstrumentRef, std::vector<Record> > _map;
    std::vector<List> _cache;

    bool fill_cache(const InstrumentRef &instr) {
        std::scoped_lock _(_mx);        
        auto iter = _map.find(instr);
        if (iter == _map.end()) return false;
        auto &lst = iter->second;
        auto enditer = std::remove_if(lst.begin(), lst.end(), [&](const Record &r){
            auto lk = r._pub.lock();
            if (!lk) return true;
            _cache.push_back(List{lk, r.param});
            return false;
        });
        lst.erase(enditer, lst.end());
        return true;
    }

};


}