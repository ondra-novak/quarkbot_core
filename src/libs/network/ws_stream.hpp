#pragma once

#include "stream_concept.hpp"
#include "ws_parser.hpp"
#include <atomic>
#include <string_view>
namespace network {



enum class StreamRole {
    ///stream is used on server side
    server,
    ///stream is used on client side
    client
};

struct StreamConfiguration {
    StreamRole role;
    ///defines limit to maximum message size. It applied to whole message is fragmentation is disabled 
    std::size_t max_message_size = 0;
    ///determines, whether return unfinished messages (fragments)
    bool need_fragmented = false;
    ///function read() also returns pongs (otherwise they are discarded)
    bool need_pongs = false;
};

struct Message {
    FrameType type;
    std::string_view data;
    bool fin = true;
};

namespace CloseCode {
    using Type = std::uint16_t;

    constexpr Type normal = 1000;
    constexpr Type going_away = 1001;
    constexpr Type protocol_error = 1002;
    constexpr Type unsupported_data = 1003;

    constexpr Type abnormal = 1006;    

    constexpr Type invalid_frame_payload = 1007;
    constexpr Type policy_violation = 1008;
    constexpr Type message_too_big = 1009;
    constexpr Type mandatory_extension = 1010;
    constexpr Type internal_server_error = 1011;

    constexpr Type service_restart = 1012;
    constexpr Type try_again_later = 1013;
    constexpr Type TLS_handshake = 1015;
}

struct ConnCloseFrame {
    CloseCode::Type code = CloseCode::normal;
    std::string_view message = {};

    std::size_t frame_size() const {
        return 2 + message.size();
    }

    auto as_source() {
        return [this, pos = std::size_t(0)]() mutable -> char {
            switch (pos) {
                case 0: pos++; return static_cast<char>(code >> 8);
                case 1: pos++; return static_cast<char>(code & 0xFF);
                default: return message[pos++];
            }
        };
    }

    static ConnCloseFrame from_message(const std::string_view &msg_data) {
        ConnCloseFrame out;
        if (msg_data.size() < 2) return {};
        out.code = static_cast<CloseCode::Type>(static_cast<std::uint8_t>(msg_data[0]) << 8) | static_cast<CloseCode::Type>(msg_data[1]);
        out.message = msg_data.substr(2);
        return out;
    }

    static constexpr std::array<char, 2> encode_to_string(CloseCode::Type code) {
        std::array<char, 2> out;
        out[0] = static_cast<char>(code >> 8);
        out[1] = static_cast<char>(code & 0xFF);
        return out;
    }
};

enum class StreamState {
    ///stream is open
    open,
    ///stream is still open, bud close has been requested
    closing,
    ///stream is closed
    closed
};

template<CloseCode::Type code> constexpr auto close_code_message_content = ConnCloseFrame::encode_to_string(code);
template<CloseCode::Type code> constexpr auto close_message = Message{
    FrameType::close, 
    {close_code_message_content<code>.begin(), close_code_message_content<code>.end()}};


///Websocket stream class, which handles fragmentation, pings and close frames. It also detects closed connections and can report oversized frames.
// It is expected that StreamType Stream is already connected and ready to use.
/**
@tparam Stream type of stream, must satisfy StreamType concept.
@note it is expected that write operation of Stream is thread safe,
    and that there is some kind of output buffer, so write is not blocking.
     This allows to send pings and close frames from reader thread without mutex.

@note The object itself handles MT safety only for two threads, one reader and one writer.
         To call send from multiple threads, user should use mutex to synchronize calls to send and close
*/
template<StreamType Stream>
class WebsocketStream {
public:

    ///construct websocket stream
    /**
     * @param stream The underlying stream to use for communication
     * @param cfg The configuration for the websocket stream
     */
    WebsocketStream(Stream stream, StreamConfiguration cfg)
        :_stream(std::move(stream))
        ,_cfg(std::move(cfg))
        ,_parser(ParserResult{this}, _cfg.need_fragmented)
        ,_builder(BuilderResult{&_output_buffer},_cfg.role == StreamRole::client)
        ,_internal_builder(BuilderResult{&_internal_output_buffer},_cfg.role == StreamRole::client)
        {}

    ///non copyable
    WebsocketStream(const WebsocketStream &) = delete;
    ///non copyable
    WebsocketStream &operator=(const WebsocketStream &) = delete;
    

    ///retrieve message from stream, this function is blocking until message is received or connection is closed
    /**
        @return received message, if connection is closed, message with type close is returned, and payload contains close code and reason.
        @note the message object is just a view of internal buffer, so it is valid until next call to retrieve or send. If you want to keep message data, you should copy it.
    */
    Message retrieve();
    ///send message to stream
    /** 
        @param msg message to send, note that message data is expected to be valid until send returns.
        @retval true message was sent successfully
        @retval false connection is closed or broken, message was not sent
    */
    bool send(const Message &msg);
    ///request to close connection with normal close code
    /**
        Connection close requested, but connection is still open until peer responds with close frame.
        @retval true close frame was sent successfully, connection is still open
        @retval false connection is already closed or broken, close frame was not sent
    */
    bool close();    
    ///request to close connection with code
    /**
        Connection close requested, but connection is still open until peer responds with close frame.
        @param code close code to send
        @retval true close frame was sent successfully, connection is still open
        @retval false connection is already closed or broken, close frame was not sent
    */
    bool close(CloseCode::Type code);
    ///request to close connection with code and reason
    /**
        Connection close requested, but connection is still open until peer responds with close frame.
        @param code close code to send
        @param reason close reason to send
        @retval true close frame was sent successfully, connection is still open
        @retval false connection is already closed or broken, close frame was not sent
    */
    bool close(CloseCode::Type code, std::string_view reason);

