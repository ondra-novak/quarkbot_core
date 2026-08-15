#pragma once

#include "abstract/imessage_bus.hpp"
#include "event_stream.hpp"
#include "utils/wrapper.hpp"
#include <chrono>
#include <cstddef>
namespace quarkbot {


    ///Class which handles sending and receiving messages from and into the strategy
    class MessageBus: public Wrapper<IMessageBus> {
    public:
        
        using Wrapper<IMessageBus>::Wrapper;

        ///Subscribe stream
        /**
            @return message stream
        */
        EventStream<Message> subscribe() {
            if (_ptr) return EventStream<Message>(_ptr->subscribe());
            else return {};
        }


        void send(const Message &msg) {
            _ptr->send(msg);
        }

    };



}