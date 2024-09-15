#pragma once

#include "mq_bridge.h"


namespace quarkbot {

class ConsoleMQBridge: public MQAbstractBridge {
public:

    ConsoleMQBridge(MQBroker broker, std::ostream &out);
    ~ConsoleMQBridge();

    void run(std::istream &in);


    virtual void on_message(const IMQBroker::Message &message, bool pm) noexcept override;
    virtual void on_update_channels(
            const quarkbot::MQAbstractBridge::ChannelList &channels) noexcept
                    override;


protected:
    std::ostream &_out;
    struct ReadState;
    ReadState *_st = nullptr;




    void parse(std::string_view data, std::vector<char> &buffer);
    void parse_message(std::string_view data, std::vector<char> &buffer);
    void parse_channels(std::string_view data, std::vector<char> &buffer);


};


}
