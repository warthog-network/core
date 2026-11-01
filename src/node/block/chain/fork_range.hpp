#pragma once

#include "block/chain/batch_slot.hpp"
#include <limits>

struct Descripted;
class HeaderView;
class Headerchain;
class Grid;
struct GridView;

class ForkHeight {

public:
    ForkHeight(NonzeroHeight height, bool isforked)
        : height(height)
        , isforked(isforked)
    {
    }
    operator NonzeroHeight() const
    {
        return val();
    }
    bool forked() const { return isforked; }
    NonzeroHeight val() const
    {
        return height;
    }

private:
    NonzeroHeight height;
    bool isforked = false;
};

class ForkRange {
    // the default value for upper
    static constexpr NonzeroHeight upper_open { std::numeric_limits<uint32_t>::max() };

    // data
    // fork height (first different header) is in interval [l,u].
    NonzeroHeight l { 1u };
    NonzeroHeight u { upper_open };

public:
    struct Change {
    public:
        bool changedLower;
        bool changedUpper;
        static Change lower()
        {
            return { true, false };
        }
        static Change upper()
        {
            return { false, true };
        }
        static Change none()
        {
            return { false, false };
        }
    };
    ForkRange() { }
    ForkRange(const GridView& g1, const Grid& g2, Batchslot begin = Batchslot(0));
    ForkRange(NonzeroHeight lFork, NonzeroHeight uFork = upper_open)
        : l(lFork)
        , u(uFork)
    {
        assert(u >= l);
    }

    // chain modifications
    void on_fork(NonzeroHeight forkHeight, const Descripted& d, const Headerchain&); // throws
    void on_append(const Descripted&, const Headerchain&); // throws
    void on_append_or_shrink(const Descripted&, const Headerchain&); // throws
    void on_shrink(const Descripted&, const Headerchain&); // does not throw

    // match related
    Change on_match(Height matchHeight); // throws, returns change
    Change on_mismatch(NonzeroHeight mismatchHeight); // throws, returns change
    Change match(const Headerchain&, NonzeroHeight, HeaderView); // throws, returns change

    // getters

    // The upper() height is the height at which we certainly know the two chains are different.
    [[nodiscard]] NonzeroHeight upper() const { return u; }

    // The lower() height is 1 + the length up to which we certainly know the two chains are equal.
    [[nodiscard]] NonzeroHeight lower() const { return l; }
    [[nodiscard]] bool converged() const { return l == u; }
    [[nodiscard]] bool forked() const { return u != upper_open; }
    [[nodiscard]] uint32_t width() const
    {
        assert(u >= l);
        if (u == upper_open) {
            return upper_open.value();
        }
        return u - l;
    }

private:
    bool detect_shrink(const Descripted&, const Headerchain&);
    void initialize(Height lFork, Height uFork = upper_open);
    void grid_match(const GridView& g1, const Grid& g2, Batchslot begin);
    void on_fork(NonzeroHeight forkHeight);
};
