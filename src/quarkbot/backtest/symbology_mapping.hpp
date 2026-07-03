#pragma once

#include "quarkbot/abstract/backtest_data_source.hpp"
#include <concepts>
#include <string>
namespace quarkbot {

///Definition of concept for symbology mapping. The mapping is a map from string to string_view. The mapping is used to convert symbols from one format to another.
/**
 * @tparam T The type of the symbology map.
    * The type must support the following operations:
    find(symbol) -> returns an iterator to the element with the given symbol, or end() if not found
    end() -> returns an iterator to the end of the map
 */
template<typename T>
concept SymbologyMap = requires(const T &v, const std::string &symbol) {
    {v.find(symbol)->second} -> std::convertible_to<std::string_view>;
    {v.find(symbol) == v.end()} -> std::convertible_to<bool>;
};

///Symbology mapping that skips events with missing symbols
/**
 * @tparam _Map The type of the symbology map.
 * @tparam _Source The type of the backtest data source.

    This is empty base class that is used to implement the symbology mapping. It is not intended to be used directly.
    Use SymbologyMapping_SkipMissing or SymbologyMapping_IgnoreMissing instead.
 */
template<SymbologyMap _Map, BacktestDataSourceType _Source>
class SymbologyMappingBase {
public:
    SymbologyMappingBase(_Map map, _Source source):_map(std::move(map)), _source(std::move(source)) {}
protected:
    _Map _map;
    _Source _source;
};

///Symbology mapping that skips events with missing symbols
/**
 * @tparam _Map The type of the symbology map.
 * @tparam _Source The type of the backtest data source.
 * The events with symbols that are not found in the mapping are skipped and not returned by the operator().
 */
template<SymbologyMap _Map, BacktestDataSourceType _Source>
class SymbologyMapping_SkipMissing : public SymbologyMappingBase<_Map, _Source>{
public:
    using SymbologyMappingBase<_Map, _Source>::SymbologyMappingBase;
    bool operator()(BacktestEvent &ev) {
        while (true) {
            if (!this->_source(ev)) return false;
            auto iter = this->_map->find(ev.symbol);
            if (iter != this->_map->end()) {
                ev.symbol.clear();
                ev.symbol.append(iter->second);
                return true;
            }
        }
    }
};

///Symbology mapping that ignores events with missing symbols
/**
 * @tparam _Map The type of the symbology map.
 * @tparam _Source The type of the backtest data source.
 * The events with symbols that are not found in the mapping are returned by the operator() with the original symbol unchanged.
 */
template<SymbologyMap _Map, BacktestDataSourceType _Source>
class SymbologyMapping_IgnoreMissing : public SymbologyMappingBase<_Map, _Source>{
public:
    using SymbologyMappingBase<_Map, _Source>::SymbologyMappingBase;
    bool operator()(BacktestEvent &ev) {
        if (!this->_source(ev)) return false;
        auto iter = this->_map.find(ev.symbol);
        if (iter != this->_map.end()) {
            ev.symbol.clear();
            ev.symbol.append(iter->second);
        }
        return true;
    }
};


}