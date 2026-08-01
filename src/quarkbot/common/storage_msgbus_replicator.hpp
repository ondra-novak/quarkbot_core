#pragma once

#include "basic_coro/coroutine.hpp"
#include "quarkbot/abstract/imessage_bus.hpp"
#include "quarkbot/event_stream.hpp"
#include "quarkbot/message_bus.hpp"
#include "quarkbot/storage.hpp"
#include "quarkbot/strategy_fragment.hpp"
namespace quarkbot {


    ///attach replicator 
    /**
        Attaches replicator to a storage and replays events into message bus as messages
        Format of messages:
        
        <F><S><size><key><size><value>

        <F> = 'P'- put, 'E' -erase (no value transfered)
        <S> = 'R' - row 'S' - schema
        <size> = variable length encoded size, big endian with continuation bit 7 (1= continues, 0= final)
        <key/value> = content of key or value

        @param storage storage
        @param bus a message bus
        @param target target name (receiver)
        @return connection which must be held to keep replication alive. To stop replication simply drop the return value
    */
    Storage::Replicator::Connection attach_replicator(Storage storage, MessageBus bus, std::string target);


    ///Parse and replicate message
    /**
    @param msg message
    @param storage storage
    @retval true message replicated
    @retval false filtered out
     */
    bool replicate_from_message(const std::span<const std::uint8_t> &msg, StorageTransaction &trn);

    ///Replicator runner, reads message stream and writes into storage
    /**
        @param msg_stream stream obtained from MessageBus containing messages
        @param storage target storage
        @param filter filter only messages for given target

        As this is StrategyFragment, it must run in ExectionWorker. It also uses shared transactions to perform delayed commits
    */
    StrategyFragment replicate_events(EventStream<Message> msg_stream, Storage storage, std::string filter = {});
    


}