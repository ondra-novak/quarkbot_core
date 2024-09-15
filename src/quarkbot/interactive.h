#pragma once

#include "output_formatter.h"
#include "strategy.h"
#include "serialize.h"



namespace quarkbot {



///Support for interactive strategies controlled from command line
/**
 * @note reading standard input and output is asynchronous operation.
 *
 * This class implements part of MQ client. To access standard output and standard
 * input messages are send or receive through MQ Broker. There is three reserved
 * channels
 *
 * - `stdin` - this channel is subscribed by strategy and it is used to send commands
 * - `stdout` - sending messages to this channel appears on console
 * - `stdin_hint` - a special channel to support readline hints (when user press TAB).
 *   The feature is handled by this class
 *
 * To use this class to receive commands, your strategy must listen for MQ messages.
 * Any message shloud be passed to the instance of this class by using functions
 * as_input() or as_hint_request().
 *
 * To send message to console, use print(). To send hint (as response to as_hint_request())
 * use send_hint()
 *
 */
class Interactive {
public:

    using HintRequestTuple = TupleBin<std::size_t,std::size_t, std::string_view>;
    using HintResponseTuple = TupleBin<std::size_t, std::string_view>;


    ///Construct object and connect to strategy
    /**
     * @param s pointer to strategy
     */
    Interactive(Strategy *s):_s(s) {
        _s->subscribe_channel("stdin");
        _s->subscribe_channel("stdin_hint");
    }


    Interactive(const Interactive &other) = default;
    Interactive &operator=(const Interactive &other) = default;
    Interactive(Interactive &&other) = default;
    Interactive &operator=(Interactive &&other) = default;


    ///output an arbitrary string to console
    /**
    * @param text text to output
    */
    void output(std::string_view text) {
        _s->send_message("stdout", text);
    }


    ///print formatted string on console
    /**
     * @param pattern pattern. Use {} as placeholder or {N} as placeholder for Nth argument
     * @param args arguments. They must support operator << to iostream
     */
    template<typename ... Args>
    void print(std::string_view pattern, Args && ... args) {
        _fmt.format(pattern, std::forward<Args>(args)...);
        output(_fmt.get_buffer());
        _fmt.clear_buffer();
    }

    ///Explore MQ message and extract content
    /**
     * Checks whether message is input message from console.
     * @param msg MQ message
     * @return text from the message, if the message is from stdin, otherwise
     * empty result. The string_view inside of return value is still tied to
     * MQ message (so don't drop it)
     */
    std::optional<std::string_view> as_input(const IMQBroker::Message &msg) {
        if (msg.get_channel() == "stdin") return msg.get_content();
        return {};
    }

    ///Contains request for hint (when user pressed TAB)
    struct HintRequest {
        ///whole entered line
        std::string_view line;
        ///last word
        std::string_view word;
        ///position of the word in line
        std::size_t pos;
    };

    ///Checks, whether request is a hint request
    /**
     * @param msg MQ message
     * @return HintRequest structure if the message is a hint request otherwise
     * an empty value
     */
    std::optional<HintRequest> as_hint_request(const IMQBroker::Message &msg) {
        if (msg.get_channel() == "stdin_hint") {
            auto [srl, pos,line] = HintRequestTuple::parse(msg.get_content());
            _hint_serial_nr = srl;
            _hint_channel = msg.get_sender();
            return HintRequest{line, line.substr(pos), pos};
        } else {
            return {};
        }
    }

    ///Sends a hint.
    /**
     * You can only send hint, if there were a hint request. Hints are associated
     * with last hint request. You can send many hints as you wish.
     *
     * @param val one hint. Note that string should start by word from the hint
     * request otherwise it can damage an user input.
     *
     * @note because receiving hints is asynchronous and there is no signaling, there
     * is a small timeout on the receive side (200ms). If the strategy
     * miss this window, its hint will not appear on the first TAB. However the user
     * can press TAB again to receive these late hints.
     */
    void send_hint(std::string_view val) {
        _s->send_message(_hint_channel, HintResponseTuple::compose(_hint_serial_nr, val));
    }


protected:
    Strategy *_s;
    std::size_t _hint_serial_nr = 0;
    std::string _hint_channel = {};
    OutpuFormatter _fmt;

};




}
