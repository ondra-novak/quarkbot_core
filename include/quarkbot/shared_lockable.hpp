#include <memory>
#include <shared_mutex>
#include <concepts>

namespace quarkbot {


class shared_lockable_ptr_common {
public:

    class SharedPtrDeleter {
    public:

        union {
            std::shared_mutex mx;
        };

        SharedPtrDeleter()  {}
        ~SharedPtrDeleter() {}

        SharedPtrDeleter(const SharedPtrDeleter &) {}

        template<typename T>
        void operator()(T *ptr) {
            if (!_fn) return;   //not initialized?
            _fn(ptr, _closure);
            std::destroy_at(&mx);
        }

        void operator()(std::nullptr_t) {
            if (!_fn) return;  //not initialized?
            _fn(nullptr, _closure);
            std::destroy_at(&mx);
        }

        template<typename T, std::invocable<T *> DeleterFn>
        void init_custom_deleter(DeleterFn &&fn) {
            using Closure = std::decay_t<DeleterFn>;
            std::construct_at(&mx);
            if constexpr(sizeof(Closure) <= sizeof(_closure)) {
                new(_closure) Closure(std::forward<DeleterFn>(fn));
                _fn = [](void *ptr, void **closure) {
                    T *p = reinterpret_cast<T *>(ptr);
                    auto delfn = reinterpret_cast<DeleterFn *>(closure);
                    (*delfn)(p);
                    std::destroy_at(delfn);
                };
            } else {
                auto fninst = new DeleterFn(std::forward<DeleterFn>(fn));
                _closure[0] = fninst;
                _fn = [](void *ptr, void **closure) {
                    T *p = reinterpret_cast<T *>(ptr);
                    auto delfn = reinterpret_cast<DeleterFn *>(closure[0]);
                    (*delfn)(p);
                    delete delfn;
                };
            }
        }

        template<typename T>
        void init_default_deleter() {
            std::construct_at(&mx);
            _fn = [](void *ptr, void **) {
                delete reinterpret_cast<T *>(ptr);
            };
        }

    protected:

        void * _closure[2] = {};
        void (*_fn)(void *ptr, void **closure) = nullptr;
    };


    template<bool shared>
    struct UniquePtrDeleter {
        std::shared_ptr<std::nullptr_t> _subject;
        template<typename T>
        void operator()(T *) {
            SharedPtrDeleter *del = std::get_deleter<SharedPtrDeleter>(this->_subject);
            if constexpr(shared) {
                del->mx.unlock_shared();
            } else {
                del->mx.unlock();
            }
        }
    };

    using SharedUniquePtrDeleter = UniquePtrDeleter<true>;
    using ExclusiveUniquePtrDeleter = UniquePtrDeleter<false>;


    template<typename T>
    static std::unique_ptr<std::add_const_t<T>, SharedUniquePtrDeleter> lock_ptr_shared(const std::shared_ptr<T> &ptr) {
        SharedPtrDeleter *dl = std::get_deleter<SharedPtrDeleter>(ptr);
        if (!dl) throw std::invalid_argument("bad shared pointer");
        dl->mx.lock_shared();
        return {ptr.get(), {{ptr, nullptr}}};
    }

    template<typename T>
    static std::unique_ptr<T, ExclusiveUniquePtrDeleter> lock_ptr_exclusive(const std::shared_ptr<T> &ptr) {
        SharedPtrDeleter *dl = std::get_deleter<SharedPtrDeleter>(ptr);
        if (!dl) throw std::invalid_argument("bad shared pointer");
        dl->mx.lock();
        return {ptr.get(), {{ptr, nullptr}}};
    }

    template<typename T>
    struct Allocator {

        Allocator(T ** space_ptr):_space_ptr(space_ptr) {}

        T ** _space_ptr;

        using value_type = std::nullptr_t;

        template<typename U>
        struct ControlAlloc {
            using value_type = U;

            ControlAlloc(const Allocator &a):_owner(a) {}
            const Allocator &_owner;

            U *allocate(std::size_t n) {
                std::size_t control = sizeof(U)*n;
                std::size_t need = control+sizeof(T);
                void *ptr =::operator new(need);
                T *private_space = reinterpret_cast<T *>(
                        reinterpret_cast<char *>(ptr)+control);
                *_owner._space_ptr = private_space;
                return reinterpret_cast<U *>(ptr);

            }
            void deallocate(U *ptr, std::size_t ) {
                ::operator delete(ptr);
            }

        };

        template<typename X>
        struct rebind {
            using other = std::conditional_t<std::is_same_v<X,std::nullptr_t>,Allocator<T>,ControlAlloc<X> >;
        };
    };

};

template<typename T>
class lockable_weak_ptr;

///shared pointer with shared/exclusive locking capabilities
/**
@tparam T type of the object pointed by the shared pointer. This is not necessarily the same as the type of the object stored in shared_ptr, but it must be compatible (pointer to derived class can be stored in shared_lockable_ptr to base class)
*/
template<typename T>
class shared_lockable_ptr: public shared_lockable_ptr_common {
private:
public:

    ///Default constructor - creates empty pointer
    shared_lockable_ptr() = default;

