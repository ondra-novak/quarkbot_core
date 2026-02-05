#pragma once

#include "coro/src/basic_coro/await_proxy.hpp"
#include "coro/src/basic_coro/concepts.hpp"
#include "coro/src/basic_coro/coro_frame.hpp"
#include "coro/src/basic_coro/exceptions.hpp"
#include "coro/src/basic_coro/awaitable.hpp"
#include "coro/src/basic_coro/prepared_coro.hpp"
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <new>
#include <optional>
#include <type_traits>

namespace coro {

namespace _details {

    template<typename T, typename ... Uvs>
    struct CalcSizeHelper {
        T _head;
        CalcSizeHelper<Uvs...> _tail;
    };

    template<typename T>
    struct CalcSizeHelper<T> {
        T _head;
    };


}


///Handles transformation of awaitable to another awaitable with different result type. It can be used to implement operators like then, finally, etc. 
/**
    * @tparam Awt expected awaitable type
    * @tparam Closure types of closure arguments (like pred in then() operator)
    All above types are used to calculate required space. The actual objects can be different during asynchronous call, but must fit in the space.
    @note the object must be alive during asynchronous call, so it is not recommended to create temporary object of this type. It is better to create one and reuse it for multiple calls. 
    The object is not MT safe nor reentrant, so it is not recommended to use it in multiple threads or for recursive calls.
    The best place for this object is as a member of an object offering an asynchronous interface.

    Usage:
    @code
        awaitable_transform<int, some_awaitable, some_closure> tr; //declare as member
        //...
        auto new_awaitable = tr(async_call(), [this](auto result){return transform_result(result);});
    @endcode    



 */
template<is_awaitable Awt, typename ... Closure>
class awaitable_transform;

///Same as `awaitable_transform` but you can specify ResultType template which used instead awaitable_result<T>. 
/**
 @tparam ResultType - template with one argument T of returning type used to carry promise for capture asynchronous result.
 It must act as callback which has compatible API with awaitable_result. This allows to implement special processing, such
 a routing execution through a dispatcher
 @param Awt expected awaiter
 @param Closure expected closure types
*/
template<template <typename> class ResultType, is_awaitable Awt,  typename ... Closure>
class awaitable_transform_r {

public:

    ///default constructible
    awaitable_transform_r() = default;
    ///non copyable
    awaitable_transform_r(const awaitable_transform_r &) = delete;
    ///non copy assignable
    awaitable_transform_r &operator=(const awaitable_transform_r &) = delete;

