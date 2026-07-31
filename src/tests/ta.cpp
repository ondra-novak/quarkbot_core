#include "check.h"
#include "quarkbot/common/mem_storage.hpp"
#include "quarkbot/decimal.hpp"
#include "quarkbot/serie_persistent.hpp"
#include "quarkbot/serie_memory.hpp"
#include "quarkbot/storage.hpp"
#include "quarkbot/ta/sma.hpp"
#include "quarkbot/ta/ema.hpp"
#include "quarkbot/ta/dema.hpp"
#include "quarkbot/ta/bollinger.hpp"
#include "quarkbot/ta/macd.hpp"
#include "quarkbot/ta/wma.hpp"


using namespace quarkbot;

void test_sma() {

    ta::SMA<MemorySerie<Decimal> > sma({}, 5);
    Decimal results[] = {0,0.5_dec, 1, 1.5_dec,2,3,4,5,6,7};

    for (int i = 0; i < 10; ++i) {
        auto out = sma.update(i);
        CHECK_EQUAL(out, results[i]) ;
    }
}

//same SMA, but backed by a persistent (Storage) serie - must produce identical results
void test_sma_persistent() {
    Storage stor (MemStorage<>::create());

    ta::SMA<PersistentSerie<Decimal> > sma({stor, "sma"}, 5);
    Decimal results[] = {0,0.5_dec, 1, 1.5_dec,2,3,4,5,6,7};

    for (int i = 0; i < 10; ++i) {
        auto out = sma.update(i);
        CHECK_EQUAL(out, results[i]) ;
    }
}

void test_ema() {
    //test with storage
    Storage stor (MemStorage<>::create());

    ta::EMA<PersistentSerie<Decimal> > ema({stor, "key"}, 4);
    Decimal results[] = {0,0.4_dec, 1.04_dec, 1.824_dec,2.6944_dec,
        3.61664_dec,4.569984_dec,5.5419904_dec,6.52519424_dec,7.515116544_dec};

    for (int i = 0; i < 10; ++i) {
        auto out = ema.update(i);
        CHECK_EQUAL(out, results[i]) ;
    }
}

//same EMA, but backed by an in-memory serie - must produce identical results
void test_ema_memory() {
    ta::EMA<MemorySerie<Decimal> > ema({}, 4);
    Decimal results[] = {0,0.4_dec, 1.04_dec, 1.824_dec,2.6944_dec,
        3.61664_dec,4.569984_dec,5.5419904_dec,6.52519424_dec,7.515116544_dec};

    for (int i = 0; i < 10; ++i) {
        auto out = ema.update(i);
        CHECK_EQUAL(out, results[i]) ;
    }
}

void test_bb() {

    ta::BollingerBands<MemorySerie<Decimal> > bb({}, 5,5);
    Decimal results[] = {0,0.5_dec, 1, 1.5_dec,2,3,4,5,6,7};
    Decimal dev_results[] = {0,
        0.5_dec,
        sqrt(5_dec/3_dec - 1),
        sqrt(14_dec/4_dec - 1.5_dec*1.5_dec),
        sqrt(30_dec/5_dec - 2_dec*2_dec),
        sqrt(55_dec/5_dec - 3_dec*3_dec),
        sqrt(90_dec/5_dec - 4_dec*4_dec),
        sqrt(135_dec/5_dec - 5_dec*5_dec),
        sqrt(190/5_dec - 6_dec*6_dec),
        sqrt(255/5_dec - 7_dec*7_dec)};

    for (int i = 0; i < 10; ++i) {
        auto out = bb.update(i);
        CHECK_EQUAL(out.mean, results[i]) ;
        CHECK_EQUAL(out.dev, dev_results[i]) ;
    }
}

//Weighted Moving Average, interval 3. The results are exact rational numbers
//(integer numerator / integer divisor) so they can be compared exactly against
//the same Decimal division performed by the indicator.
void test_wma() {
    //linear weights: most recent value has weight 3, oldest weight 1
    //warmup divisors: 1, 3, 6; steady divisor: 3*4/2 = 6
    Decimal results[] = {
        1_dec/1_dec,        // [1]
        5_dec/3_dec,        // [1,2]
        14_dec/6_dec,       // [1,2,3]
        20_dec/6_dec,       // [2,3,4]
        26_dec/6_dec,       // [3,4,5]
        32_dec/6_dec,       // [4,5,6]
        38_dec/6_dec,       // [5,6,7]
        44_dec/6_dec,       // [6,7,8]
        50_dec/6_dec,       // [7,8,9]
        56_dec/6_dec,       // [8,9,10]
    };

    ta::WMA<MemorySerie<Decimal> > wma({}, 3);
    for (int i = 0; i < 10; ++i) {
        CHECK_EQUAL(wma.update(i+1), results[i]);
    }

    //identical behaviour on a persistent serie
    Storage stor (MemStorage<>::create());
    ta::WMA<PersistentSerie<Decimal> > wma2({stor, "wma"}, 3);
    for (int i = 0; i < 10; ++i) {
        CHECK_EQUAL(wma2.update(i+1), results[i]);
    }
}

