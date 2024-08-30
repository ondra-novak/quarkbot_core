#include <iostream>
#include <cstdlib>

#include "../common/scheduler.h"

template class quarkbot::Scheduler<void (*)() >;
template class quarkbot::SchedulerRealTime<void (*)() >;

int main(int argc, char **argv) {
    return 0;
}