    ///send ping frame
    /**
        @param ping ping payload, must be less than 126 bytes, otherwise it is treated as protocol error and connection is closed
        @retval true ping frame was sent successfully, connection is still open
        @retval false connection is already closed or broken, ping frame was not sent

        @note To receive pong frames, you must set need_pongs to true in configuration. 
    */
    bool ping(std::string_view ping);

    ///get current state of stream    
    StreamState get_state() const {return _state.load();}


protected:

    struct ParserResult {
        WebsocketStream *self;
        void operator()(char c) {
            self->put_to_input(c);
        }
        bool operator()(PayloadSize sz) {
            return self->check_size(sz.size);
        }
    };

    struct BuilderResult {
        std::vector<char> *buff;
        void operator()(char c) {
            buff->push_back(c);
        }
    };

    Stream _stream;
    StreamConfiguration _cfg;
    Parser<ParserResult> _parser;
    Builder<BuilderResult> _builder;
    Builder<BuilderResult> _internal_builder;   //internal builder is used from reader - this avoids mutex 
    

    std::vector<char> _input_buffer;
    std::vector<char> _output_buffer;
    std::vector<char> _internal_output_buffer;  //internal buffer is used from reader - this avoids mutex to send pings and closes
    std::string_view _unprocessed;

    friend struct ParserResult;
    
    void put_to_output(char c);
    void put_to_input(char c);
    bool check_size(std::size_t) ;

    std::atomic<StreamState> _state = StreamState::open;
    void set_closing() {
        StreamState expected = StreamState::open;
        _state.compare_exchange_strong(expected, StreamState::closing);
    }
    void set_closed() {
        _state.store(StreamState::closed);
    }

    bool _ping_sent = false;
    bool _oversized = false; //detected oversized frame

    bool send_output();
    
    bool send_internal_msg(Message msg);
};

template<StreamType Stream>
Message WebsocketStream<Stream>::retrieve() {
    Message resp;

    _input_buffer.clear();    
    while (true) {        
        std::string_view data = _unprocessed;
        if (data.empty()) {
            _unprocessed = _stream.read();
            if (data.empty()) {
                if (_ping_sent) {
                    set_closed();
                    return close_message<CloseCode::abnormal>;
                } else {
                    if (!send_internal_msg({FrameType::ping,{}})) {
                        return close_message<CloseCode::abnormal>;
                    }
                    _ping_sent = true;
                }
            } else {
                _ping_sent = false;
            }
        } else {
            std::size_t p = 0;
            FrameType t;
            while (p < _unprocessed.size()) {
                auto st =  _parser(_unprocessed[p]);
                if (st.has_value()) {
                    t = *st;
                    break;
                }
            }
            _unprocessed = _unprocessed.substr(p);
            std::string_view msgdata{_input_buffer.begin(), _input_buffer.end()};
            switch (t) {
                case FrameType::error:
                    if (_oversized) resp = close_message<CloseCode::message_too_big>;
                    else resp = close_message<CloseCode::protocol_error>; 
                    send_internal_msg(resp);
                    set_closed();
                    return resp;
                case FrameType::ping:
                    send_internal_msg(Message{FrameType::pong,msgdata});
                    break;
                case FrameType::pong:
                    if (_cfg.need_pongs) {
                        return Message{t, msgdata};
                    }
                    break;
                case FrameType::close:
                    send_internal_msg(close_message<CloseCode::normal>);
                    set_closed();
                    return Message{t, msgdata};
                    break;
                default:
                    return Message{t, msgdata};
                                
            }
            _input_buffer.clear();    
        }
    }
    
}
template<StreamType Stream>
bool WebsocketStream<Stream>::send(const Message &msg) {
    _builder(msg.type, msg.data, msg.fin);
    return send_output();
}
template<StreamType Stream>
bool WebsocketStream<Stream>::close() {
    bool st = send(close_message<CloseCode::normal>);    
    set_closing();
    return st;
}
template<StreamType Stream>
bool WebsocketStream<Stream>::close(CloseCode::Type code) {
    close(code, std::string_view{});
    return send_output();

}
template<StreamType Stream>
bool WebsocketStream<Stream>::close(CloseCode::Type code, std::string_view reason) {
    ConnCloseFrame msg{code,reason};
    _builder(FrameType::close, msg.frame_size(), msg.as_source(), true);
    return send_output();
}
template<StreamType Stream>
bool WebsocketStream<Stream>::ping(std::string_view ping) {
    return send(Message(FrameType::ping, ping));
}

template<StreamType Stream>
void WebsocketStream<Stream>::put_to_output(char c) {
    _output_buffer.push_back(c);
}
template<StreamType Stream>
void WebsocketStream<Stream>::put_to_input(char c) {
    _input_buffer.push_back(c);
}

template<StreamType Stream>
bool WebsocketStream<Stream>::check_size(std::size_t sz)  {
    _oversized = _cfg.max_message_size && _input_buffer.size() + sz > _cfg.max_message_size;
    return !_oversized;
}

template<StreamType Stream>
bool WebsocketStream<Stream>::send_output() {
    if (_state.load() != StreamState::open) return false;
    bool st = _stream.write(_output_buffer.begin(), _output_buffer.end());
    _output_buffer.clear();
    if (!st) set_closed();
    return st;
}

template<StreamType Stream>
bool  WebsocketStream<Stream>::send_internal_msg(Message msg) {
    if (_state.load() != StreamState::open) return false;
    _internal_builder(msg.type, msg.data, msg.fin);
    bool st = _stream.write({_internal_output_buffer.begin(), _internal_output_buffer.end()});
    _internal_output_buffer.clear();
    if (!st) set_closed();
    return st;
}

}