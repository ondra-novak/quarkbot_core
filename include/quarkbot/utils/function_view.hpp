//@file function_view.hpp

#include <type_traits>
#include <utility>

template<typename Fn> class function_view;

namespace _details {

template<bool nx, typename RetVal, typename ... Args>
class FunctionViewImpl {
public:

    using CallFnPtr = RetVal (*)(const void *context, Args && ...)
                                                             noexcept (nx);


    RetVal operator()(Args ... args) const noexcept(nx){
        return _callptr(_context, std::forward<Args>(args) ...);
    }

    template<typename Fn>
    requires(std::is_invocable_r_v<RetVal, Fn, Args...> )
    FunctionViewImpl(Fn &&fn) {
        _context = &fn;
        _callptr = [](const void *ctx, Args && ...  args) {
            Fn *fptr = const_cast<Fn *>(
                       static_cast<std::add_const_t<Fn> *>(ctx));
            return (*fptr)(std::forward<Args>(args)...);
        };
    }

protected:

    CallFnPtr _callptr;
    const void *_context;
};

}

template< class R, class... Args >
class function_view<R(Args...)>:
   public _details::FunctionViewImpl<false, R, Args...> {
    using _details::FunctionViewImpl<false, R, Args...>::FunctionViewImpl;
};


template< class R, class... Args >
class function_view<R(Args...) noexcept>:
   public _details::FunctionViewImpl<true, R, Args...> {
    using _details::FunctionViewImpl<true, R, Args...>::FunctionViewImpl;
};