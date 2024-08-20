#pragma once

#include "wrapper.h"
namespace trading_api {

class IExchangeCredentials {
public:

    virtual ~IExchangeCredentials() = default;
    virtual std::string get_label() const = 0;

    class Null;
};

class IExchangeCredentials::Null : public IExchangeCredentials{
public:
    virtual std::string get_label() const override {return {};}
};

class ExchangeCredentials : public Wrapper<IExchangeCredentials> {
public:
    using Wrapper<IExchangeCredentials>::Wrapper;

    std::string get_label() const {return _ptr->get_label();}
};



}
