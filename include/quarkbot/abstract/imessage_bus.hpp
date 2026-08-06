#pragma once

#include "ieventstream.hpp"

#include "../serializer/schema_fwd.hpp"
#include "quarkbot/log.hpp"
#include "quarkbot/utils/refcnt.hpp"


namespace quarkbot {


    using ConversationID = std::uint64_t;


    enum class MessageType {
        ///normal peer-to-peer, peer-to-group, peer-to-service message (has target)
        /**
            From: UUID of sender 
            To: UUID, service name, group name
            Type: normal_message
            Payload: anything            

            Every router should remember a direction back to the sender in case to route a response.             
        */
        normal_message = 0,
        ///anounce message - sender contains name of service, target contains absolute TTL (in milliseconds) as string
        /**
            From: my_service
            To: 1784700083313
            Type: announce
            Payload: "at your service"


            this message is broadcasted to all routers and nodes. Each router remembers path
            to the service as direction back to the sender

            To remove service, broadcast message with TTL = now()

            payload can contain any useful informations.
         */
        announce = 1,
        ///add target to a group
        /**
            From: UUID of group owner
            To: UUID of new member
            Type: add_to_group
            Payload: group name (string)

            Router just forwards the message. It also need to remember which direction is member of the group.
            Messages having a target as a one of the registered groups must be forwarded in remembered direction.            

         */
        add_to_group = 2,
        ///group has been closed, this message is posted to group-id, 
        /**
            From: UUID of group owner
            To: group name
            Type: group_close
            Payload: farewell message

            Router forwards message to the group and deletes the group            
        */
        group_close = 3,
        ///There is no route to target
        /**
            From: non-existing target
            To: original sender
            Type: no_route
            Payload: empty

            Send to original sender. Every router in way must
            remove target from the routing table in original direction.
            Groups can have multiple directions, in this case, forwarding
            stops, when group still exists after direction removal
        */
        no_route = 4
    };

    ///Message item (for message streams)
    struct Message {

        ///message type
        MessageType type = MessageType::normal_message;
        ///Send of this message
        std::string_view sender = {};
        ///Target or topic
        std::string_view target = {};
        ///Message payload
        std::string_view payload ={};
        ///Conversation id
        ConversationID conversation_id ={};
        ///Schema hash of payload
        srl::SchemaHash schema = {};
        ///time on send side
        std::chrono::system_clock::time_point send_time = {};
        ///holds reference to snapshot to keep lifetime 
        RefCountPtr<RefCountInstanceWithDeleter> ownership = {};

        ///Extract value to type, function checks for schema
        /**
            @param x variable that receives value
            @retval true extracted
            @retval false failed to extract, schema mismatch or parse error
        */
        template<typename T>
        bool extract(T &x);
    };

    ///Class which handles sending and receiving messages
    class IMessageBus {
    public:
        virtual ~IMessageBus() = default;

        ///Subscribe stream
        /**
            @return message stream
        */
        virtual std::shared_ptr<IEventStream<Message> > subscribe() = 0;

        ///Send message 
        /**
            @param msg message to send. For the standard node left "sender" empty. 
            If this node is router, the sender must contain actual ID of sender
        */
        virtual void send(const Message &msg) = 0;

        class Null;
    };

    class IMessageBus::Null final: public IMessageBus{
    public:
        virtual std::shared_ptr<IEventStream<Message> > subscribe() override {
            logWarning("Nobody is listening message bus, subscribing to void");
            return IEventStream<Message>::Silent::create_instance();
        }
        virtual void send(const Message &msg) override {
                logWarning("Message discarded: target={}, length={}, conversation_id={}, schema={:x}",
                    msg.target, msg.payload.size(), msg.conversation_id, msg.schema
                );            
        }        
    };

    constexpr auto null_messagebus = IMessageBus::Null();


    


}