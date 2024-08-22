#include "../common/stream_merge.h"
#include "check.h"
#include <optional>

template<typename T>
class DataSource {
public:
    DataSource(std::vector<T> x):_data(std::move(x)) {}
    std::optional<int> operator()() {
        if (_pos == _data.size()) return {};
        else return _data[_pos++];
    }
protected:
    std::size_t _pos = 0;
    std::vector<T> _data;

};

template<typename T>
class DataSourcePtr {
public:
    DataSourcePtr(std::vector<T> x):_data(std::move(x)) {}
    const int * operator()() {
        if (_pos == _data.size()) return {};
        else return &_data[_pos++];
    }
protected:
    std::size_t _pos = 0;
    std::vector<T> _data;

};




int test1() {
    DataSource<int> s1({1,5,8,10,44,89});
    DataSource<int> s2({3,5,9,15,21,55});
    DataSource<int> s3({4,11,32,90,95});
    std::vector<int> result = {1,3,4,5,5,8,9,10,11,15,21,32,44,55,89,90,95};
    StreamMerge<DataSource<int> > tst({std::move(s1), std::move(s2), std::move(s3)});
    auto iter = result.begin();
    auto x = tst();
    while (x) {
        CHECK_EQUAL(*x, *iter);
        ++iter;
        x = tst();
    }
    CHECK(iter == result.end());
    return 0;
}

int test2() {
    DataSourcePtr<int> s1({1,5,8,10,44,89});
    DataSourcePtr<int> s2({3,5,9,15,21,55});
    DataSourcePtr<int> s3({4,11,32,90,95});
    std::vector<int> result = {1,3,4,5,5,8,9,10,11,15,21,32,44,55,89,90,95};
    StreamMerge<DataSourcePtr<int> > tst({std::move(s1), std::move(s2), std::move(s3)});
    auto iter = result.begin();
    auto x = tst();
    while (x) {
        CHECK_EQUAL(*x, *iter);
        ++iter;
        x = tst();
    }
    CHECK(iter == result.end());
    return 0;
}


int main() {
    test1();
    test2();
}
