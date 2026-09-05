#pragma once

#include "basic_coro/coroutine.hpp"
#include "quarkbot/abstract/imessage_bus.hpp"
#include "quarkbot/event_stream.hpp"
#include "quarkbot/message_bus.hpp"
#include "quarkbot/storage.hpp"
#include "quarkbot/strategy_fragment.hpp"
#include <span>
namespace quarkbot {


    ///attach replicator
    /**
        Attaches replicator to a storage and replays events into message bus as messages

        Format of messages:

        <T><payload...>

        <T> = 'S' put_schema     <8B schema hash, big endian> <blob value>
              'P' put_key_value  <blob name> <16B recordkey> <blob value>
              'L' update_latest  <blob name> <16B recordkey>
              'K' erase_key      <blob name> <16B recordkey>
              'R' erase_latest   <blob name>
              'N' erase_name     <blob name>

        <blob>      = <size><bytes>, size variable length big endian with continuation bit 7
        <recordkey> = 16 bytes big endian, the same encoding a physical key uses

        @param storage storage
        @param bus a message bus
        @param target target name (receiver)
        @return connection which must be held to keep replication alive. To stop
        replication simply drop the return value
    */

    ///Number of bytes encode_replication_message needs for this event
    std::size_t replication_message_size(const Storage::ReplicatorEvent &ev);

    ///Encodes event into the wire format
    /**
        @param ev event to encode
        @param buffer destination, at least replication_message_size(ev) bytes
        @return the written prefix of buffer
    */
    std::span<char> encode_replication_message(const Storage::ReplicatorEvent &ev, std::span<char> buffer);

    Storage::Replicator::Connection attach_replicator(Storage storage, MessageBus bus, std::string target);


    ///Parse and replicate message
    /**
    @param msg message
    @param storage storage
    @retval true message replicated
    @retval false filtered out
     */
    bool replicate_from_message(const std::string_view &msg, StorageTransaction &trn);

    ///Replicator runner, reads message stream and writes into storage
    /**
        @param msg_stream stream obtained from MessageBus containing messages
        @param storage target storage
        @param filter filter only messages for given target

        As this is StrategyFragment, it must run in ExectionWorker. It also uses shared transactions to perform delayed commits
    */
    StrategyFragment replicate_events(EventStream<Message> msg_stream, Storage storage, std::string filter = {});
    


}