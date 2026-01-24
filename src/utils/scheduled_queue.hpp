#include <concepts>
#include <vector>
#include <optional>
#include <algorithm>
#include <stdexcept>

template<typename T, typename U, typename V>
requires std::totally_ordered<U> && std::equality_comparable<V>
class ScheduledQueue {
public:

struct Item {
        T value;
        U time;
        V id;
    };

private:
    std::vector<Item> heap;


    // heap compare by time, then id
    static bool less(const Item& a, const Item& b) {
        if (a.time != b.time) return a.time < b.time;
        return a.id < b.id;
    }

    void swapNodes(size_t i, size_t j) {
        std::swap(heap[i], heap[j]);
    }

    void siftUp(size_t idx) {
        while (idx > 0) {
            size_t parent = (idx - 1) / 2;
            if (!less(heap[idx], heap[parent])) break;
            swapNodes(idx, parent);
            idx = parent;
        }
    }

    void siftDown(size_t idx) {
        while (true) {
            size_t left = idx * 2 + 1;
            size_t right = idx * 2 + 2;
            size_t smallest = idx;

            if (left < heap.size() && less(heap[left], heap[smallest])) smallest = left;
            if (right < heap.size() && less(heap[right], heap[smallest])) smallest = right;

            if (smallest == idx) break;
            swapNodes(idx, smallest);
            idx = smallest;
        }
    }



public:
    bool empty() const { return heap.empty(); }

    void push(T value, U time, V id) {
        heap.push_back({std::move(value), std::move(time), std::move(id)});
        size_t idx = heap.size() - 1;
        siftUp(idx);
    }

    const Item& top() const {
        if (heap.empty()) throw std::runtime_error("Queue is empty");
        return heap.front();
    }

    Item& top()  {
        if (heap.empty()) throw std::runtime_error("Queue is empty");
        return heap.front();
    }

    void pop() {
        if (heap.empty()) throw std::runtime_error("Queue is empty");


        if (heap.size() == 1) {
            heap.pop_back();
            return;
        }

        heap[0] = std::move(heap.back());
        heap.pop_back();


        siftDown(0);
    }

    std::vector<Item>::const_iterator find(const V& id) const {
        auto it = std::find_if(heap.begin(), heap.end(), [&](const auto &x){
            return x.id == id;
        });
        return it;
    }

    auto mutable_ref(std::vector<Item>::const_iterator iter) {
        return heap.begin() + std::distance(heap.cbegin(), iter);
    }

    auto begin() const {return heap.begin();}
    
    auto end() const {return heap.end();}

    bool erase(std::vector<Item>::const_iterator iter) {

        size_t idx = std::distance(heap.cbegin(), iter);

        if (idx == heap.size() - 1) {
            heap.pop_back();
            return true;
        }

        // move last element into idx
        heap[idx] = std::move(heap.back());
        heap.pop_back();


        siftUp(idx);
        siftDown(idx);

        return true;
    }
};
