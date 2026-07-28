#pragma once

#include <concepts>
namespace quarkbot {
namespace ta {


template<typename T>
concept MovingAverage = requires(T indicator, typename T::DataPoint point) {
    {indicator.update(point)} -> std::convertible_to<typename T::DataPoint>;
    {static_cast<bool>(indicator)} -> std::convertible_to<bool>;
    typename T::SerieType;
};
    
}
}