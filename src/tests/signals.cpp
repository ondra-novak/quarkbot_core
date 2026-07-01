#include <quarkbot/utils/signals.hpp>
#include "check.h"
#include <future>

using namespace signals;





int test1() {

     auto slot1 = SharedSignalSlot<void(int)>::create();

    int ret = 1;

    auto con = slot1 >> [&](int v) noexcept {
        ret = v;
    };

    slot1(0);
    CHECK_EQUAL(ret,0);
    
    return ret;
}


int test2() {
    auto slot1 = SharedSignalSlot<void(int)>::create();
    auto slot2 = SharedSignalSlot<void(int)>::create();
    int sum = 0;

    auto con = slot1 >> [&](int v) noexcept {
        sum+=v;
    };
    slot2 >> con;


    slot1(10);
    slot2(20);
    CHECK_EQUAL(sum,30);
    return 0;

}

int test3() {
    int a = 0;
    int b = 0;
    auto slot1 = SharedSignalSlot<void(int)>::create();    
    auto con1 = slot1 >> [&](int v) noexcept {
        a = v;
    };
    auto con2 = slot1 >> [&](int v) noexcept {
        b = v;
    };

    slot1(10);
    CHECK_EQUAL(a, 10);
    CHECK_EQUAL(b,10);


    return 0;

}

int test4() {
    int a = 0;
    int b = 0;
    auto slot1 = SharedSignalSlot<void(int)>::create();    
    auto slot2 = SharedSignalSlot<void(int)>::create();    
    auto con0 = slot2 >> [&](int v) noexcept {
        a = v;
    };

    auto con1 = slot1 >> [&](int v) noexcept {
        slot2(v+12);
    };
    auto con2 = slot1 >> [&](int v) noexcept {
        b = v;
    };

    slot1(10);
    CHECK_EQUAL(a, 22);
    CHECK_EQUAL(b,10);


    return 0;

}

int test5() {
    std::vector<int> q;
    auto slot1 = SharedSignalSlot<void()>::create();
    auto con1 = connect(slot1, [&]() noexcept {
        q.push_back(10);
    },10);
    auto con2 = connect(slot1, [&]() noexcept {
        q.push_back(5);
    },5);
    auto con3 = connect(slot1, [&]() noexcept {
        q.push_back(8);
    },8);
    auto con4 = connect(slot1, [&]() noexcept {
        q.push_back(12);
    },12);

    slot1();

    CHECK_EQUAL(q[0] ,12);
    CHECK_EQUAL(q[1] ,10);
    CHECK_EQUAL(q[2] ,8);
    CHECK_EQUAL(q[3] ,5);

    return 0;
}

int test6() {
    auto slot1 = SharedSignalSlot<void(int a)>::create();

    std::vector<int> q = {0,0,0,0};
    std::vector<SharedSignalSlot<void(int a)>::Connection>  conns;
    conns.push_back(connect(slot1, [&](int a) noexcept {
        q[0] = a;
        if (a == 10) conns[0].reset();

    }));
    conns.push_back(connect(slot1, [&](int a) noexcept {
        q[1] = a;
        if (a == 20) conns[1].reset();
    }));
    conns.push_back(connect(slot1, [&](int a) noexcept {
        q[2] = a;
        if (a == 30) conns[2].reset();
    }));
    conns.push_back(connect(slot1, [&](int a) noexcept {
        q[3] = a;
        if (a == 40) conns[3].reset();
    }));

    slot1(5);

    CHECK_EQUAL(q[0] ,5);
    CHECK_EQUAL(q[1] ,5);
    CHECK_EQUAL(q[2] ,5);
    CHECK_EQUAL(q[3] ,5);

    slot1(10);

    CHECK_EQUAL(q[0] ,10);
    CHECK_EQUAL(q[1] ,10);
    CHECK_EQUAL(q[2] ,10);
    CHECK_EQUAL(q[3] ,10);

    slot1(20);

    CHECK_EQUAL(q[0] ,10);
    CHECK_EQUAL(q[1] ,20);
    CHECK_EQUAL(q[2] ,20);
    CHECK_EQUAL(q[3] ,20);

    slot1(30);

    CHECK_EQUAL(q[0] ,10);
    CHECK_EQUAL(q[1] ,20);
    CHECK_EQUAL(q[2] ,30);
    CHECK_EQUAL(q[3] ,30);

    slot1(40);

    CHECK_EQUAL(q[0] ,10);
    CHECK_EQUAL(q[1] ,20);
    CHECK_EQUAL(q[2] ,30);
    CHECK_EQUAL(q[3] ,40);

    slot1(50);

    CHECK_EQUAL(q[0] ,10);
    CHECK_EQUAL(q[1] ,20);
    CHECK_EQUAL(q[2] ,30);
    CHECK_EQUAL(q[3] ,40);

    return 0;
}

int test7() {
    int a = 0;
    int b = 0;
    auto slot1 = SharedSignalSlot<void(int)>::create();    
    auto con1 = connect(slot1, [&](int v) noexcept {
        a = v;
    },0,ConnectionMode::one_shot);
    auto con2 = connect(slot1, [&](int v) noexcept {
        b = v;
    },0, ConnectionMode::normal);

    slot1(10);
    CHECK_EQUAL(a,10);
    CHECK_EQUAL(b,10);

    slot1(20);
    CHECK_EQUAL(a,10);
    CHECK_EQUAL(b,20);

    connect(slot1, con1,0, ConnectionMode::one_shot);

    slot1(30);
    CHECK_EQUAL(a,30);
    CHECK_EQUAL(b,30);

    return 0;

}
/*
int test_async_1() {
    auto slot = SharedSignalSlot<void(int), AsyncSignalDispatcher<> >::create();

    std::promise<int> res;
    auto fut =res.get_future();


    auto con = slot >> [&](int k) noexcept {res.set_value(k);};

    slot(42);

    int r = fut.get();
    CHECK_EQUAL(r, 42);
 
    return 0;

}




int test_async_2() {
    auto disp = AsyncSignalDispatcher<0>::create();
    auto slot = SharedSignalSlot<void(int), AsyncSignalDispatcher<0> >::create(disp);

    std::promise<int> res;
    auto fut =res.get_future();


    auto con = slot >> [&](int k) noexcept {res.set_value(k);};

    slot(42);

    auto wr = fut.wait_for(std::chrono::seconds(0));
    
    CHECK(wr == std::future_status::timeout);

    disp.pump_one();

    int r = fut.get();
    CHECK_EQUAL(r, 42);
 
    return 0;

}
*/
int main() {
    return test1() + test2() + test3() 
        + test4() + test5() + test6() + test7();
        /*+ test_async_1() + test_async_2();*/
}