#pragma  once

#include <concepts>
#include <string_view>
#include <type_traits>
namespace quarkbot {

struct StreamTypeItem {
    using Type = std::string_view;
};

///base class for all MarketInstrument events 
struct MarketInstrumentStreamTypeItem: StreamTypeItem {};
///base class for all TradableInstrument events
struct TradableInstrumentStreamTypeItem: StreamTypeItem {};


template<typename T>
concept is_copy_assignable_type = std::is_copy_assignable_v<T>;


template<typename T, typename Base = StreamTypeItem>
concept StreamType = (std::same_as<Base,StreamTypeItem> || std::derived_from<Base, StreamTypeItem>)
          && std::derived_from<T, Base> 
&& requires(T v) {
    {T::type}->std::convertible_to<typename T::Type>;    
    {v.view()} -> is_copy_assignable_type;
};


struct StreamParams {
};

inline constexpr StreamParams emptyStreamParams = {};


template<typename T, typename U>
concept DecayIsDeriverFrom = std::is_base_of_v<U, std::decay_t<T> >;
template<typename T>
concept HasStreamParams = StreamType<T> && requires {
    {T::params} -> DecayIsDeriverFrom<StreamParams>;
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