    ///Constructor from raw pointer - takes ownership of the pointer
    shared_lockable_ptr(T *ptr):_subject(ptr, SharedPtrDeleter{}) {
        SharedPtrDeleter *deleter = std::get_deleter<SharedPtrDeleter>(_subject);
        deleter->init_default_deleter<T>();
    }

    ///Constructor from raw pointer and custom deleter - takes ownership of the pointer
    template<typename Fn>
    shared_lockable_ptr(T *ptr, Fn &&custom_deleter):_subject(ptr, SharedPtrDeleter{}) {
        SharedPtrDeleter *deleter = std::get_deleter<SharedPtrDeleter>(_subject);
        deleter->init_custom_deleter<T>(std::forward<Fn>(custom_deleter));
    }

    ///Constructor from raw pointer, custom deleter and allocator - takes ownership of the pointer
    template<typename Fn, typename Alloc>
    shared_lockable_ptr(T *ptr, Fn &&custom_deleter, Alloc &&alloc):_subject(ptr, SharedPtrDeleter{}, std::forward<std::decay_t<Alloc> >(alloc)) {
        SharedPtrDeleter *deleter = std::get_deleter<SharedPtrDeleter>(_subject);
        deleter->init_custom_deleter<T>(std::forward<Fn>(custom_deleter));
    }

    ///Constructor from shared pointer - takes ownership of the pointer
    explicit shared_lockable_ptr(std::shared_ptr<T> &&ptr):_subject(std::move(ptr)) {
        test_deleter();
    }
    ///Constructor from shared pointer - takes ownership of the pointer
    explicit shared_lockable_ptr(const std::shared_ptr<T> &ptr):_subject(ptr) {
        test_deleter();
    }

    ///Lock the pointer for exclusive access - returns unique_ptr with custom deleter which unlocks the mutex when destroyed
    auto lock() {
        return lock_ptr_exclusive(_subject);
    }
    ///Lock the pointer for shared access - returns unique_ptr with custom deleter which unlocks the mutex when destroyed
    auto lock() const {
        return lock_ptr_shared(_subject);
    }
    ///Lock the pointer for shared access - returns unique_ptr with custom deleter which unlocks the mutex when destroyed
    auto lock_shared() {
        return lock_ptr_shared(_subject);
    }
    ///Lock the pointer for shared access - returns unique_ptr with custom deleter which unlocks the mutex when destroyed
    auto lock_shared() const {
        return lock_ptr_shared(_subject);
    }

    ///Constructor from compatible shared_lockable_ptr - allows to construct shared_lockable_ptr<Base> from shared_lockable_ptr<Derived>
    template<typename U>
    requires std::convertible_to<U *, T *>
    shared_lockable_ptr(const shared_lockable_ptr<U> &src):_subject(src._subject) {}

    ///reset the pointer to empty
    void reset() {
        _subject.reset();
    }

    ///reset the pointer to new raw pointer - takes ownership of the pointer
    void reset(T *ptr) {
        if (ptr != _subject.get()) {
            (*this) = shared_lockable_ptr(ptr);
        }
    }

    ///convenience function to read the value under shared lock - returns false if pointer is empty
    /**
    @tparam Fn type of the function to call with the value. The function must be invocable with const T & (or T & for non-const version) and return void.
             The function is called under shared lock, so it can safely read the value. Use closure to capture reference to variables to read the value. 
             If the pointer is empty, the function is not called and false is returned. Otherwise, the function is called and true is returned. 
    @retval true function called
    @retval false pointer is empty and function not called
    @note recommended to use as it protects against null pointers.
    */
    template<std::invocable<const T &> Fn>
    bool read(Fn &&fn) const{
        auto lk = lock_shared();
        const T *val = lk.get();
        if (!val) return false;
        std::forward<Fn>(fn)(*val);
        return true;
    }

    ///convenience function to update the value under exclusive lock - returns false if pointer is empty
    /**
    @tparam Fn type of the function to call with the value. The function must be invocable with T & and return void. The function is called under exclusive lock, so it can safely read and modify the value. If the pointer is empty, the function is not called and false is returned. Otherwise, the function is called and true is returned. 
    @retval true function called
    @retval false pointer is empty and function not called
    @note recommended to use as it protects against null pointers.
    */
    template<std::invocable<T &> Fn>
    bool update(Fn &&fn) {
        auto lk = lock();
        T *val = lk.get();
        if (!val) return false;
        std::forward<Fn>(fn)(*val);
        return true;
    }


protected:

    void test_deleter() {
        auto d = std::get_deleter<SharedPtrDeleter>(_subject);
        if (d == nullptr) throw std::invalid_argument("bad shared pointer");
    }

    std::shared_ptr<T> _subject;


    template<typename U>
    friend class shared_lockable_ptr;



};


template<typename T>
class lockable_weak_ptr {
public:

    lockable_weak_ptr(const shared_lockable_ptr<T> &ptr):_subject(ptr._subject) {}

    shared_lockable_ptr<T> lock() const {
        return shared_lockable_ptr<T>(_subject.lock());
    }


protected:
    std::weak_ptr<T> _subject;
};


}