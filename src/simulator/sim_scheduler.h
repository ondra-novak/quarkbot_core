#pragma once

#include "../quarkbot/function.h"
#include "../common/dispatcher.h"
#include "../common/common.h"
#include "../common/icontrol.h"
#include <chrono>

namespace quarkbot {


class SimScheduler {
public:

    enum class Type {
        exchange,
        strategy
    };

    class Control: public IControl {
    public:
        Control(SimScheduler &owner):_owner(owner) {}

        bool is_stopped() const noexcept {
            return _ent==nullptr || _stopped;
        }
        void request_stop() noexcept {
            if (_stop_requested) return;
            if (_ent) _ent->request_stop();
            _stop_requested = true;
        }
        virtual void attach(IControlledEntity *ent) {
            _ent = ent;
        }
        virtual Type get_type() const = 0;

    protected:
        SimScheduler &_owner;
        IControlledEntity * _ent = {};
        bool _stopped = false;
        bool _stop_requested = false;
    };


    class ExchangeControl: public Control {
    public:

        using Control::Control;

        virtual void schedule(quarkbot::Timestamp tp) override {
            _owner._dispatcher.post_timed(this, tp, [this](auto tp){
                _ent->on_scheduled(tp);
            });
        }
        virtual void notify_exit() override {
            _owner.exchange_exit();
        }
        virtual void notify_fail() override {
            _owner.exchange_fail();
        }
        virtual Type get_type() const override {return Type::exchange;}
    };


    class StrategyControl: public Control {
    public:
        using Control::Control;


        virtual void schedule(quarkbot::Timestamp tp) override {
            if (this == _owner._dispatcher.get_executing_ident()) {
                auto exec_time = std::chrono::system_clock::now() - _owner._exec_start;
                tp = std::max(tp, _owner._tp + exec_time);
            }
            _owner._dispatcher.post_timed(this, tp,[this](auto tp){
                _ent->on_scheduled(tp);
            });
        }
        virtual void notify_exit() override {
            _owner.strategy_exit();
        }
        virtual void notify_fail() override {
            _owner.strategy_fail();
        }
        virtual Type get_type() const override {return Type::strategy;}
    };

    std::unique_ptr<Control> new_control_for_exchange() {
        return std::make_unique<ExchangeControl>(*this);
    }

    std::unique_ptr<Control> new_control_for_strategy() {
        return std::make_unique<StrategyControl>(*this);
    }

    void add(std::unique_ptr<Control> c) {
        switch (c->get_type()) {
            case Type::exchange: _exchanges.push_back(std::move(c));break;
            case Type::strategy: _strategies.push_back(std::move(c));break;
        }
    }



    bool is_next() const {
        return !_can_exit_now && !_dispatcher.empty();
    }

    auto get_next_time() const {
        return _dispatcher.get_nearest_schedule();
    }

    bool go_next() {
        if (is_next()) {
            _tp = std::max(_tp,_dispatcher.get_nearest_schedule());
            _dispatcher.process_message(_tp, [&](auto &&fn){
                _exec_start = std::chrono::system_clock::now();
                fn(_tp);
                return true;
            });
            return true;
        }
        return false;
    }

    void exchange_exit() {
        request_stop();
    }
    void exchange_fail() {
        if (!_stored_exception) _stored_exception = std::current_exception();
        request_stop();
    }
    void strategy_exit() {}
    void strategy_fail() {
        if (!_stored_exception) _stored_exception = std::current_exception();
        request_stop();
    }

    void request_stop() {
        for (auto &x: _exchanges) {x->request_stop();}
        for (auto &x: _exchanges) if (!x->is_stopped()) return ;
        for (auto &x: _strategies) {x->request_stop();}
        for (auto &x: _strategies) if (!x->is_stopped()) return ;
        _can_exit_now = true;
    }


protected:

    Timestamp _tp = {};
    Timestamp _exec_start ={};
    DispatcherCore<Function<void(Timestamp)> > _dispatcher;
    std::vector<std::unique_ptr<Control> > _exchanges;
    std::vector<std::unique_ptr<Control> > _strategies;
    std::exception_ptr _stored_exception = {};
    bool _can_exit_now = false;
    bool _stop_requested =false;




};


}
