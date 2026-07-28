#pragma once

#include "ma.hpp"
#include "quarkbot/defs.hpp"
#include "quarkbot/ta/sma.hpp"
namespace quarkbot {

namespace ta {


///Bollinger Bands generator
/**
@tparam _MA type of moving average indicator to use. The moving average must have DataPoint type and Seriel type defined. 
The moving average must have update() function which takes DataPoint
*/
template<MovingAverage _MA>
class BollingerBandsGen {
public:


    using DataPoint = typename _MA::DataPoint;
    using SerieType = typename _MA::SerieType;

    ///result of Bollinger Bands calculation
    struct Result {
        ///mean value of the series
        DataPoint mean;
        ///standard deviation of the series
        DataPoint dev;
    };


    ///Construct Bollinger Bands generator
    /**
        @param serie serie to store values. The serie must be able to store at least mean_interval and dev_interval values
        @param mean_interval number of values to average for mean
        @param dev_interval number of values to average for standard deviation
    */
    BollingerBandsGen(SerieType serie, std::size_t mean_interval, std::size_t dev_interval)
        :_ex(serie, mean_interval)
        ,_ex2(serie.clone(), dev_interval) {}


    ///Update Bollinger Bands with new value
    /**
        @param dp new value to update Bollinger Bands
        @return current Bollinger Bands result
        @note during warmup phase (when less than mean_interval or dev_interval values are available), the returned value is calculated from all available values. 
        After warmup phase, the returned value is calculated from last mean_interval and dev_interval values
    */
    Result update(const DataPoint &dp) {
        using namespace std;
        DataPoint ex = _ex.update(dp);
        DataPoint ex2 = _ex2.update(dp*dp);
        return {ex, sqrt(ex2 - ex*ex)};        
    }

     ///returns true, if returned value is accurate (returns false during warmup phase)
     explicit operator bool() const {
        return static_cast<bool>(_ex) && static_cast<bool>(_ex2);
    }    


protected:
    _MA _ex;
    _MA _ex2;
};

///Bollinger Bands generator using SMA as moving average
template<IsSerie Serie>
using BollingerBands = BollingerBandsGen<SMA<Serie> >;


}

}