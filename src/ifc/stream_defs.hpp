#pragma  once

#include <concepts>
#include <string_view>
#include <type_traits>
namespace quarkbot {

struct StreamTypeItem {
    using Type = std::string_view;
};

template<typename T>
concept StreamType = std::derived_from<T, StreamTypeItem> && requires {
    {T::type}->std::convertible_to<typename T::Type>;    
};


struct StreamParams {
};

inline constexpr StreamParams emptyStreamParams = {};

struct MarketStreamTypeItem :StreamTypeItem {};
struct InstrumentStreamTypeItem :StreamTypeItem{};


template<typename T>
concept HasStreamParams = StreamType<T> && requires {
    {T::params}->std::derived_from<StreamParams>;
};



template<typename T>
struct ExtractStreamParams {
    static constexpr const StreamParams *value = &emptyStreamParams;
};

template<HasStreamParams T>
struct ExtractStreamParams<T> {
    static constexpr const StreamParams *value = &T::params;
};

template<StreamType T>
constexpr const StreamParams *stream_params = ExtractStreamParams<T>::value;




}
