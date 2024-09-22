#include <quarkbot/shared/cluster_alloc.h>
#include <list>

std::list<int, ClusterAlloc<int> > intlist = {};


int main () {
    for (int i = 1; i < 1000; ++i) {
        intlist.push_back(i);
    }
    while (!intlist.empty()) {
        intlist.pop_front();
    }
}
