#include "storage.h"
#include <shared_mutex>
#include <map>
#include <unordered_map>
#include <queue>

namespace quarkbot {
class MemoryStorage: public IStorage {
public:

    virtual void begin_transaction() override;
    virtual void put_var(Timestamp,std::string_view name, std::string_view value) override;
    virtual void erase_var(Timestamp,std::string_view name) override;
    virtual void put_order(Timestamp,const Order &ord) override;
    virtual void put_fill(Timestamp,const Fill &fill) override;
    virtual void commit() override;
    virtual void rollback() override;
    virtual bool is_duplicate_fill(const Fill &fill) const override;
    virtual Fills load_fills(std::size_t limit, std::string_view filter = {}) const override;
    virtual Fills load_fills(Timestamp limit, std::string_view filter = {}) const override;
    virtual std::vector<SerializedOrder> load_open_orders(const Account &account) const override;
    virtual std::string get_var(std::string_view var_name) const override;
    virtual VarSet<std::string_view> get_vars(std::string_view prefix) const override;
    virtual VarSet<std::string_view> get_vars(std::string_view start, std::string_view end) const override;
    virtual Positions load_positions(std::string_view filter = {}) const override;
    virtual Trades load_closed(Timestamp limit, std::string_view filter = {}) const override;
    virtual void series_erase_points(std::string_view series_name, uint64_t index_and_less) override;
    virtual uint64_t series_add_point(std::string_view series_name, std::string_view point_data) override;
    virtual ValueStream<std::string_view> load_series(std::string_view name) const override;

protected:
    struct TxVar {
        std::string key;
        std::optional<std::string> value;

    };

    struct TxOrder {
        Account acc;
        SerializedOrder ord;
        bool erase;
    };

    struct Series {
        std::deque<std::string> data;
        std::uint64_t index = 0;
    };

    using Tx = std::variant<TxVar, TxOrder, Fill>;

    struct StoreAction;
    class VarSetDef;
    class ValueSetDef;

    mutable std::shared_mutex _mx;
    using VarMap = std::map<std::string, std::string, std::less<> >;
    using SeriesMap = std::unordered_map<std::string, Series>;
    VarMap _variables;
    SeriesMap _series;
    std::unordered_map<Account, std::unordered_map<std::string, std::string>, Account::Hasher > _orders;
    std::vector<Fill> _fills;
    std::vector<Tx> _tx;
    int _txlevel = 0;
    bool _batch_rollback = false;

    void auto_commit() {
        if (!_txlevel) commit();
    }
};
}
