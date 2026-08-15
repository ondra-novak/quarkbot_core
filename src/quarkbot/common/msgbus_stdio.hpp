#pragma once

#include "quarkbot/abstract/imessage_bus.hpp"
#include "quarkbot/strategy_publisher.hpp"
#include <memory>
#include <mutex>
#include <ostream>
namespace quarkbot {


    class MessageBusStdIo: public IMessageBus {
    public:

        using Publisher = StrategyPublisher<Message>;
    
        MessageBusStdIo(std::ostream &output, std::size_t queue_len = 1000);
        

        virtual std::shared_ptr<IEventStream<Message> > subscribe() override;
        virtual void send(const Message &msg) override;

        bool process_message(std::istream &input);
        void process_all_messages(std::istream &input);


    protected:
        


        std::mutex _mx;
        std::ostream &_output;
        std::shared_ptr<Publisher> _publisher;
        std::string _line_buffer;
    };

}