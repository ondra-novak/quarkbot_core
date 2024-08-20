#pragma once
#include "instrument.h"
#include "function.h"
#include "market_event.h"
#include "wrapper.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace trading_api {

class Instrument;
struct TickData;
class OrderBook;


class IExchangeInfo {
public:

    struct Icon {
        ///icon data (binary)
        std::string_view data;
        ///content type - for example image/png, image/jpeg, etc
        std::string_view content_type;
    };


    virtual ~IExchangeInfo() = default;
    virtual std::string get_id() const = 0;
    virtual std::string get_label() const = 0;
    virtual std::string get_name() const = 0;
    virtual std::optional<Icon> get_icon() const = 0;
    class Null;
};


class IExchangeInfo::Null: public IExchangeInfo {
public:
    virtual std::string get_label() const override {return {};}
    virtual std::string get_name() const override  {return {};}
    virtual std::string get_id() const override  {return {};}
    virtual std::optional<Icon> get_icon() const override {return {};}
};


///Information about exchange
/**
 * Contains various informations about exchange
 * You can use this instance to retrieve connection between account
 * and instrument.
 *
 * You can only trade account and instrument at the same exchange
 *
 *
 */
class ExchangeInfo: public Wrapper<IExchangeInfo> {
public:

    using Icon = IExchangeInfo::Icon;

    using Wrapper<IExchangeInfo>::Wrapper;


    ///Retrieve user defined label (configured for this exchange)
    std::string get_label() const {return _ptr->get_label();}
    ///Retrieve name of exchange
    std::string get_name() const {return _ptr->get_name();}
    ///Retrieves internal ID of the exchange (can be empty)
    std::string get_id() const  {return _ptr->get_id();}
    ///Retrieves icon. This feature is optional.
    std::optional<Icon> get_icon() const {return _ptr->get_icon();}



};


}

