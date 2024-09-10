#pragma once

#include <quarkbot/timer.h>

namespace quarkbot {


//controlled
class IControlledEntity {
public:
    virtual ~IControlledEntity() = default;
    ///called on scheduled event
    virtual void on_scheduled(Timestamp tp) noexcept= 0;
    ///query whether entity is stopped
    virtual bool is_stopped() const noexcept = 0;
    ///request to stop entity
    virtual void request_stop() noexcept = 0;
};

class IControl {
public:


    virtual ~IControl() = default;
    ///attach controlled entity to control object
    virtual void attach(IControlledEntity *ent) = 0;
    ///schedule associated entity to given timestamp
    virtual void schedule(Timestamp tp) = 0;
    ///notify that entity exited normally
    virtual void notify_exit() = 0;
    ///notify that entity exited on fatal error (error is carried as an exception)
    virtual void notify_fail() = 0;

};

using PControl = std::unique_ptr<IControl>;


}
