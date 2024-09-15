#pragma once

#include "mq.h"

namespace quarkbot {


///Base class for bridges - classes intended to forward messages between nodes
/**
 * This abstract class is one end of a bridge. You obviously need two ends, so you need
 *  to implement two such classes. For example client and server
 *
 *  You need to implement two methods: on_message() and on_channels_update(). If you have a processing
 *  thread, you should also implement on_update_channels to notify processing thread to invoke
 *  send_mine_channels() - in its context
 *
 *  MT Safety : the class is mt-safe while different methods are called. It is not mt-safe to
 *  call the same method from different threads at the same time. Note that peer_reset() calls
 *  send_mine_channels() and on_channels_update() also calls send_mine_channels().  Also note that
 *  it si ok to call on_message() from multiple threads as this is feature of MQBroker itself
 *
 *
 *
 */
class MQAbstractBridge : public MQClient, public IMQBroker::IMonitor {
public:

    using Message = IMQBroker::Message;
    using ChannelID = IMQBroker::ChannelID;
    using ChannelList = std::span<ChannelID>;

    ///Construct the bridge
    /**
     * @param broker broker to connect
     */
    MQAbstractBridge(MQBroker broker);
    ///Destruct the bridge
    ~MQAbstractBridge();

    ///Abstract bridge is not movable
    MQAbstractBridge(const MQAbstractBridge &broker) = delete;
    ///Abstract bridge is not movable
    MQAbstractBridge &operator=(const MQAbstractBridge &broker)  = delete;

    ///Sends list of channels of current broker to the other side
    /**
     * Retrieves active list of channels from a connected broker and generates a list which is then
     * forwarded to the function on_channels_update(). It also detects changes in the list and skips
     * sending the list if no change detected
     *
     * @note @b mt-safety: this method is not mt-safe
     */
    void send_mine_channels();

    ///Apply list of channels of other/remote broker
    /**
     * The function subscribes new channels and unsubscribes no longer active channels by a list
     * received from other side.
     *
     * @param lst list of channels of other/remote broker. Note that argument is not const and can
     * be changed during processing (content is ordered)
     *
     * @note @b mt-safety: this method is mt-safe relative to other methods, but not mt-safe for calling
     * it from multiple threads
     *
     */
    void apply_their_channels(ChannelList lst);

    ///Forward message from other side to connected broker
    /**
     * @param msg message to forward
     *
     * @note @b mt-safety: this method is mt-safe
     */
    void forward_message(const Message &msg);

    ///Call this function if peer has been reset
    /**
     * If peer is reset, it is expected, that peer unsubscribed all channels, so we must resend current
     * list. This function assumes, that no channels are subscribed on peer and calls send_mine_channels()
     *
     * @note @b mt-safety: this method is not mt-safe
     */
    void peer_reset();

    ///Implement this function to send the list of channels to other side
    /**
     * @param channels list of channels
     *
     * @note @b mt-safety: this function must be mt-safe. It is called by send_mine_channels(),
     * so it depends on context of this call
     *
     */
    virtual void on_update_channels(const ChannelList &channels) noexcept = 0;
    ///Implement this function to send the message to the other side
    /**
     * @param message message to send
     * @param pm this argument is inherited from IListener, but in this case, it should be
     * always false. If the function is called with true, the message should not be send, because
     * it is private message for the bridge instance itself - however, the bridge doesn't have
     * private address in most of the cases
     *
     * @note @b mt-safety: this function must be mt-safe
     *
     */
    virtual void on_message(const Message &message, bool pm) noexcept override = 0;
    ///Called by broker when list of channels has been changed (channels added or removed)
    /**
     * Default implementation calls send_mine_channels(). If the bridge has a processing thread, it is
     * recommended to use this function to signal the processing thread to call send_mine_channels in
     * its context
     *
     * @note @b mt-safety: this function must be mt-safe
     */
    virtual void on_channels_update() noexcept override ;

protected:


    std::vector<char> _char_buffer = {};
    std::vector<ChannelID> _cur_channels = {};
    std::size_t _chan_hash = 0;

    static std::size_t hash_of_channel_list(const ChannelList &list);

    virtual bool on_message_dropped(IMQBroker::IListener *lsn, const IMQBroker::Message &msg) noexcept override;
};

///Implements direct bridge between two brokers
/**
 * This allows to connect two brokers directly. Messages sent to one broker are forwarded to other broker
 * and vice versa.
 *
 *
 * @note Do not create cycles!
 */
class MQDirectBridge {
public:

    ///ctor
    /**
     * @param b1 first broker
     * @param b2 second broker
     */
    MQDirectBridge(MQBroker b1, MQBroker b2);

protected:

    class Bridge: public MQAbstractBridge { // @suppress("Miss copy constructor or assignment operator")
    public:
        Bridge(MQDirectBridge &owner, MQBroker &&b):MQAbstractBridge(std::move(b)),_owner(owner) {}

        virtual void on_update_channels(const ChannelList &channels) noexcept override;
        virtual void on_message(const Message &message, bool pm) noexcept override;

    protected:
        MQDirectBridge &_owner;
    };


    Bridge _b1;
    Bridge _b2;

    Bridge &select_other(const Bridge &other);

    void on_update_chanels(const Bridge &source, const Bridge::ChannelList &channels);
    void on_message(const Bridge &source, const Bridge::Message &msg);
};


}
