#pragma once

#include <quarkbot/function.h>
#include <quarkbot/timer.h>

namespace quarkbot {


/*scheduler object acts as invocable, which receives timestamp, function to call,
 * and pointer which serves as identification.
 *
 * If function is called, and there is already scheduled action, it
 * reschedules the action to new time point
 *
 */

template<typename Scheduler>
concept SchedulerType = (std::is_invocable_v<Scheduler, Timestamp, Function<void(Timestamp)>, const void *>);

}
