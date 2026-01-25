#include "data_source_base.hpp"
#include "ifc/stream.hpp"
#include <memory>
#include <mutex>

namespace quarkbot {

bool DataSourceBase::post(const Key &topic, const StreamTypeItem &data) {
    std::unique_lock lk(_mx);
    auto iter =  _map.find(topic);
    if (iter == _map.end()) {
        return false;
    }
    return broadcast(lk, iter, 0, 0, data);    
}

constexpr std::size_t count_of_receivers_in_block = 32;

bool DataSourceBase::broadcast(std::unique_lock<std::mutex> &lk,
        Map::iterator iter, std::size_t rd_pos, std::size_t wr_pos, 
         const StreamTypeItem &data) {
    Targets &tgs = iter->second;
    if (rd_pos >= tgs.size()) {
        bool b = true;
        if (wr_pos) {
            tgs.resize(wr_pos);        
            b = true;
        } else {        
            _map.erase(iter);
            b = false;
        }        
        lk.unlock();
        return b;
    }

    std::shared_ptr<IDataReceiver> receives[count_of_receivers_in_block];
    std::size_t rcsz = 0;
    while (rcsz < count_of_receivers_in_block) {
        auto lk = tgs[rd_pos].lock();
        if (!lk) {
            ++rd_pos;
            if (rd_pos == tgs.size()) break;
            continue;;
        } 
        tgs[wr_pos] = std::move(tgs[rd_pos]);
        ++wr_pos;
        ++rd_pos;
        receives[rcsz] = std::move(lk);
        ++rcsz;
    }
    bool b = broadcast(lk, iter, rd_pos, wr_pos, data);
    for (std::size_t p = 0; p < rcsz; ++p) {
        receives[p]->on_data_received(data);
    }    
    return b;
}



bool DataSourceBase::subscribe(std::string_view topic, std::shared_ptr<IDataReceiver> receiver) {
    Key k {std::string(topic), receiver->get_type()};
    std::lock_guard _(_mx);
    auto iter = _map.find(k);
    if (iter != _map.end()) {
        iter->second.push_back(receiver);
        return true;
    }
    if (!enable_stream(topic, k.type)) return false;
    _map.emplace(k, Targets(1, receiver));
    return true;
}


}