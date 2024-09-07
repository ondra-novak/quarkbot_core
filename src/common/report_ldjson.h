#pragma once
#include "storage.h"

#include <json20.h>

namespace quarkbot {


class ReportLDJSON: public IStorage {
public:

    ReportLDJSON(IStorage *storage, std::ostream &out):_storage(storage),_out(out) {}

    virtual void rollback() override;
    virtual std::string get_var(std::string_view var_name) const override;
    virtual void begin_transaction() override;
    virtual void commit() override;
    virtual Fills load_fills(std::size_t limit,
            std::string_view filter) const override;
    virtual void put_var(Timestamp tm, std::string_view name, std::string_view value)
            override;
    virtual bool is_duplicate_fill(const Fill &fill) const override;
    virtual VarSet<std::string_view> get_vars(std::string_view prefix) const override;
    virtual VarSet<std::string_view> get_vars(std::string_view start, std::string_view end) const override;
    virtual void put_fill(Timestamp tm,const Fill &fill) override;
    virtual void erase_var(Timestamp tm,std::string_view name) override;
    virtual Fills load_fills(Timestamp limit,
            std::string_view filter) const override;
    virtual Positions load_positions(std::string_view filter) const
            override;
    virtual std::vector<SerializedOrder> load_open_orders(
            const Account &account) const override;
    virtual Trades load_closed(Timestamp limit,
            std::string_view filter) const override;
    virtual void put_order(Timestamp tm,const Order &ord) override;

protected:

    IStorage *_storage;
    std::ostream &_out;
    std::vector<json::value> _tx;
    void tx_beg();
    void tx_end();
    void tx_rollback();
    unsigned int _txcnt = 0;

    static json::value new_record(Timestamp tm, std::string_view type, json::value payload);
    static json::value order_to_json(const Order &ord);
};

}
