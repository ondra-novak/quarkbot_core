#include "storage.h"
#include <leveldb/db.h>
#include <leveldb/write_batch.h>
#include <unordered_map>
#include <vector>


namespace quarkbot {
class LvlDBStorage: public IStorage {
public:

    class RecordType {
    public:
        enum Type: char {
            variable='V',
            order='O',
            fill='F',
            series='S'
        };
        constexpr RecordType(Type x):_val(x) {}
        constexpr RecordType(char c):_val(static_cast<Type>(c)) {}
        constexpr bool operator==(const RecordType &) const = default;
        std::string_view to_string() const;
        constexpr operator char() const {return static_cast<char>(_val);}
    protected:
        Type _val;
    };

    LvlDBStorage(std::shared_ptr<leveldb::DB> db, std::string key_pfx);
    virtual void begin_transaction() override;
    virtual void put_var(Timestamp event_time, std::string_view name, std::string_view value) override;
    virtual void erase_var(Timestamp event_time, std::string_view name) override;
    virtual void put_order(Timestamp event_time, const Order &ord) override;
    virtual void put_fill(Timestamp event_time, const Fill &fill) override;
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
    std::shared_ptr<leveldb::DB> _db;
    std::string _key_pfx;
    leveldb::WriteBatch _batch;
    leveldb::WriteOptions _write_opts;
    unsigned int _txlevel;
    bool _batch_rollback = false;
    std::string _buffer;

    struct SeriesState {
        std::uint64_t first_point = 0;
        std::uint64_t last_point = 1;
    };

    std::unordered_map<std::string, SeriesState> _series_state;
    std::vector<std::pair<SeriesState *, SeriesState> > _series_state_rollback_data;

    void auto_commit() {
        if (!_txlevel) commit();
    }
    const std::string &build_key(RecordType type, const std::string_view &rest);
    const std::string &build_fill_key(Timestamp tm, std::string_view id);
    const std::string &build_series_key(const std::string_view &name, std::uint64_t index);
    const std::string &build_key(std::string &buff, RecordType type, const std::string_view &rest) const;
    const std::string &build_fill_key(std::string &buff, Timestamp tm, std::string_view id) const;
    const std::string &build_series_key(std::string &buff, const std::string_view &name, std::uint64_t index) const;
    const std::string &build_series_key(std::string &buff, const std::string_view &name) const;
    static bool key_match_prefix(const std::string_view &pfx, const leveldb::Slice &slice);
    std::string_view remove_key_prefix(const leveldb::Slice &slice) const;
    static std::string_view extract_slice(const leveldb::Slice &slice);
    SeriesState load_series_state_from_db(std::string_view name) const;
    SeriesState &get_series_state(const std::string &name);


};
}
