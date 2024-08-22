#pragma once

#include <vector>
#include <functional>

///Merges multiple ordered streams (such a timeline, scheduler etc)
/**
 * @tparam Stream stream, each stream returns pointer to comparable type, if EOF reaches
 * it returns nullptr. You can also use std::optional<> instead pointer
 * @tparam Cmp compare operator. It should compare result returned by stream (not pointer)
 *
 * The object acts as function, which returns values of same type as stream
 */
template<std::invocable<> Stream,
         typename Cmp = std::greater<std::remove_reference_t<decltype(*(std::declval<Stream>()()))> > >
class StreamMerge {
public:
    using T = std::remove_reference_t<decltype(*(std::declval<Stream>()()))>;
    using PtrT = std::remove_reference_t<decltype(std::declval<Stream>()())>;
    static constexpr PtrT nullval = {};
    StreamMerge(std::vector<Stream> streams):_streams(std::move(streams)) {
        prepare();
    }
    PtrT operator()() {
        std::pop_heap(_items.begin(),_items.end(), _cmp);
        PtrT v = (*_next_source)();
        if (v) {
            _items.back().source = _next_source;
            _items.back().val = std::move(v);
            std::push_heap(_items.begin(),_items.end(), _cmp);
        } else {
            _items.pop_back();
        }
        if (_items.empty()) {
            return nullval;
        }
        _next_source = _items.front().source;
        return _items.front().val;
    }
    StreamMerge(StreamMerge &&other) = default;
    StreamMerge &operator=(StreamMerge &&other) = default;
    StreamMerge(const StreamMerge &other) = delete;
    StreamMerge &operator=(const StreamMerge &other) = delete;

protected:
    struct Item { // @suppress("Miss copy constructor or assignment operator")
        PtrT val = nullval;
        Stream *source = nullptr;
    };
    struct CmpOp {
        Cmp _cmp = {};
        bool operator()(const Item &a, const Item &b) const {
            return _cmp(*a.val, *b.val);
        }
    };
    std::vector<Stream> _streams;
    std::vector<Item> _items;
    CmpOp _cmp;
    Stream *_next_source;


    void prepare() {
        _items.reserve(_streams.size());
        _items.push_back({});
        auto iter = _streams.begin();
        ++iter;
        auto end = _streams.end();
        while (iter != end) {
            auto v = (*iter)();
            if (v != nullval)  {
                _items.push_back(Item{v, &(*iter)});
                std::push_heap(_items.begin()+1, _items.end(), _cmp);
            }
            ++iter;
        }
        _next_source = &_streams[0];
    }

};
