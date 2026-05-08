#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstdint>
#include <optional>
#include <random>
namespace network {


enum class FrameType {
    text = 0x1,
    binary = 0x2,
    close = 0x8,
    ping = 0x9,
    pong = 0xA,
    continuation = 0x0,
    error = 0xff, //not a valid frame type, used to indicate an error
};

struct PayloadSize {
    ///size of payload
    std::uint64_t size;
    ///true if this is the final frame of a message, false if more frames are expected (for fragmented messages)
    bool fin;
};

///WebSocket frame parser, parses incoming bytes and outputs unmasked payload through the output function
/**
 * The parser maintains internal state and processes incoming bytes according to the WebSocket framing protocol.
 * It handles fragmentation, masking, and payload length encoding as specified in the WebSocket RFC.
 * The output function is called with unmasked payload bytes as they are parsed.
 * The parser can be configured to output complete frames only or also fragments, depending on the need_fragments parameter in the constructor.
 * @tparam Output A callable type that takes a char as input, used to output unmasked payload bytes. 
        It can also optionally take a PayloadSize for reporting payload size. It can return false to indicate an error when reporting size,
         in which case the parser will stop parsing and return an error frame type. This can be used to implement size limits or other constraints on the payload.
 */
template<std::invocable<char> Output>
class Parser {
public:

    ///Constructor
    /**
     * @param output The output function to call with parsed data
     * @param need_fragments By default, the parser will only output complete frames. 
            If this is set to true, it will output fragments as they are parsed. 
            If you want to see continuation frames, you must set this to true.
     */
    Parser(Output output, bool need_fragments = false) 
        : _output(std::move(output))
        , _need_fragments(need_fragments) {}

    ///Parse a single byte, returns a frame if a complete frame is parsed, otherwise std::nullopt
    std::optional<FrameType> operator()(char input);

    ///Checks whether message is complete. This is useful, when need_fragmens is true to detect, whether continuation frame is expected
    /**
        @param parser parser object
        @retval true message is complete (last frame had fin set to true)
        @retval false continuation frames are expected
        @note function returns valid result only when frame has been finished recently. Otherwise result is undefined
    */
    friend bool is_complete(const Parser &parser) {return parser._fin;}

protected:
    enum class State {
        first_byte,
        length,
        extended_length,
        mask,
        payload
    };

    Output _output;
    bool _need_fragments = false;
    State _state = State::first_byte;
    FrameType _frame_type;
    bool _fin;
    bool _masked;
    std::uint64_t _payload_length;
    std::array<std::uint8_t, 4> _mask = {};
    std::uint8_t _extended_length_size = 0;
    std::uint8_t _mask_index = 0;

    std::optional<FrameType> parse_first_byte(char input);
    std::optional<FrameType> parse_length(char input);
    std::optional<FrameType> parse_extended_length(char input);
    std::optional<FrameType> parse_mask(char input);
    std::optional<FrameType> parse_payload(char input);
    std::optional<FrameType> finalize(std::optional<FrameType> frame_type);
    bool report_size();

};


template<std::invocable<char> Output>
class Builder {
public:

    Builder(Output output, bool masked):_output(std::move(output)), _need_mask(masked) {
        if (_need_mask) {
            std::random_device rdev;
            _random = std::default_random_engine(rdev());            
        }

    }

    ///generate frame
    /**
    @param type type of frame
    @param data frame data
    @param last_frame optional, set to false, if this is not last frame in the message. Default value expects that every message has exact 1 frame
     */
    void operator()(FrameType type, std::string_view data, bool last_frame = true);

