#pragma once

#include <mutex>
#include <memory>
#include <vector>
#include <algorithm>


    template<typename Prototype> class SignalConsumer;
    template<typename Fn, typename Prototype> class FunctorSignalConsumer;

    template<typename ... Args>
    class SignalConsumer<void(Args ...)> {
    public:
        virtual ~SignalConsumer() = default;
        virtual void operator()(Args ... args) noexcept = 0;
    };

    template<typename Fn, typename ... Args>
    requires (std::is_nothrow_invocable_v<Fn, Args...>)
    class FunctorSignalConsumer<Fn, void(Args...)> : public SignalConsumer<void(Args...)> {
    public:
        template<typename ... Ts>
        requires (std::is_constructible_v<Fn, Ts...>)
        FunctorSignalConsumer(Ts && ... params): _fn(std::forward<Ts>(params)...) {}
        virtual void operator()(Args ... args) noexcept {
            _fn(std::forward<Args>(args)...);
        }

    private:
        Fn _fn;
    };

    struct SyncSignalDispatcher {
        template<typename Fn, typename ... Args>        
        requires(std::is_invocable_v<decltype(*std::declval<Fn>()), Args...>)
        constexpr void operator()(Fn &&fn, Args && ... args) const {
            (*fn)(std::forward<Args>(args)...);
        }
    };

    class SignalScratchpad {
    public:
        std::vector<std::shared_ptr<void> > _lst;
        static SignalScratchpad &get_instance() {
            static thread_local SignalScratchpad sp;
            return sp;
        }
    };

    ///Represents unmovable not shareable instance of signal slot
    /**
     * @tparam Prototype define function prototype. Currently only void functions are supported,
     * so you need always specify in for void(Args...). For example void(int, float) creates
     * signal slot which accepts integer and float arguments
     * 
     * @tparam Dispatcher specifies class responsible to dispatch the signal. Default value
     * (aka SyncSignalDispatchers) dispatches the signal synchronously in the callers thread.
     * All signal consumers are called synchronously one after other. Alternatively you
     * can install AsyncSignalDispatcher which allows to call consumers in a thread pool
     * 
     * Examples:
     * @code
     * SignalSlot<void(int)> slot1
     * SignalSlot<void(std::string), AsyncSignalDispatcher> slot2;
     * @endcode
     */
    template<typename Prototype, typename Dispatcher = SyncSignalDispatcher, typename Lock = std::mutex>
    class SignalSlot;


    ///Represents movable and shareable instance of signal slot
    /**
     * This acts as a smart pointer to SignalSlot. Its default constructor doesn't create
     * any signal slot. You need to call static function SharedSignalSlot::create() to 
     * create an instance of signal slot. Once you have an instance, you can copy
     * it as you need, all these copies points to signle signal slot (shared). The
     * signal slot is destroyed once last reference is released
     * 
     * The SharedSignalSlot has similar API as SignalSlot. 
     * 
     * @tparam Prototype specifies function prototype for the signal. It
     * is specified as typical function call example: void(Args...). Only 
     * void is allowed as return value
     * 
     * @tparam Dispatcher specifies dispatcher. 
     * 
     * @see SignalSlot
     */
    template<typename Prototype, typename Dispatcher = SyncSignalDispatcher, typename Lock = std::mutex>
    class SharedSignalSlot;
   
    template<typename T>
    concept create_constructible = requires {
        {T::create()}->std::same_as<T>;
    };

    enum class ConnectionMode {
        ///Normal connection mode, connection is active until disconnected
        normal,
        ///One shot connection mode, connection is disconnected once it receives signal
        one_shot
    };


    template<typename Dispatcher, typename Lock, typename ... Args>
    class SignalSlot<void(Args...), Dispatcher, Lock> {
    public:
        ///Abstract consumer
        using Consumer = SignalConsumer<void(Args...)>;
        ///Connection object 
        /** Connection object is a smart pointer which points to Consumer object.
         * The Consumer object acts as function, which directly calls the consumer.
         * With this knowledge you can use Connection to selectively call this consumer
         * outside of signal slot. If you need to disconnect the connection, just
         * call reset() on this object. However, the connection object can be shared
         * and you need to reset all shared instances to perform full disconnect
         */
        using Connection = std::shared_ptr<Consumer>;

        struct ConsumerReg {
            int priority;
            ConnectionMode mode;
            std::weak_ptr<Consumer> consumer;
        };


        union TmpReference {
            Connection con;
            TmpReference() {}
            ~TmpReference() {};
        };
    
        ///Construct signal slot with default settings
        /**
         * Inicializes signal slot and its dispatcher. The dispatcher must be default constructible 
         * or create constructible (Dispatcher::create())
         */
        SignalSlot() = default;

        ///Construct signal slot and initialize dispatcher
        /**
         * @param disp instance of dispatcher. It should be at least movable
         */
        explicit SignalSlot(Dispatcher disp):_dispatcher(disp) {}


        ///Creates connection object from a callable, but doesn't connect this object
        /**
         * @param fn function which consumes signal sent to this connection
         * @return Connection object. You can connect this object to signal slot later
         */
        template<typename Fn>        
        requires(std::is_nothrow_invocable_v<Fn, Args...>)
        static Connection create_connection(Fn &&fn) {
            return std::make_shared<FunctorSignalConsumer<std::remove_cvref_t<Fn>, void(Args...)> >(std::forward<Fn>(fn));
        }
        

        ///Connect consumer to signal slot
        /** 
         * Connect consumer defined as function call to signal slot
         * 
         * @param slot reference to signal slot. Note that this function is actually global, not member function
         * (you need to use connect(slot, fn)
         * @param fn function compatible with slot's prototype. NOTE: The function must be noexcept
         * @param priority specifies priority. Consumers are ordered from high priority (high number), to
         * low priority(low number). If you ignore priority, consumers are called in order of connection
         * @param mode specifies connection mode. It can be either normal or one_shot
         * @return Connection object. To keep connection active, you need to hold Connection object somewhere. Once
         * this object is destroyed, the connection is disconnected. 
         * 
         * @note Connection object can be shared. In order to disconnect the connection, you need to destroy
         * all shared instances.
         * 
         * 
         */
        template<typename Fn>        
        requires(std::is_nothrow_invocable_v<Fn, Args...>)
        friend Connection connect(SignalSlot &slot, Fn &&fn, int priority = 0, ConnectionMode mode = ConnectionMode::normal) {
            return connect(slot, create_connection(std::forward<Fn>(fn)),priority, mode);
        }


        ///Connect consumer to signal slot
        /** 
         * Connect consumer defined as function call to signal slot
         * 
         * @param slot reference to signal slot. Note that this function is actually global, not member function
         * (you need to use connect(slot, fn)
         * @param fn function compatible with slot's prototype. NOTE: The function must be noexcept
         * @param priority specifies priority. Consumers are ordered from high priority (high number), to
         * low priority(low number). If you ignore priority, consumers are called in order of connection
         * @param mode specifies connection mode. It can be either normal or one_shot
         * @return Connection object. To keep connection active, you need to hold Connection object somewhere. Once
         * this object is destroyed, the connection is disconnected. 
         * 
         * @note Connection object can be shared. In order to disconnect the connection, you need to destroy
         * all shared instances.
         * 
         * 
         */
        template<typename Fn>        
        requires(std::is_nothrow_invocable_v<Fn, Args...>)
        friend Connection operator>>(SignalSlot &slot, Fn &&fn)  {
            return connect(slot, std::forward<Fn>(fn));
        }


        ///Connect consumer to signal slot
        /**
         * @param slot this slot
         * @param conn existing connection. You can use this function to connect existing connection
         * to just another signal slot. It just creates new connection to the same consumer
         * @param priority priority see original connect()
         * @param mode specifies connection mode. It can be either normal or one_shot
         * @return conn instance
         * 
         */
        friend Connection connect(SignalSlot &slot, const Connection &conn, int priority =0, ConnectionMode mode = ConnectionMode::normal) {
            std::lock_guard _(slot._mx);
            auto iter = std::upper_bound(slot._consumers.begin(), slot._consumers.end(), ConsumerReg(priority, {}, {}), priority_order);            
            slot._consumers.insert(iter, ConsumerReg(priority, mode,  conn));
            return conn;
        }

        ///Connect consumer to signel slot
        /** Shortcut to connect()
         * 
         * @see connect
         */
        friend Connection operator>>(SignalSlot &slot, const Connection &conn) {
            return connect(slot, conn);
        }

        ///Disconnect a connection
        /**
         * Manually disconnect specified connection from specified signal slot. This function
         * doesn't affect connections to other signal slots
         */
        friend void disconnect(SignalSlot &slot, const Connection &con) {
            std::lock_guard _(slot._mx);
            auto e = std::remove_if(slot._consumers.begin(), slot._consumers.end(), [&](const ConsumerReg &c){
                Consumer cc = c.consumer.lock();
                return !cc || cc == con;
            });
            slot._consumers.erase(e, slot._consumers.end());

        }


        ///Send signal
        /**
         * @param params arguments associated with the signal
         */
        template<typename ... Params>        
        requires (std::is_invocable_v<Dispatcher, Connection, Params...> )
        void operator()(Params && ... params) {
            //retrieve TLS scratchpad to store active consumers
            SignalScratchpad &sp = SignalScratchpad::get_instance();
            //retrieve current position in the scratchpad (could be non-zero)
            std::size_t sp_pos = sp._lst.size();
            //prepare all consumers into scratchpad
            prepare_consumers([&](Connection &&con){
                sp._lst.push_back(con);
            });
            //mark end of scratchpad
            std::size_t sp_end = sp._lst.size();
            //process our content of the scratchpad and broadcast 
            for (std::size_t i =sp_pos; i != sp_end; ++i) {
                Connection con = std::static_pointer_cast<Consumer>(sp._lst[i]);
                _dispatcher(std::move(con), params...);
            }
            //additional items of scartchpad should be released
            //don't check it, just release our written data
            sp._lst.resize(sp_pos);
            //all clear
        }

    private:
        
        [[no_unique_address]] Dispatcher _dispatcher = auto_create_dispatcher();
        [[no_unique_address]] Lock _mx;
        std::vector<ConsumerReg> _consumers = {};

        static Dispatcher auto_create_dispatcher() requires(create_constructible<Dispatcher>) {
            return Dispatcher::create();
        }

        
        static Dispatcher auto_create_dispatcher() requires(!create_constructible<Dispatcher>) {
            static_assert(std::is_default_constructible_v<Dispatcher>);
            return Dispatcher();
        }


        template<typename Cb>
        void prepare_consumers(Cb &&cb) {
            std::lock_guard _(_mx);
            auto e = std::remove_if(_consumers.begin(), _consumers.end(), [&](auto &c){
                Connection cc = c.consumer.lock();
                if (cc) {
                    cb(std::move(cc));
                    return c.mode == ConnectionMode::one_shot;
                } else{
                    return true;
                }                    
            });
            _consumers.erase(e, _consumers.end());
        }

        static bool priority_order(const ConsumerReg&a, const ConsumerReg &b) {
            return a.priority > b.priority;
        }
    };

    
    template<typename Dispatcher, typename Lock, typename ... Args>
    class SharedSignalSlot<void(Args...), Dispatcher, Lock> {
    public:        
        using SignalSlot = SignalSlot<void(Args...), Dispatcher, Lock>;
        using Ref = std::shared_ptr<SignalSlot>;
        using Consumer = typename SignalSlot::Consumer;
        using Connection = typename SignalSlot::Connection;

        ///Construct empty shared signal slot. 
        /**
         * Empty instance cannot be used to send signals and connect consumers. 
         * You need to call create() to create instance
         */
        SharedSignalSlot() = default;

        ///Send signal
        /**
         * @param params arguments associated with the signal
         */
        template<typename ... Params>        
        requires (std::is_invocable_v<Dispatcher, Connection, Params...> )
        void operator()(Params && ... params) {
            (*_sslot)(std::forward<Params>(params)...);
        }

        template<typename Fn>        
        requires(std::is_nothrow_invocable_v<Fn, Args...>)
        friend Connection connect(SharedSignalSlot &slot, Fn &&fn, int priority = 0, ConnectionMode mode = ConnectionMode::normal) {
            return connect(*slot._sslot,std::forward<Fn>(fn), priority, mode);
        }

        friend Connection connect(SharedSignalSlot &slot, const Connection &con, int priority = 0, ConnectionMode mode = ConnectionMode::normal) {
            return connect(*slot._sslot,con, priority, mode);
        }

        friend void disconnect(SharedSignalSlot &slot, const Connection &con) {
            return disconnect(*slot._sslot,con);
        }


        template<typename Fn>        
        requires(std::is_nothrow_invocable_v<Fn, Args...>)
        friend Connection operator>>(SharedSignalSlot &slot, Fn &&fn)  {
            return connect(slot, std::forward<Fn>(fn));
        }

        friend Connection operator>>(SharedSignalSlot &slot, const Connection &conn) {
            return connect(slot, conn);
        }


        ///Create shared signal slot
        /**
         * @return an instance of shared slot
         */
        static SharedSignalSlot create() {
            return SharedSignalSlot(std::make_shared<SignalSlot>());
        }

        ///Create shared slot initialize dispatcher
        /**
         * @param dispatcher an instance of dispatcher
         * @return an instance of shared slot
         */
        static SharedSignalSlot create(Dispatcher dispatcher) {
            return SharedSignalSlot(std::make_shared<SignalSlot>(dispatcher));
        }

    private:
        SharedSignalSlot(Ref &&ref):_sslot(std::move(ref)) {}

        Ref _sslot;
    };



