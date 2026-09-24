#pragma once

#include "general/with_uint64.hpp"
#include <chrono>


template <uint32_t seconds>
class RoundedTimestamp;
class Timestamp : public IsUint32 {

public:
    Timestamp(uint32_t v)
        : IsUint32(v)
    {
    }
    auto operator<=>(const Timestamp&) const = default;

    Timestamp operator-(uint32_t n) const
    {
        return val - n;
    }
    Timestamp operator-(std::chrono::system_clock::duration d)
    {
        using namespace std::chrono;
        return operator-(duration_cast<seconds>(d).count());
    }
    Timestamp operator+(uint32_t n) const
    {
        return val + n;
    }
    Timestamp operator+(std::chrono::system_clock::duration d) const
    {
        using namespace std::chrono;
        return operator+(duration_cast<seconds>(d).count());
    }
    static Timestamp now()
    {
        using namespace std::chrono;
        return Timestamp(duration_cast<seconds>(system_clock::now().time_since_epoch()).count());
    }
    static Timestamp from_time_point(std::chrono::steady_clock::time_point tp)
    {
        using namespace std::chrono;
        return duration_cast<seconds>((system_clock::now() + (tp - steady_clock::now())).time_since_epoch()).count();
    }
    Timestamp floor(uint32_t seconds) const{
        return { (value() / seconds) * seconds };
    }
    template <uint32_t seconds>
    RoundedTimestamp<seconds> floor() const;



    template <uint32_t seconds>
    RoundedTimestamp<seconds> ceil() const;

    std::chrono::steady_clock::time_point steady_clock_time_point() const
    {
        using namespace std::chrono;
        return steady_clock::now() + (system_clock::time_point(seconds(val)) - system_clock::now());
    }
    Timestamp operator+(std::chrono::steady_clock::duration d)
    {
        using namespace std::chrono;
        return Timestamp(val + duration_cast<seconds>(d).count());
    }
};

struct TimestampRange {
    Timestamp begin;
    Timestamp end;
};

template <uint32_t seconds>
class RoundedTimestamp : public Timestamp {
private:
    using Timestamp::Timestamp;
    friend class Timestamp;

public:
    static std::optional<RoundedTimestamp<seconds>> try_from_timestamp(Timestamp ts)
    {
        if (ts.value() % seconds == 0)
            return RoundedTimestamp<seconds> { ts };
        return {};
    }
    static RoundedTimestamp zero()
    {
        return { 0 };
    }
    RoundedTimestamp prev() const
    {
        return { value() - seconds };
    }
};

template <uint32_t seconds>
RoundedTimestamp<seconds> Timestamp::floor() const
{
    return { floor(seconds).value() };
}
template <uint32_t seconds>
RoundedTimestamp<seconds> Timestamp::ceil() const
{
    return { ((value() + seconds - 1) / seconds) * seconds };
}

struct Timepoint : public std::chrono::steady_clock::time_point {
    Timepoint(std::chrono::steady_clock::time_point tp)
        : std::chrono::steady_clock::time_point(tp)
    {
    }
    static Timepoint now()
    {
        return { std::chrono::steady_clock::now() };
    }
    Timestamp timestamp() const
    {
        using namespace std::chrono;
        return duration_cast<seconds>((system_clock::now() + (*this - steady_clock::now())).time_since_epoch()).count();
    }
};