    ///generate frame from source
    /**
    @param type frame type
    @param size expected frame size
    @param source function which generates frame bytes
    @param last_frame set to false, if this is not last frame
    */
    template<typename Fn>
    requires(std::is_invocable_r_v<char, Fn>)
    void operator()(FrameType type, std::size_t size, Fn &&source, bool last_frame = true);

protected:
    Output _output;
    bool _need_mask;
    std::default_random_engine _random;
};

template<std::invocable<char> Output>
std::optional<FrameType> Parser<Output>::operator()(char input) {
    switch (_state) {
        case State::first_byte:  return finalize(parse_first_byte(input));
        case State::length:return finalize(parse_length(input));
        case State::extended_length: return finalize(parse_extended_length(input));
        case State::mask: return finalize(parse_mask(input));
        case State::payload: return finalize(parse_payload(input));
        default:return FrameType::error;
    }    
}

template<std::invocable<char> Output>
std::optional<FrameType> Parser<Output>::parse_first_byte(char input) {
    FrameType ft = static_cast<FrameType>(input & 0x0F);
    if (ft == FrameType::continuation) {
        if (_fin) return FrameType::error; //continuation frame must be preceded by a non-continuation frame
        //keep type of previous frame
    } else {
        if (!_fin) return FrameType::error; //non-continuation frame must not be preceded by an unfinished frame
        _frame_type = ft; //set type of current frame
    }
    _fin = (input & 0x80) != 0;
    _state = State::length;
    _masked = false;
    _payload_length = 0;
    _extended_length_size = 0;
    _mask_index = 0;
    std::fill(_mask.begin(), _mask.end(), 0);
    return std::nullopt;
}
template<std::invocable<char> Output>
std::optional<FrameType> Parser<Output>::parse_length(char input) {
    _masked = (input & 0x80) != 0;
    std::uint8_t l = input & 0x7F;
    switch (l) {
        case 126:
            _extended_length_size = 2;
            _state = State::extended_length;
        case 127:
            _extended_length_size = 8;
            _state = State::extended_length;
        case 0:
            if (!_masked) return _frame_type; //empty payload, frame is complete
            _payload_length = 0;
            if (!report_size()) return FrameType::error; //report size to output, if it returns false, it's an error
            _state = State::mask;
            break;
        default:
            _payload_length = l;
            _state = _masked ? State::mask : State::payload;
            break;
    }
    return std::nullopt;
}
template<std::invocable<char> Output>
std::optional<FrameType> Parser<Output>::parse_extended_length(char input) {
    std::uint64_t l = static_cast<std::uint8_t>(input);
    _payload_length = (_payload_length << 8) | l;
    if (--_extended_length_size == 0) {
        if (!report_size()) return FrameType::error; //report size to output, if it returns false, it's an error
        if (_payload_length == 0) {
            if (!_masked) return _frame_type; //empty payload, frame is complete
            _state = State::mask;
        } else {
            _state = _masked ? State::mask : State::payload;
        }        
    }
    return std::nullopt;
}

template<std::invocable<char> Output>
std::optional<FrameType> Parser<Output>::parse_mask(char input) {
    _mask[_mask_index++] = static_cast<std::uint8_t>(input);
    if (_mask_index == 4) {
        if (_payload_length == 0) return _frame_type; //empty payload, frame is complete
        _state = State::payload;
    }
    return std::nullopt;
}

template<std::invocable<char> Output>
std::optional<FrameType> Parser<Output>::parse_payload(char input) {
    char unmasked = input ^ _mask[_mask_index % 4];
    _output(unmasked);
    _payload_length--;
    _mask_index++;
    if (_payload_length == 0) return _frame_type;
    return std::nullopt;
}

template<std::invocable<char> Output>
std::optional<FrameType> Parser<Output>::finalize(std::optional<FrameType> frame_type) {
    if (frame_type.has_value()) {
        _state = State::first_byte;
        if (!_fin && !_need_fragments) return std::nullopt; //if frame is not finished and we don't need fragments, don't return frame type yet
        return frame_type;
    }
    return std::nullopt;
}

template<std::invocable<char> Output>
bool Parser<Output>::report_size() {
    if constexpr(std::is_invocable_r_v<bool, Output, PayloadSize>) {
        return _output(PayloadSize{_payload_length, _fin});
    }else if constexpr(std::is_invocable_v<Output, PayloadSize>) {
        _output(PayloadSize{_payload_length, _fin});
        return true;
    } else {
        return true; //output doesn't support reporting size, just return true to continue parsing
    }
}
template<std::invocable<char> Output>
void Builder<Output>::operator()(FrameType type, std::string_view data, bool fin) {
    std::size_t pos = 0;
    this->operator()(type, data.size(), [&]{return data[pos++];}, fin);
}

template<std::invocable<char> Output>
template<typename Fn>
requires(std::is_invocable_r_v<char, Fn>)
void Builder<Output>::operator()(FrameType type, std::size_t frame_size, Fn &&source, bool fin) {
    std::array<std::uint8_t, 4> mask = {};
    std::uint8_t mask_index = 0;


    auto fb = static_cast<char>((fin?0x80:0) | static_cast<int>(type));
    _output(fb);
    std::size_t dtsz = frame_size;
    auto sb = static_cast<char>((_need_mask?0x80:0) | (dtsz>0xFFFF?127:dtsz>125?126:dtsz));
    _output(sb);
    if (dtsz > 0xFFFF) {
        for (int i = 0; i < 8; ++i) {
            _output(static_cast<char>((dtsz >> (8*(7-i)))& 0xFF));            
        }
    } else if (dtsz > 125) {
        for (int i = 0; i < 2; ++i) {
            _output(static_cast<char>((dtsz >> (8*(2-i)))& 0xFF));            
        }
    }
    if (_need_mask) {
        std::uniform_int_distribution<std::uint8_t> dist(0,255);
        for (auto &a: mask) {
            a = dist(_random);
            _output(static_cast<char>(a));
        }
    }
    for (std::size_t i = 0; i < dtsz; ++i) {
        char c = std::forward<Fn>(source)();
        char mc = c ^ static_cast<char>(mask[mask_index & 3]);
        ++mask_index;
        _output(mc);
    }
}


} // namespace ws