#pragma once
#include "api/types/all_fwd.hpp"
#include "block/chain/fork_range.hpp"
#include "block/header/view_inline.hpp"
#include "communication/messages.hpp"

struct ShrinkInfo {
    Height length;
    uint32_t distance;
    ShrinkInfo(Height length, uint32_t distance)
        : length(length)
        , distance(distance)
    {
    }
};

struct HeaderchainAppend {
    std::vector<SharedBatchView> completeBatches;
    SharedBatch finalPin;
    HeaderBatch incompleteBatch;
};

struct HeaderchainRollback {
    ShrinkInfo shrink;
    Descriptor descriptor;
};

struct HeaderchainFork {
    std::vector<SharedBatchView> completeBatches;
    SharedBatch finalPin;
    HeaderBatch incompleteBatch;
    ShrinkInfo shrink;
    Descriptor descriptor;
};

class HeaderchainSkeleton {
public:
    HeaderchainSkeleton(SharedBatch finalPin, HeaderBatch incompleteBatch)
        : finalPin(std::move(finalPin))
        , incompleteBatch(std::move(incompleteBatch)) { };

    wrt::optional<HeaderView> inefficient_search_header(NonzeroHeight h) const;
    Height length() const { return finalPin.upper_height() + incompleteBatch.size(); };
    HeaderSearchRecursive header_search_recursive() const
    {
        return { finalPin, incompleteBatch };
    }
    const HeaderBatch& incomplete_batch() { return incompleteBatch; }

protected:
    HeaderchainSkeleton() { };
    SharedBatch finalPin;
    HeaderBatch incompleteBatch;
};

class Headerchain;
[[nodiscard]] ForkHeight fork_height(const Headerchain& h1, const Headerchain& h2, NonzeroHeight startHeight = { 1u });
class Headerchain : public HeaderchainSkeleton {
    friend class HeaderVerifier;
    struct HeaderViewNoHash : public HeaderView {
        HeaderViewNoHash(const HeaderView& hv)
            : HeaderView(hv) { };
        Hash hash() = delete;
    };

public:
    struct pin_t {
        Headerchain& operator*() const
        {
            return **data;
        }
        Headerchain* operator->() const
        {
            return &**data;
        }
        operator bool() const
        {
            return data.use_count() != 0;
        }

        std::shared_ptr<std::shared_ptr<Headerchain>> data;
    };

    struct GridView {
        const std::vector<SharedBatchView>& vec;
        HeaderView operator[](Batchslot s) const { return vec[s.index()].getBatch().last(); }
        HeaderView operator[](size_t i) const { return vec[i].getBatch().last(); }
        size_t size() const { return vec.size(); }
        Batchslot slot_end() const { return Batchslot(size()); }
        GridView(const std::vector<SharedBatchView>& vec)
            : vec(vec) { };
    };
    // chain updates
    [[nodiscard]] HeaderchainAppend get_append(Height prevLength) const;
    [[nodiscard]] std::pair<Height, AppendMsg> apply_append(HeaderchainAppend&& append);
    [[nodiscard]] HeaderchainFork get_fork(ShrinkInfo shrink, Descriptor descriptor) const;
    [[nodiscard]] ForkMsg apply_fork(HeaderchainFork&& fork);

    void shrink(Height shrinkLength);
    uint64_t hashrate_at(Height h, uint32_t nblocks) const;
    uint64_t hashrate(uint32_t nblocks) const;
    api::HashrateBlockChart hashrate_block_chart(NonzeroHeight min, NonzeroHeight max, uint32_t nblocks) const;
    api::HashrateTimeChart hashrate_time_chart(uint32_t min, uint32_t max, uint32_t interval) const;

    size_t nonempty_batch_size() const { return completeBatches.size() + (incompleteBatch.size() > 0 ? 1 : 0); }
    HeaderBatch get_headers(HeaderRange) const;
    GridView grid_view() const { return completeBatches; }
    wrt::optional<HeaderView> get_header(Height) const;
    [[nodiscard]] Height length() const
    {
        return finalPin.upper_height() + incompleteBatch.size();
    }
    Headerchain() { };
    explicit Headerchain(HeaderchainSkeleton);
    Headerchain(Headerchain&& hc) = default;
    Headerchain(const Headerchain& hc) = default;
    Headerchain(const Headerchain& hc, Height subheight);
    Headerchain& operator=(const Headerchain&) = default;
    Headerchain& operator=(Headerchain&&) = default;
    const HeaderViewNoHash operator[](NonzeroHeight) const;
    void throw_out_of_bounds(Height h) const;
    Grid grid(Batchslot begin = Batchslot(0)) const;
    const HeaderBatch* operator[](Batchslot bs) const
    {
        size_t index = bs.index();
        if (index > completeBatches.size())
            return nullptr;
        if (index == completeBatches.size()) {
            return &incompleteBatch;
        }
        return &completeBatches[index].getBatch();
    };
    Worksum total_work() const { return worksum; }
    const std::vector<SharedBatchView>& complete_batches() const { return completeBatches; }
    [[nodiscard]] Worksum total_work_at(Height) const;
    [[nodiscard]] wrt::optional<BlockHash> get_hash(Height h) const
    {
        if (h > length())
            return {};
        if (length() == 0)
            return BlockHash::genesis();
        if (h == length())
            return static_cast<HeaderView>(operator[](h.nonzero_assert())).hash();
        return BlockHash(operator[]((h + 1).nonzero_assert()).prevhash());
    };
    [[nodiscard]] wrt::optional<PinHash> get_hash(PinHeight ph) const
    {
        if (auto h { get_hash(Height(ph)) })
            return PinHash { *h };
        return {};
    };

    [[nodiscard]] BlockHash hash_at(Height height) const
    {
        auto h = get_hash(height);
        assert(h);
        return *h;
    };
    [[nodiscard]] PinHash hash_at(PinHeight height) const
    {
        return PinHash(hash_at(Height(height)));
    };

    void clear();
    friend ForkHeight fork_height(const Headerchain& h1, const Headerchain& h2, NonzeroHeight startHeight);
    wrt::optional<NonzeroHeight> max_match_height(const HeaderSpan&) const;

protected: // methods
    void initialize_worksum();
    [[nodiscard]] Worksum sum_work(const NonzeroHeight begin, const NonzeroHeight end) const;

protected: // variables
    std::vector<SharedBatchView> completeBatches;
    Worksum worksum;
};
