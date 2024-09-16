#include "console_mq_bridge.h"

#include <charconv>
#include <mutex>
#include <vector>
#include <iostream>
namespace quarkbot {

namespace {

struct index_list { // @suppress("Miss copy constructor or assignment operator")
    std::size_t from = {};
    std::size_t size = {};
    index_list *prev = nullptr;
};


template<typename _InIter1, typename _InIter2, typename _OutIter>
_OutIter parse_string(_InIter1 &&from, _InIter2 to, _OutIter out) {
    while (from != to) {
        if (*from == '|') {
            ++from;
            break;
        }
        if (*from == '~') {
            ++from;
            if (from != to) {
                switch (*from) {
                    case '~': *out = '~';break;
                    case '|': *out = '|';break;
                    default: *out = (*from-'@');break;
                }
                ++out;
                ++from;
            } else {
                break;
            }
        } else {
            *out = *from;
            ++out;
            ++from;
        }
    }
    return out;
}

void write_string(std::ostream &out, const std::string_view &str) {
    for (char c: str) {
        if (c >= '\0' && c < ' ') {
            out.put('~');
            out.put(c+'@');
        } else if (c == '|' || c == '~') {
            out.put('~');
            out.put(c);
        } else {
            out.put(c);
        }
    }
    out.put('|');
}

}

struct ConsoleMQBridge::ReadState {
    std::mutex mx;
    bool exit_flag = false;
};


ConsoleMQBridge::ConsoleMQBridge(MQBroker broker, std::ostream &out)
:MQAbstractBridgeAutoMonitor(std::move(broker))
,_out(out){}

ConsoleMQBridge::~ConsoleMQBridge() {
    auto s = _st;
    std::lock_guard _(s->mx);
    s->exit_flag = true;
}

void ConsoleMQBridge::run(std::istream &in) {
    ReadState st;
    _st = &st;
    std::unique_lock lk(st.mx);
    std::string line;
    std::vector<char> buffer;
    while (!in.eof()) {
        lk.unlock();
        std::getline(in,line);
        lk.lock();
        if (st.exit_flag) break;
        if (line.empty()) continue;
        parse(line,buffer);
    }
    _st = nullptr;
}




void ConsoleMQBridge::on_message(const Message &message,bool ) noexcept {
    _out.put('M');
    write_string(_out, message.get_sender());
    write_string(_out, message.get_channel());
    write_string(_out, message.get_content());
    _out << message.get_conversation();
    _out.put('\n');
    _out.flush();
}

void ConsoleMQBridge::send_channels_to_other_side(const ChannelList &channels) noexcept {
    _out.put('C');
    for (const ChannelID &chan: channels) {
        write_string(_out, chan);
    }
    _out.put('\n');
    _out.flush();
}


void ConsoleMQBridge::parse(std::string_view data,std::vector<char> &buffer) {
    if (data.front() == 'M') {
        parse_message(data.substr(1),buffer);
    } else if (data.front() == 'C') {
        parse_channels(data.substr(1),buffer);
    }
}

void ConsoleMQBridge::parse_message(std::string_view data,std::vector<char> &buffer) {
    buffer.clear();
    auto out = std::back_inserter(buffer);
    auto src = data.begin();
    auto end = data.end();
    out = parse_string(src, end, out);
    auto sep1 = buffer.size();
    out = parse_string(src, end, out);
    auto sep2 = buffer.size();
    out = parse_string(src, end, out);
    std::uint32_t cid = 0;
    std::from_chars(src, end, cid, 10);
    std::string_view allstr(buffer.begin(), buffer.end());
    auto msg = this->_broker.create_message(allstr.substr(0,sep1),
                                allstr.substr(sep1,sep2-sep1),
                                allstr.substr(sep2), cid);
    forward_message(msg);
}

void ConsoleMQBridge::parse_channels(std::string_view data,std::vector<char> &buffer) {
    buffer.clear();
    index_list *lst = nullptr;
    auto out = std::back_inserter(buffer);
    auto src = data.begin();
    auto end = data.end();
    std::size_t count = 0;
    while (src != end) {
        index_list *itm = reinterpret_cast<index_list *>(alloca(sizeof(index_list)));
        itm->from = buffer.size();
        out = parse_string(src, end, out);;
        itm->size =  buffer.size() - itm->from;
        itm->prev = lst;
        lst = itm;
        ++count;
    }

    std::string_view *chanlist = reinterpret_cast<std::string_view *>(alloca(sizeof(std::string_view)*count));
    auto iter = chanlist;
    auto p = lst;
    while (p) {
        std::construct_at(iter, buffer.data()+p->from, p->size);
        ++iter;
        p = p->prev;
    }
    apply_their_channels(ChannelList(chanlist, count));
}

}
