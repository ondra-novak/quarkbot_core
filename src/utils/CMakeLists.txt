add_library(quarkbot_utils INTERFACE)

target_link_libraries(quarkbot_utils INTERFACE BasicCoro::basic_coro)
target_sources(quarkbot_utils INTERFACE
         FILE_SET HEADERS  
         BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/.." FILES
         
            hashable.hpp
            serialize.hpp
            decimal.hpp
            bigendian.hpp
            ref_count.hpp
            spin_mutex.hpp
            lockfree_queue.hpp
            scheduled_queue.hpp
            fnv1a.hpp
            pubsub.hpp
            round.hpp
            coro_dispatch.hpp
            init_with.hpp
            utf8.hpp
            random_string.hpp
            flatmap.hpp
            signals.hpp
            json.hpp
            jumptable.hpp
            type_name.hpp
            serialize_schema_to_json.hpp
            acb.hpp
            dispatcher.hpp
            signals_async.hpp
            function_view.hpp
)