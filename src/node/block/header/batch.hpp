#pragma once
#include "block/chain/pin.hpp"
#include "general/errors.hpp"
#include "general/fwd.hpp"
#include <span>

class Worksum;
class Writer;
class Headerchain;
class Headervec {
public:
    Headervec() { }
    Headervec(const std::vector<HeaderView>& v);
    Headervec(std::span<const uint8_t> s)
    {
        if (s.size() % HeaderView::bytesize)
            throw Error(EINV_HEADERVEC);
        if (s.size() > HeaderView::bytesize * HEADERBATCHSIZE)
            throw Error(EBATCHSIZE);
        bytes.assign(s.begin(), s.end());
    }
    Headervec(std::vector<uint8_t>&& b)
        : bytes(std::move(b))
    {
        assert(bytes.size() % 80 == 0);
    }
    Headervec(const uint8_t* begin, const uint8_t* end)
        : bytes(begin, end)
    {
        assert(begin <= end);
        assert(((end - begin) % 80) == 0);
    }
    void assign(const uint8_t* begin, const uint8_t* end)
    {
        assert(begin <= end);
        assert(((end - begin) % 80) == 0);
        bytes.assign(begin, end);
    }
    class const_iterator {
    public:
        const_iterator(const const_iterator&) = default;
        HeaderView operator*() { return HeaderView(pos); }
        const_iterator& operator++()
        {
            pos += HeaderView::size();
            return *this;
        }
        const_iterator operator+(size_t i)
        {
            return pos + i * HeaderView::size();
        }
        auto operator->() { return Helper { HeaderView { pos } }; }
        auto operator<=>(const const_iterator&) const = default;

    private:
        friend class Headervec;
        const_iterator(const uint8_t* pos)
            : pos(pos)
        {
        }
        struct Helper {
            HeaderView* operator->() { return &hv; }
            HeaderView hv;
        };
        const uint8_t* pos;
    };
    void assign(const_iterator begin, const_iterator end)
    {
        assign(begin.pos, end.pos);
    }
    const_iterator begin() const { return bytes.data(); }
    const_iterator end() const { return bytes.data() + bytes.size(); }

    void shrink(size_t elements)
    {
        size_t newsize = elements * 80;
        assert(newsize <= bytes.size());
        bytes.resize(newsize);
    }
    const std::vector<uint8_t>& raw() const { return bytes; }
    const uint8_t* data() const { return bytes.data(); }
    size_t size() const { return bytes.size() / 80; }
    inline HeaderView operator[](size_t i) const
    {
        auto pos = bytes.data() + i * 80;
        assert(bytes.size() >= (i + 1) * 80);
        return HeaderView(pos);
    }
    inline HeaderView last() const
    {
        assert(bytes.size() >= 80);
        return HeaderView(bytes.data() + size() * 80 - 1 * 80);
    }
    inline HeaderView first() const { return HeaderView(bytes.data()); }
    bool operator==(const Headervec& b) const { return bytes == b.bytes; }
    std::optional<HeaderView> get_header(size_t id) const
    {
        size_t offset = id * 80;
        if (bytes.size() < offset + 80)
            return {};
        return HeaderView { bytes.data() + offset };
    }
    void swap(Headervec& b) { bytes.swap(b.bytes); };
    HeaderView back() const
    {
        return HeaderView(bytes.data() + bytes.size() - 80);
    }
    void append(HeaderView hv)
    {
        bytes.insert(bytes.end(), hv.data(), hv.data() + 80);
    }
    void append(const Headervec& b)
    {
        bytes.insert(bytes.end(), b.bytes.begin(), b.bytes.end());
    }
    void clear() { bytes.clear(); }
    size_t byte_size() const
    {
        return bytes.size();
    }

    friend auto&& operator<<(RawSerializer auto&& s, const Headervec& hv){
        return FWD(s << hv.bytes);
    };


protected:
    friend class HeaderVecRegistry;
    std::vector<uint8_t> bytes;
};

class HeaderBatch : public Headervec {
public:
    using Headervec::Headervec;
    bool complete() const { return size() == HEADERBATCHSIZE; }
    Worksum worksum(Height offset, uint32_t maxElements = HEADERBATCHSIZE) const;
    bool valid_inner_links();
};

class HeaderSpan {
    struct HeightHeader : public HeaderView {
        const NonzeroHeight height;
        HeightHeader(HeaderView hv, NonzeroHeight h)
            : HeaderView(hv)
            , height(h)
        {
        }
    };
    struct Sentinel {
        const size_t numElements;
    };
    struct Iterator {
        Iterator(const HeaderSpan& b, size_t i)
            : b(b)
            , index(i)
        {
        }
        const HeaderSpan& b;
        uint32_t index;
        void operator++()
        {
            index += 1;
        }
        HeightHeader operator*() const
        {
            return { b.batch.get_header(index).value(),
                (b.batchOffset + index + 1).nonzero_assert() };
        }
        bool operator!=(Sentinel s)
        {
            return index != s.numElements;
        }
    };

    HeaderSpan(const HeaderSpan& hr, Height begin)
        : batchOffset(hr.batchOffset)
        , batch(hr.batch)
    {
        assert(begin >= begin_height());
        extraOffset = begin - begin_height();
    }

public:
    HeaderSpan sub_range(Height begin)
    {
        return { *this, begin };
    }
    HeaderSpan(Batchslot s, const HeaderBatch& b)
        : batchOffset(s.offset())
        , batch(b)
    {
    }
    auto begin() const
    {
        return Iterator(*this, extraOffset);
    }
    auto end() const
    {
        return Sentinel { batch.size() };
    }
    NonzeroHeight begin_height() const
    {
        return (offset() + 1).nonzero_assert();
    }
    NonzeroHeight end_height() const
    {
        return (batchOffset + (1 + uint32_t(batch.size()))).nonzero_assert();
    }

    auto at(NonzeroHeight h) const
    {
        if (h < begin_height() + extraOffset || h >= end_height())
            throw std::range_error("Invalid height " + std::to_string(h.value()) + " index in HeaderRange.");
        return batch[h - begin_height()];
    }
    Height offset() const { return batchOffset + extraOffset; }

private:
    const Height batchOffset;
    uint32_t extraOffset { 0 };
    const HeaderBatch& batch;
};

class Grid : public Headervec {
public:
    Grid(std::span<const uint8_t> s);
    Grid(Reader &r);
    Grid(const Headerchain&, Batchslot begin);
    friend Writer& operator<<(Writer&, const Grid&);
    using Headervec::operator[];
    HeaderView operator[](Batchslot s) const { return Headervec::operator[](s.index()); }
    Batchslot slot_begin() const
    {
        return Batchslot(0);
    }
    Batchslot slot_end() const
    {
        return Batchslot(Headervec::size());
    }
    [[nodiscard]] std::optional<ChainPin> back_pin() const
    {
        if (size() > 0) {
            return ChainPin { slot_end().offset(), last() };
        }
        return {};
    }
    bool valid_checkpoint() const;
};
