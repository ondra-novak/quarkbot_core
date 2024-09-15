#include "basic_mq.h"
#include <algorithm>
#include <random>

#include <atomic>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif


namespace quarkbot {



template<typename Iter>
Iter to_base62(std::uint64_t x, Iter iter, int digits = 1) {
    if (x>0 || digits>0) {
        iter = to_base62(x/62, iter, digits-1);
        auto rm = x%62;
        char c = rm < 10?'0'+rm:rm<36?'A'+rm-10:'a'+rm-36;
        *iter = c;
        ++iter;
        return iter;
    }
    return iter;
}

template<typename Iter>
static void generate_mailbox_id(Iter iter) {
    static std::atomic<std::uint64_t> counter = {0};
    std::random_device dev;
    auto rnd = dev();
    auto now = std::chrono::system_clock::now();
    #ifdef _WIN32
        auto pid = GetCurrentProcessId();
    #else
        auto pid= ::getpid();
    #endif
    iter = to_base62(std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count(), iter);
    iter = to_base62(pid, iter);
    iter = to_base62(counter++,iter,1);
    iter = to_base62(rnd,iter, 1);
}




class BasicMQ::MessageDef: public IMQBroker::IMessage {
public:
    MessageDef(std::string_view sender, std::string_view channel, std::string_view message, ConversationID cid, std::shared_ptr<BasicMQ> owner);
    virtual std::string_view get_sender() const override {return sender;}
    virtual std::string_view get_channel() const override {return channel;}
    virtual MessageContent get_content() const override {return message;}
    virtual ConversationID get_conversation() const override {return cid;}

protected:
    std::vector<char, std::pmr::polymorphic_allocator<char> > data;
    std::string_view sender;
    std::string_view channel;
    std::string_view message;
    ConversationID cid;
    std::shared_ptr<BasicMQ> owner;
};

BasicMQ::BasicMQ()
    :_channels(ChannelMap::allocator_type(&_mem_resource))
    ,_listeners(ListenerToChannelMap::allocator_type(&_mem_resource))
    ,_mailboxes_by_ptr(ListenerToMailboxMap::allocator_type(&_mem_resource))
    ,_mailboxes_by_name(MailboxToListenerMap::allocator_type(&_mem_resource))
    ,_monitors(mvector<IMonitor *>::allocator_type(&_mem_resource))
{

}

BasicMQ::PChanMapItem BasicMQ::get_channel(ChannelID channel) {
    std::lock_guard _(_mx);
    return get_channel_lk(channel);
}
BasicMQ::PChanMapItem BasicMQ::get_channel_lk(ChannelID channel) {
    auto iter = _channels.find(channel);
    if (iter == _channels.end()) {
        auto chan = std::make_shared<ChanDef>(channel, &_mem_resource);
        _channels.emplace(chan->get_id(), chan);
        channel_list_updated();
        return chan;
    }
    return iter->second;
}

void BasicMQ::subscribe(IListener *listener, ChannelID channel)
{
    std::lock_guard _(_mx);
    auto chan = get_channel_lk(channel);
    chan->add_listener(listener);
    auto iter = _listeners.find(listener);
    if (iter == _listeners.end()) {
        iter = _listeners.emplace(listener, mvector<ChannelID>(mvector<ChannelID>::allocator_type(&_mem_resource))).first;
    }
    iter->second.push_back(chan->get_id());
}

void BasicMQ::unsubscribe(IListener *listener, ChannelID channel)
{
    if (remove_channel_from_listener(channel,listener)) { //mt-safe
        auto chan = get_channel(channel);            //mt-safe
        if (chan->remove_listener(listener)) {          //mt-safe
            std::lock_guard _(_mx);                 //lock here
            _channels.erase(channel);
            channel_list_updated();
        }
    }
}

void BasicMQ::unsubscribe_all(IListener *listener)
{
    std::lock_guard _(_mx);
    erase_mailbox(listener);
    bool ech = false;
    auto iter = _listeners.find(listener);
    if (iter != _listeners.end()) {
        auto &chans = iter->second;
        for (auto &ch: chans) {
            auto chptr = get_channel(ch);
            if (chptr->remove_listener(listener)) {
                _channels.erase(ch);
                ech = true;
            }
        }
        _listeners.erase(iter);
    }
    if (ech) channel_list_updated();
}

void BasicMQ::unsubcribe_private(IListener *listener) {
    std::lock_guard _(_mx);
    erase_mailbox(listener);
}
std::string BasicMQ::create_random_channel_name(std::string_view prefix) const {
    std::string out(prefix);
    generate_mailbox_id(std::back_inserter(out));
    return out;
}


void BasicMQ::erase_mailbox(IListener *listener) {
    //always under lock
    auto iter = _mailboxes_by_ptr.find(listener);
    if (iter == _mailboxes_by_ptr.end()) return;
    _mailboxes_by_name.erase(iter->second);
    _mailboxes_by_ptr.erase(iter);
    //no channel_list_updated() - handled by unsubscribe_all()
}

std::string_view BasicMQ::get_mailbox(IListener *listener)
{
    static constexpr std::string_view mbx_prefix = "!mbx_";

    std::lock_guard _(_mx);
    auto iter = _mailboxes_by_ptr.find(listener);
    if (iter != _mailboxes_by_ptr.end()) return iter->second;
    mstring mbid((mstring::allocator_type(&_mem_resource)));
    mbid.append(mbx_prefix);
    generate_mailbox_id(std::back_inserter(mbid));
    iter = _mailboxes_by_ptr.emplace(listener, std::move(mbid)).first;
    auto ret = _mailboxes_by_name.emplace(iter->second, listener).first->first;
    return ret;
}


IMQBroker::Message BasicMQ::create_message(ChannelID sender, ChannelID channel, MessageContent msg, ConversationID cid) {
    return Message(std::allocate_shared<MessageDef>(
        std::pmr::polymorphic_allocator<MessageDef>(&_mem_resource),
        std::string(sender), std::string(channel), std::string(msg), cid,
        shared_from_this()
    ));
}
void BasicMQ::send_message(IListener *listener, ChannelID channel, MessageContent message, ConversationID cid)
{
    //no lock needed there
    if (listener == nullptr) {
        forward_message(nullptr, create_message({},channel,message,cid));
    } else {
        forward_message(listener, create_message(get_mailbox(listener), channel, message, cid));
    }
}


bool BasicMQ::remove_channel_from_listener(std::string_view channel, IListener *listener)
{
    std::lock_guard _(_mx);
    auto iter = _listeners.find(listener);
    if (iter == _listeners.end()) return false;
    auto &lst = iter->second;
    auto f = std::find(lst.begin(), lst.end(), channel);
    if (f == lst.end()) return false;
    lst.erase(f);
    if (lst.empty()) {
        _listeners.erase(iter);
    }
    return true;
}

void BasicMQ::forward_message(IMQBroker::IListener *listener, const IMQBroker::Message &msg) {
    PChanMapItem ch;
    ChannelID chanid = msg.get_channel();

    {
        std::lock_guard _(_mx);
        auto miter = _mailboxes_by_name.find(chanid);
        if (miter != _mailboxes_by_name.end()) {
            auto l = miter->second;
            l->on_message(msg,true);
            return;
        }

        auto citer = _channels.find(chanid);
        if (citer == _channels.end()) {
            for (const auto m: _monitors) m->message_dropped(listener, msg);
            return; //unknown channel, drop message
        }
        ch = citer->second;
    }

    //process channel outside of lock (has own lock)
    ch->enum_listeners([listener, &msg](IListener *l){
        if (l != listener) l->on_message(msg, false);
    });
}

void BasicMQ::register_monitor(IMonitor *mon) {
    std::lock_guard lk(_mx);
    _monitors.push_back(mon);
}

void BasicMQ::unregister_monitor(const IMonitor *mon) {
    std::lock_guard lk(_mx);
    auto iter = std::find(_monitors.begin(), _monitors.end(), mon);
    if (iter != _monitors.end()) {
        std::swap(*iter, _monitors.back());
        _monitors.pop_back();
    }
}


void BasicMQ::channel_list_updated() {
    //always under lock
    for (const auto &m: _monitors) m->channels_update();
}

void BasicMQ::get_active_channels(IMQBroker::IListener *listener,
        IMQBroker::IChannelListCallback &&cb) const {
    std::lock_guard _(_mx);
    _tmp_channels.clear();
    for (const auto &[k,v]: _channels) {
        if (v->can_export(listener)) {
            _tmp_channels.push_back(k);
        }
    }
    cb({_tmp_channels.begin(), _tmp_channels.end()});
}

void BasicMQ::get_subscribed_channels(IMQBroker::IListener *listener,
        IMQBroker::IChannelListCallback &&cb) const {
    std::lock_guard _(_mx);
    auto iter = _listeners.find(listener);
    if (iter == _listeners.end()) return;
    _tmp_channels.clear();
    _tmp_channels.resize(iter->second.size());
    std::copy(iter->second.begin(), iter->second.end(), _tmp_channels.begin());
    cb({_tmp_channels.begin(), _tmp_channels.end()});
}

BasicMQ::ChanDef::ChanDef(std::string_view name, std::pmr::memory_resource *memres)
    :_name(name, std::pmr::polymorphic_allocator<char>(memres))
    ,_listeners(std::pmr::polymorphic_allocator<std::pair<IListener *, bool> >(memres)) {}

void BasicMQ::ChanDef::lock() {
    _mx.lock();
    ++_recursion;
}

void BasicMQ::ChanDef::unlock() {
    --_recursion;
    if (!_recursion && _del_count) {
        auto iter = std::remove(_listeners.begin(), _listeners.end(), nullptr);
        _listeners.erase(iter, _listeners.end());
        _del_count = 0;
    }
    _mx.unlock();
}

template<std::invocable<BasicMQ::IListener *> Fn>
void BasicMQ::ChanDef::enum_listeners(Fn &&fn) {
    std::lock_guard _(*this);
    //using numeric counter is intentional
    //underlying array can resize self which renders iterators unusable
    for (std::size_t i = 0; i < _listeners.size(); ++i) {
        auto l = _listeners[i];
        if (l) fn(l);
    }
}



bool BasicMQ::ChanDef::empty() const {
    std::lock_guard _(_mx);
    return (_listeners.size() - _del_count) == 0;
}

void BasicMQ::ChanDef::add_listener(IListener *lsn) {
    std::lock_guard _(*this);
    _listeners.push_back(lsn);
}

bool BasicMQ::ChanDef::remove_listener(IListener *lsn) {
    std::lock_guard _(*this);
    auto iter = std::find(_listeners.begin(), _listeners.end(), lsn);
    if (iter != _listeners.end()) {
        *iter = nullptr;
        ++_del_count;
    }
    return (_listeners.size() - _del_count) == 0;

}

bool BasicMQ::ChanDef::can_export(IListener *lsn) const {
    std::lock_guard _(_mx);
    auto iter = std::find_if(_listeners.begin(), _listeners.end(), [&](const auto &l){
        return l && l != lsn;
    });
    return iter != _listeners.end();
}

BasicMQ::ChannelID BasicMQ::ChanDef::get_id() const {
    return _name; //no lock is needed (it is immutable)
}

MQBroker BasicMQ::create() {
    return MQBroker(std::make_shared<BasicMQ>());
}

bool BasicMQ::is_channel(ChannelID id) const {
    std::lock_guard _(_mx);
    auto iter = _channels.find(id);
    return iter != _channels.end() && !iter->second->empty();
}

BasicMQ::MessageDef::MessageDef(std::string_view sender,
        std::string_view channel, std::string_view message, ConversationID cid,
        std::shared_ptr<BasicMQ> owner)
:data(std::pmr::polymorphic_allocator<char>(&owner->_mem_resource))
,cid(cid)
,owner(std::move(owner))
{
    //we use polymorphic allocator to allocate one space for all three strings
    //calculate total size (+ 3times terminating zero)
    auto needsz = sender.size()+channel.size()+message.size()+3;
    //allocate buffer
    data.resize(needsz,0);
    //copy each string to buffer
    //construct string_view
    //append zero
    auto iter = data.data();
    this->sender = std::string_view(iter, sender.size());
    iter = std::copy(sender.begin(), sender.end(), iter);
    *iter++ = 0;
    this->channel= std::string_view(iter, channel.size());
    iter = std::copy(channel.begin(), channel.end(), iter);
    *iter++ = 0;
    this->message= std::string_view(iter, message.size());
    iter = std::copy(message.begin(), message.end(), iter);
    *iter++ = 0;
}
}

