#include "check.h"

#include "../common/mq_bridge.h"
#include "../common/basic_mq.h"
#include "../common/console_mq_bridge.h"
#include <ext/stdio_filebuf.h>
#include <future>
#include <fstream>
#include <thread>


using namespace quarkbot;


class PipeStream {
public:
    PipeStream() {
        if (pipe(pipe_fds) == -1) {
            perror("pipe");
            throw std::runtime_error("Failed to create pipe");
        }

        read_buf.emplace(pipe_fds[0], std::ios::in);
        write_buf.emplace(pipe_fds[1], std::ios::out);
        read_stream.emplace(&(*read_buf));
        write_stream.emplace(&(*write_buf));

    }

    PipeStream(const PipeStream&) = delete;
    PipeStream& operator=(const PipeStream&) = delete;


    ~PipeStream() {
        close(pipe_fds[0]);
        if (pipe_fds[1]) close(pipe_fds[1]);
    }

    std::istream& get_read_end() {
        return *read_stream;
    }

    std::ostream& get_write_end() {
        return *write_stream;
    }

    void close_output() {
        close(pipe_fds[1]);
        pipe_fds[1] = 0;
    }

private:
    int pipe_fds[2] = {};
    std::optional<__gnu_cxx::stdio_filebuf<char> > read_buf;
    std::optional<__gnu_cxx::stdio_filebuf<char> > write_buf;
    std::optional<std::istream> read_stream;
    std::optional<std::ostream> write_stream;
};


void localBroker() {
    auto broker = BasicMQ::create();
    bool r1 = false;
    bool r2 = false;
    bool r3 = false;
    bool rd = false;
    constexpr std::string_view channel_name = "test";
    constexpr std::string_view message = "msg";

    auto client1 = MQClient::create(broker,[&](auto &,const MQBroker::Message &msg, bool ){
        CHECK_EQUAL(msg.get_channel(), channel_name);
        CHECK_EQUAL(msg.get_content(), message);
        r1 = true;
    });
    auto client2 = MQClient::create(broker,[&](auto &,const MQBroker::Message &msg, bool ){
        CHECK_EQUAL(msg.get_channel(), channel_name);
        CHECK_EQUAL(msg.get_content(), message);
        r2 = true;
    });
    auto client3 = MQClient::create(broker,[&](auto &,const MQBroker::Message &msg, bool ){
        CHECK_EQUAL(msg.get_channel(), channel_name);
        CHECK_EQUAL(msg.get_content(), message);
        r3 = true;
    });
    auto clientd = MQClient::create(broker,[&](auto &client, const MQBroker::Message &msg, bool ){
        CHECK_EQUAL(msg.get_channel(), channel_name);
        CHECK_EQUAL(msg.get_content(), message);
        rd = true;
        client.unsubscribe(channel_name);
    });
    client1.subscribe(channel_name);
    clientd.subscribe(channel_name);
    client2.subscribe(channel_name);
    client3.subscribe(channel_name);
    broker.send_message(nullptr, channel_name, message);
    CHECK(r1);
    CHECK(r2);
    CHECK(r3);
    CHECK(rd);
    r1 = false;
    r2 = false;
    r3 = false;
    rd = false;
    broker.send_message(nullptr, channel_name, message);
    CHECK(r1);
    CHECK(r2);
    CHECK(r3);
    CHECK(!rd);
}

void localClientServer() {

    auto broker = BasicMQ::create();
    std::string result;

    auto server = MQClient::make_unique(broker, [&](MQClient &c, const MQBroker::Message &msg){
        std::string s ( msg.get_content());
        std::reverse(s.begin(), s.end());
        c.send_message(msg.get_sender(), s);
    });
    auto client = MQClient::make_unique(broker, [&](MQClient &, const MQBroker::Message &msg){
        result = std::string(msg.get_content());
    });

    server->subscribe("reverse");
    client->send_message("reverse", "ahoj svete");
    CHECK_EQUAL(result, "etevs joha");
}

void remoteClientServer () {


    auto master = BasicMQ::create();
    auto slave1 = BasicMQ::create();
    auto slave2 = BasicMQ::create();
    MQDirectBridge b1(slave1, master);
    MQDirectBridge b2(slave2, master);

    std::promise<std::string> result;


    auto sn = MQClient::create(slave1, [&](MQClient &c, const MQBroker::Message &msg){
        std::string s ( msg.get_content());
        std::reverse(s.begin(), s.end());
        c.send_message(msg.get_sender(), s);
    });
    auto cn= MQClient::create(slave2, [&](MQClient &, const MQBroker::Message &msg){
        result.set_value(std::string(msg.get_content()));
    });

    sn.subscribe("reverse");
    cn.send_message("reverse", "ahoj svete");
    auto r = result.get_future().get();
    CHECK_EQUAL(r, "etevs joha");

}


void iostreamClientServer () {

    std::jthread thrs[4];

    PipeStream dir1;
    PipeStream dir2;
    PipeStream dir3;
    PipeStream dir4;

    auto master = BasicMQ::create();
    auto slave1 = BasicMQ::create();
    auto slave2 = BasicMQ::create();

    ConsoleMQBridge br1(slave1,dir1.get_write_end());
    ConsoleMQBridge br2(master,dir2.get_write_end());
    ConsoleMQBridge br3(master,dir3.get_write_end());
    ConsoleMQBridge br4(slave2,dir4.get_write_end());

    thrs[0] = std::jthread([&]{br1.run(dir2.get_read_end());});
    thrs[1] = std::jthread([&]{br2.run(dir1.get_read_end());});
    thrs[2] = std::jthread([&]{br3.run(dir4.get_read_end());});
    thrs[3] = std::jthread([&]{br4.run(dir3.get_read_end());});

    std::promise<std::string> result;

    auto sn = MQClient::create(slave1, [&](MQClient &c, const MQBroker::Message &msg){
        std::string s ( msg.get_content());
        std::reverse(s.begin(), s.end());
        c.send_message(msg.get_sender(), s, msg.get_conversation());
    });
    auto sn2 = MQClient::create(slave1, [&](MQClient &c, const MQBroker::Message &msg){
        std::string s ( msg.get_content());
        s.push_back('x');
        c.send_message(msg.get_sender(), s, msg.get_conversation());
    });
    auto cn= MQClient::create(master, [&](MQClient &c, const MQBroker::Message &msg){
        if (msg.get_conversation() == 0) {
            c.send_message("addx", msg.get_content(), 1);
        } else {
            result.set_value(std::string(msg.get_content()));
        }
    });

    sn.subscribe("reverse");
    sn2.subscribe("addx");


    int cnt = 0;
    while (!slave2.is_channel("reverse") && cnt < 100) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));    //wait until route is propagated
        ++cnt;
    }
    CHECK_LESS(cnt,100);   //must not take too long

    cn.send_message("reverse", "ahoj svete");
    auto r = result.get_future().get();
    CHECK_EQUAL(r, "etevs johax");

}


int main() {
    localBroker();
    localClientServer();
    remoteClientServer();
    iostreamClientServer();

}