//DEMA = 2*EMA(x) - EMA(EMA(x)). Reference built from the (already tested) EMA class
//so the wiring of the double-exponential formula is verified exactly.
void test_dema() {
    ta::DEMA<MemorySerie<Decimal> > dema({}, 4);
    ta::EMA<MemorySerie<Decimal> > e1({}, 4);
    ta::EMA<MemorySerie<Decimal> > e2({}, 4);

    for (int i = 0; i < 20; ++i) {
        Decimal a = e1.update(i);
        Decimal b = e2.update(a);
        Decimal expected = a + a - b;
        CHECK_EQUAL(dema.update(i), expected);
    }
}

//MACD: macd line = EMA(fast) - EMA(slow); signal = EMA(macd line).
//Reference built from three independent EMA instances.
void test_macd() {
    ta::MACD<MemorySerie<Decimal> > macd({}, 26, 12, 9);
    ta::EMA<MemorySerie<Decimal> > slow({}, 26);
    ta::EMA<MemorySerie<Decimal> > fast({}, 12);
    ta::EMA<MemorySerie<Decimal> > sig({}, 9);

    for (int i = 0; i < 50; ++i) {
        Decimal f = fast.update(i);
        Decimal s = slow.update(i);
        Decimal m = f - s;
        Decimal expected_signal = sig.update(m);

        auto out = macd.update(i);
        CHECK_EQUAL(out.macd, m);
        CHECK_EQUAL(out.signal, expected_signal);
    }
}

//warmup flags: operator bool must be false during warmup and true once the
//window is full
void test_warmup_flags() {
    ta::SMA<MemorySerie<Decimal> > sma({}, 5);
    ta::WMA<MemorySerie<Decimal> > wma({}, 3);
    ta::EMA<MemorySerie<Decimal> > ema({}, 4);

    CHECK(!static_cast<bool>(sma));
    CHECK(!static_cast<bool>(wma));
    CHECK(!static_cast<bool>(ema));

    for (int i = 0; i < 4; ++i) sma.update(i);
    CHECK(!static_cast<bool>(sma));     //still warming (4 < 5)
    sma.update(4);
    CHECK(static_cast<bool>(sma));      //full window

    for (int i = 0; i < 2; ++i) wma.update(i);
    CHECK(!static_cast<bool>(wma));     //still warming (2 < 3)
    wma.update(2);
    CHECK(static_cast<bool>(wma));      //full window

    ema.update(0);
    CHECK(static_cast<bool>(ema));      //EMA is "ready" after first value
}

//warm restart: an indicator built on top of a persistent serie that already
//holds values must resume from the stored state instead of starting cold.
void test_warm_restart() {
    Storage stor (MemStorage<>::create());

    //--- SMA ---
    {
        ta::SMA<PersistentSerie<Decimal> > sma({stor, "sk"}, 5);
        for (int i = 0; i < 10; ++i) sma.update(i);     //last window: 5,6,7,8,9
    }
    ta::SMA<PersistentSerie<Decimal> > sma2({stor, "sk"}, 5);
    CHECK(static_cast<bool>(sma2));                     //restored as full window
    CHECK_EQUAL(sma2.update(10), 8_dec);                //avg of 6,7,8,9,10

    //--- WMA (this path was broken: it read a moved-from serie) ---
    {
        ta::WMA<PersistentSerie<Decimal> > wma({stor, "wk"}, 3);
        for (int i = 1; i <= 10; ++i) wma.update(i);    //last window: 8,9,10
    }
    ta::WMA<PersistentSerie<Decimal> > wma2({stor, "wk"}, 3);
    CHECK(static_cast<bool>(wma2));                     //restored as full window
    //continue with 11: window 9,10,11 -> (9*1+10*2+11*3)/6 = 62/6
    CHECK_EQUAL(wma2.update(11), 62_dec/6_dec);
}


int main() {
    test_sma();
    test_sma_persistent();
    test_ema();
    test_ema_memory();
    test_bb();
    test_wma();
    test_dema();
    test_macd();
    test_warmup_flags();
    test_warm_restart();

}
