#pragma once

#include <chrono>

namespace quarkbot {

///Converts local wall clock timestamps to UTC, caching the current UTC offset
/**
    Data files carry local wall clock time with no offset, so every timestamp
    needs a time zone lookup. Looking up each row would be wasteful: a lookup
    result is valid for a whole offset interval (typically half a year), which
    the class caches together with the interval bounds. A single-day file
    therefore costs one lookup rather than one per row.

    Ambiguous local times (the repeated hour of an autumn DST transition)
    resolve to the earlier of the two instants. Local times that do not exist
    (the skipped hour of a spring transition) resolve using the offset in
    effect before the transition; such timestamps cannot occur in exchange
    trading data.
*/
class LocalTimeConverter {
public:

    ///construct for the given time zone
    /**
        @param tz time zone, must outlive the converter; zones returned by
            std::chrono::locate_zone are owned by the tzdb and always do
    */
    explicit LocalTimeConverter(const std::chrono::time_zone *tz):_tz(tz) {}

    ///convert a local timestamp to UTC
    std::chrono::system_clock::time_point to_sys(
            std::chrono::local_time<std::chrono::nanoseconds> lt) {
        auto ns = lt.time_since_epoch() - _offset;
        //compare in seconds: the sys_info bounds of the first and last offset
        //interval are sys_seconds min/max, which cannot be promoted to
        //nanoseconds without signed overflow
        auto secs = std::chrono::floor<std::chrono::seconds>(
                std::chrono::sys_time<std::chrono::nanoseconds>(ns));
        if (secs < _begin || secs >= _end) {
            auto info = _tz->get_info(lt);
            _offset = info.first.offset;
            _begin = info.first.begin;
            _end = info.first.end;
            ns = lt.time_since_epoch() - _offset;
        }
        return std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                std::chrono::sys_time<std::chrono::nanoseconds>(ns));
    }

protected:
    const std::chrono::time_zone *_tz;
    std::chrono::seconds _offset = {};
    //an empty interval, so the first conversion always performs a lookup
    std::chrono::sys_seconds _begin{std::chrono::seconds::max()};
    std::chrono::sys_seconds _end{std::chrono::seconds::min()};
};

}
