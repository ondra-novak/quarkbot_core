#include "mq_bridge.h"

#include <algorithm>
#include <numeric>
namespace quarkbot {

std::size_t MQAbstractBridge::hash_of_channel_list(const ChannelList &list) {
    size_t hash_value = 0;
    std::hash<std::string_view> hasher;

    for (const auto& str_view : list) {
        hash_value ^= hasher(str_view) + 0x9e3779b9 + (hash_value << 6) + (hash_value >> 2);
    }

    return hash_value;
}

namespace {
///Helper class which converts a lambda to an output iterator
template <typename Func>
class LambdaOutputIterator {
public:
    LambdaOutputIterator(Func func) : _fn(func) {}

    template<typename T>
    LambdaOutputIterator& operator=(T &&value) {
        _fn(std::forward<T>(value));
        return *this;
    }
    LambdaOutputIterator& operator*() { return *this; }
    LambdaOutputIterator& operator++() { return *this; }
    LambdaOutputIterator& operator++(int) { return *this; }

private:
    Func _fn;
};

}


MQAbstractBridge::MQAbstractBridge(MQBroker broker)
        :MQClient(std::move(broker)) {

}



MQAbstractBridgeAutoMonitor::MQAbstractBridgeAutoMonitor(MQBroker broker)
        :MQAbstractBridge(std::move(broker)) {
    get_broker().register_monitor(this);
}


MQAbstractBridgeAutoMonitor::~MQAbstractBridgeAutoMonitor() {
    get_broker().unregister_monitor(this);
}

void MQAbstractBridge::send_mine_channels() {
    get_broker().get_active_channels(this, [&](const ChannelList &lst){
       auto h = hash_of_channel_list(lst);
       if (h != _chan_hash) {
           _chan_hash = h;
           send_channels_to_other_side(lst);
       }
    });
}

void MQAbstractBridge::send_empty_channels() {
    if (_chan_hash) {
        _chan_hash = 0;
        send_channels_to_other_side({});
    }
}
void MQAbstractBridge::apply_their_channels(ChannelList lst) {
    std::sort(lst.begin(), lst.end());
    std::set_difference(_cur_channels.begin(), _cur_channels.end(),
                        lst.begin(), lst.end(), LambdaOutputIterator(
                                [&](const ChannelID &id) {unsubscribe(id);}));
    std::set_difference(lst.begin(), lst.end(),
                        _cur_channels.begin(), _cur_channels.end(),LambdaOutputIterator(
                                [&](const ChannelID &id) {subscribe(id);}));
    _char_buffer.resize(std::accumulate(lst.begin(), lst.end(), std::size_t(0),
            [](std::size_t x, const ChannelID &id){return x + id.size();}));
    _cur_channels.resize(lst.size());
    auto iter = _char_buffer.data();
    std::transform(lst.begin(), lst.end(),_cur_channels.begin(),[&](const ChannelID &id){
        std::string_view ret(iter, id.size());
        iter = std::copy(id.begin(), id.end(), iter);
        return ret;
    });

}

void MQAbstractBridge::forward_message(const Message &msg) {
    (*this)->forward_message(this, msg, true);
}

void MQAbstractBridge::peer_reset() {
    _chan_hash = 0;
    send_mine_channels();
}

bool MQAbstractBridgeAutoMonitor::on_message_dropped(IMQBroker::IListener *,const IMQBroker::Message &) noexcept {
    return false;
}

void MQAbstractBridgeAutoMonitor::on_channels_update() noexcept {
    send_mine_channels();
}

MQDirectBridge::MQDirectBridge(MQBroker b1, MQBroker b2)
    :_b1(*this,std::move(b1)),_b2(*this,std::move(b2)) {}



void MQDirectBridge::Bridge::send_channels_to_other_side(const ChannelList &channels) noexcept {
    _owner.on_update_chanels(*this, channels);
}

void MQDirectBridge::Bridge::on_message(const Message &message,bool ) noexcept {
    _owner.on_message(*this, message);
}

MQDirectBridge::Bridge& MQDirectBridge::select_other(const Bridge &other) {
    if (&other == &_b1) return _b2;
    if (&other == &_b2) return _b1;
    throw std::runtime_error("Invalid source bridge instance (unreachable code)");
}

void MQDirectBridge::on_update_chanels(const Bridge &source, const Bridge::ChannelList &channels) {
    Bridge &target = select_other(source);
    target.apply_their_channels(channels);
}

void MQDirectBridge::on_message(const Bridge &source, const Bridge::Message &msg) {
    Bridge &target = select_other(source);
    target.forward_message(msg);
}

}
