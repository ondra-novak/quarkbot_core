#include "msgbus_stdio.hpp" 
#include "quarkbot/abstract/imessage_bus.hpp"
#include "quarkbot/json/json.hpp"
#include "../network/base64.hpp"
#include "quarkbot/utils/refcnt.hpp"
#include <chrono>
#include <iterator>
#include <mutex>
#include <string>

namespace quarkbot {

    MessageBusStdIo::MessageBusStdIo(std::ostream &output, std::size_t queue_len)
        :_output(output)
        ,_publisher(std::make_shared<Publisher>(queue_len)) {}


    std::shared_ptr<IEventStream<Message> > MessageBusStdIo::subscribe() {
        auto s =  _publisher->subscribe();;
        return s.get_handle();
    }

    constexpr bool is_text_message(std::string_view text) {

        int extend = 0;
        std::size_t len = 0;
        for (char c: text) {
            unsigned char ch = static_cast<unsigned char>(c);
            if (extend) {
                if ((ch & 0xC0) != 0x80) return false;
                --extend;                
            } else if (ch < 0x20) {
                if (ch == '\r' || ch == '\n' || ch == '\t' || ch == '\f') len+=1;
                else len+=5; /* \u0000*/                
            } else if (ch < 0x80) {
                if (ch == '"' || ch == '\\') len+=1;
                extend = 0;
            } else if (ch < 0xC0) {
                return false;
            } else if (ch < 0xE0) {
                extend = 1;                
            } else if (ch < 0xF0) {
                extend = 2;
            } else if (ch < 0xF8) {
                extend = 3;
            } else {
                return false;
            }
            ++len;
        }
        auto max_len = (text.size() * 4 + 2) / 3;
        return extend == 0 && len <= max_len; 
    }


    void MessageBusStdIo::send(const Message &msg)  {
        Json::Array jmsg;
        jmsg.reserve(8);
        bool isbin = is_text_message(msg.payload);
        for (auto &x: std::array<Json,7>({
            msg.sender,
            msg.target,
            std::chrono::duration_cast<std::chrono::milliseconds>(msg.send_time.time_since_epoch()).count(),
            static_cast<int>(msg.type),
            msg.conversation_id,
            msg.content_type,
            isbin
        }))  jmsg.push_back(std::move(x));

        if (isbin) {
            std::string b;
            b.reserve((msg.payload.size()*4+2)/3);
            base64.encode(msg.payload.begin(), msg.payload.end(),std::back_inserter(b));
            jmsg.push_back(std::move(b));
        } else {
            jmsg.push_back(msg.payload);
        }
        std::scoped_lock _(_mx);
        Json(std::move(jmsg)).serialize([&](char c){ _output.put(c);});;
        _output.put('\n');
    }


    struct MessageObj: public RefCountInstanceWithDeleter {
        Json data;
        std::vector<char> binary;
        MessageObj(Json data):RefCountInstanceWithDeleter([](RefCountInstanceWithDeleter *ptr){
            delete static_cast<MessageObj *>(ptr);
        }), data(std::move(data)) {}
    };

    bool MessageBusStdIo::process_message(std::istream &input) {
        if (std::getline(input,_line_buffer)) {
            try {
                auto jmsg = std::make_unique<MessageObj>(Json::from_string(_line_buffer));
                Message msg;
                msg.sender = jmsg->data[0].as_text();
                msg.target = jmsg->data[1].as_text();
                msg.send_time = std::chrono::system_clock::time_point(std::chrono::duration_cast<std::chrono::system_clock::duration>(
                    std::chrono::milliseconds(jmsg->data[2].as<std::int64_t>())
                ));
                msg.type = static_cast<MessageType>(jmsg->data[3].as_int());
                msg.conversation_id = jmsg->data[4].as<std::uint_least32_t>();
                msg.content_type = jmsg->data[5].as_text();
                bool isbin = jmsg->data[6].as_bool();
                msg.payload = jmsg->data[7].as_text();
                if (isbin) {
                    jmsg->binary.reserve((msg.payload.size()*3+3)/4);
                    base64.decode(msg.payload.begin(), msg.payload.end(), std::back_inserter(jmsg->binary));
                    msg.payload = std::string_view(jmsg->binary.data(), jmsg->binary.size());
                }
                msg.ownership = jmsg.release();
                _publisher->publish(std::move(msg));

            } catch (std::exception &e) {
                logError("MessageBus: message dropped because: {}, - message: {}", e.what(), _line_buffer);                
            }
            return true;
        }
        return false;

    }
    void MessageBusStdIo::process_all_messages(std::istream &input) {
        while (process_message(input));        
    }

}