    ///perform transformation
    /**
     * @param awt awaitable (any suitabable awaiter compatible with co_await expression)
     * @param pred closure to perform transformation. It is called with result of awaitable as argument.
                   the transform function can be asynchronous, it just need to return a valid awaiter with a value type. Note that
                   returned awaiter must also fit into space reserved for the awaiter.
                   The function can throw an exception, which propagated to the result awaitable as execption.
                   If the awaitable is canceled, the function is not called and the result awaitable is also canceled. Cancellation is propagated as std::nullopt value.
     * @return awaitable with result of type T, which is the result of transformation
     * @note don't call this function if the previous call is not completed, otherwise the behaviour is undefined
     */
    template<typename _Awt, std::invocable<awaiter_result<_Awt> > _Pred>
    auto operator()(_Awt &&awt, _Pred &&pred) {
        //contains result of the predicate
        using FnResult = std::invoke_result_t<_Pred, awaiter_result<_Awt> >;
        //if callback returns awaiter, we need its result, otherwise it predicate result is used
        using OutResult = typename std::conditional_t<is_awaiter<FnResult>,awaiter_result_def<FnResult>, std::type_identity<FnResult> >::type;
        //construct awaitable result
        using Result = awaitable<OutResult>;
        //test readinnes of the result;
        if (awt.await_ready()) {
            try {
                //if predicate returns avaiter
                if constexpr(is_awaiter<FnResult>) {
                    //invoke self but use returned awaiter as awaiting awaiter
                    return std::invoke(*this, (std::invoke(std::forward<_Pred>(pred), awt.await_resume()), 
                                [](auto&& x) -> decltype(auto) {return std::forward<decltype(x)>(x);}));
                } else {
                    //return result
                    return Result(pred(awt.await_resume()));
                }
            } catch (await_canceled_exception) {
                //if awaitable was canceled  - propagate cancel
                return Result(std::nullopt);
            } catch (...) {
                //otherwise propagate exception
                return Result(std::current_exception());
            }
        } else {
            static_assert(sizeof(_frame._awt) >= sizeof(_Awt), "Awaiter is too large" );
            static_assert(sizeof(_frame._closure) >= sizeof(_Pred), "Closure of transform function is too large" );

            //move arguments to frame
            auto awtptr = reinterpret_cast<_Awt *>(_frame._awt);
            //awaiter (must be movable)
            std::construct_at(awtptr, std::move(awt));

            auto predptr = reinterpret_cast<_Pred *>(_frame._closure);
            //predicate (must be movable)
            std::construct_at(predptr, std::move(pred));

            //create pointer with cleanup feature
            //the async operation may not be initiated, in such case, the callback is destroyed without invocation
            //so this handles cleanup in such case
            auto frame_cleanup = std::unique_ptr<frame, decltype([](frame *frm){
                    auto awtptr = std::launder(reinterpret_cast<_Awt *>(frm->_awt));
                    auto predptr = std::launder(reinterpret_cast<_Pred *>(frm->_closure));                    
                    std::destroy_at(awtptr);
                    std::destroy_at(predptr);
            })>(&_frame);
            
            //prepare callback for asynchronous call - called when async operation is initiated
            return Result([frmclnp = std::move(frame_cleanup)](auto promise) mutable -> prepared_coro {                
                //determine promise type
                using Promise = ResultType<OutResult>;
                //retrieve frame pointer and disable autocleanup operation - cancel is not possible now
                auto frm = frmclnp.release();

                static_assert(sizeof(frm->_result) >= sizeof(Promise), "Result of awaitable is unexpectly large");
                //initialize result
                auto resptr = reinterpret_cast<Promise *>(frm->_result);
                std::construct_at(resptr, std::move(promise));

                auto awtptr = std::launder(reinterpret_cast<_Awt *>(frm->_awt));


                //trailer function - handling conversion itself - called when await is complete
                frm->trailer = [](frame *frm) -> prepared_coro{

                    //retrieve all object - launder to break aggresive optimization
                    auto resptr = std::launder(reinterpret_cast<Promise *>(frm->_result));
                    auto awtptr = std::launder(reinterpret_cast<_Awt *>(frm->_awt));
                    auto predptr = std::launder(reinterpret_cast<_Pred *>(frm->_closure));                    
                    prepared_coro result;
                    //try to read result
                    try {
                        //if predicate returns awaiter
                        if constexpr(is_awaiter<FnResult>) {
                            //invoke predicate and retrieve awaiter
                            auto awt = std::invoke(*predptr, awtptr->await_resume());                            
                            //if returned awaiter is ready
                            if (awt.await_ready()) {
                                //then store its result as actual result of the operation and done
                                result = std::invoke(*resptr, awt.await_resume());
                            } else {
                                //we need continue awaiting on returned awaiter
                                //so clear some space
                                //for awaiter
                                std::destroy_at(awtptr);
                                //predicate is no longer needed
                                std::destroy_at(predptr);
                                //move awaiter to available space
                                awtptr = std::construct_at(frm->_awt, std::move(awt));
                                //define new trailer - called when operation is complete
                                frm->trailer = [](frame *frm){
                                    //again retrieve objects - result
                                    auto resptr = std::launder(reinterpret_cast<Promise *>(frm->_result));
                                    //and awaiter
                                    auto awtptr = std::launder(reinterpret_cast<_Awt *>(frm->_awt));
                                    prepared_coro result;
                                    try {
                                        //attempt to resolve and store result
                                        result = std::invoke(*resptr, awtptr->await_resume());
                                    } catch (await_canceled_exception) {
                                        //if canceled, store nullopt
                                        result = std::invoke(*resptr, std::nullopt);
                                    } catch (...) {
                                        //if exception, store exception
                                        result = std::invoke(*resptr, std::current_exception());
                                    }
                                    //clean up
                                    std::destroy_at(awtptr);
                                    std::destroy_at(resptr);
                                    //return prepared coroutine
                                    return result;
                                };
                                //continue waiting, initiate await_suspend on returned awaiter
                                return call_await_suspend(*awtptr, frm->create_handle());
                            }
                        } else {
                            //call predicate with result
                            result = std::invoke(*resptr, std::invoke(*predptr,awtptr->await_resume()));
                        }
                    } catch (await_canceled_exception) {
                        //handle cancel
                        result = std::invoke(*resptr, std::nullopt);
                    } catch (...) {
                        //handle exception
                        result = std::invoke(*resptr, std::current_exception());
                    }
                    //destroy everything
                    std::destroy_at(resptr);
                    std::destroy_at(awtptr);
                    std::destroy_at(predptr);
                    //return prepared coroutine (if any)
                    return result;
                };

                //everything ready, now initiate the suspend operation
                return call_await_suspend(*awtptr, frm->create_handle());
            });
        }
    }

    static constexpr std::size_t align_up(std::size_t size, std::size_t alignment) {
        return (size + alignment - 1) / alignment * alignment;
    }

    static constexpr std::size_t closure_alignment = std::alignment_of_v<_details::CalcSizeHelper<Closure ...> >;
    static constexpr std::size_t awt_alignment = std::alignment_of_v<Awt >;    
    static constexpr std::size_t awaiter_size = align_up(sizeof(Awt),std::max(closure_alignment, awt_alignment));
    static constexpr std::size_t closure_size = sizeof(_details::CalcSizeHelper<Closure ...>);
    static constexpr std::size_t result_size = sizeof(ResultType<std::uintmax_t>);

    //emulates coroutine frame - act as coroutine, calls resume()
    struct frame: public coro_frame<frame> {
        
        prepared_coro (*trailer)(frame *me) = nullptr;
        char _result[result_size] = {};
        char _awt[awaiter_size] = {};
        char _closure[closure_size] = {};

        prepared_coro resume() {
            return trailer(this);
        }
        void destroy() {resume();}
    };

protected:    
    frame _frame;
};

template<is_awaitable Awt, typename ... Closure>
class awaitable_transform: public awaitable_transform_r<awaitable_result,Awt,  Closure...> {};


}