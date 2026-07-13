#pragma once

#include <cassert>
#include <memory>
#include <new>
#include <vector>
template<typename T>
class ArenaAllocatorInstance {
public:
    static constexpr std::size_t block_size = 4096;
    static constexpr std::size_t item_size = sizeof(T);
    static constexpr std::size_t block_item_count = std::max<std::size_t>(block_size/item_size,1);
    

    union alignas(T)  Item {
        char data[item_size];
        Item *next_free;
    };
    
    struct Block {        
        Item items[block_item_count];
    };

    T *allocate() {
        if (first_free ) {
            Item &itm = *first_free;
            first_free = itm.next_free;
            return reinterpret_cast<T *>(itm.data);
        } else {
            if (first_unused >= _blocks.size() * block_item_count){
                _blocks.push_back(std::make_unique<Block>());                
            }
            Item &itm = _blocks[first_unused/block_item_count]->items[first_unused%block_item_count];
            ++first_unused;
            return reinterpret_cast<T *>(itm.data);
        }
    }

    void deallocate(T *x) {
        Item *itm = reinterpret_cast<Item *>(x);
        itm->next_free = first_free;
        first_free = itm;
    }

protected:
    std::vector<std::unique_ptr<Block> > _blocks;
    Item *first_free = nullptr;
    std::size_t first_unused = 0;

};


template<typename X>
class ArenaAllocatorUnsynchronized {
public:
    using value_type = X;        

    ArenaAllocatorUnsynchronized() = default;
    template<typename Y>
    ArenaAllocatorUnsynchronized(ArenaAllocatorUnsynchronized<Y>) {}
    constexpr bool operator==(const ArenaAllocatorUnsynchronized &) const {return true;}

    static ArenaAllocatorInstance<X> instance;

    X *allocate(std::size_t n) {
        if (n != 1) return reinterpret_cast<X *>(::operator new(sizeof(X) * n));
        return instance.allocate();
    }
    void deallocate(X *p, [[maybe_unused]]std::size_t n) {
        if (n != 1) return ::operator delete(p);
        instance.deallocate(p);
    }
};

template<typename X>
inline ArenaAllocatorInstance<X> ArenaAllocatorUnsynchronized<X>::instance = {};

