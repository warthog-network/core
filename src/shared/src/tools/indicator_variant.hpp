#include "general/reader.hpp"
#include "general/writer.hpp"
#include "wrt/variant.hpp"
#include <expected>
namespace wrt {
template <typename parse_exception_generator_t, typename... Ts>
struct indicator_variant : public wrt::variant<Ts...> {
private:
    static constexpr auto Max(auto const& value, auto const&... args)
    {
        if constexpr (sizeof...(args) == 0u) // Single argument case!
            return value;
        else {
            const auto max = Max(args...);
            return value > max ? value : max;
        }
    }

    using expected_t = std::expected<indicator_variant, uint8_t>;

    [[nodiscard]] static expected_t try_parse_expected(Reader& r)
    {
        std::optional<indicator_variant> o;
        auto i { r.uint8() };
        if (([&] {
                if (i == Ts::INDICATOR) {
                    o.template emplace<Ts>(r);
                    return true;
                }
                return false;
            }() || ...))
            return *o;
        return std::unexpected(i);
    }

public:
    static constexpr const size_t max_byte_size = Max(Ts::byte_size()...);
    [[nodiscard]] static std::optional<indicator_variant> try_parse(Reader& r)
    {
        if (expected_t e { try_parse_expected(r) })
            return *e;
        else
            return e.error();
    }
    [[nodiscard]] static indicator_variant parse_throw(Reader& r)
    {
        if (expected_t o { try_parse_expected(r) })
            return *o;
        else
            throw parse_exception_generator_t::generate(o.error());
    }
    static consteval bool ascending_indicators()
    {
        constexpr uint8_t values[] = { Ts::INDICATOR... };
        for (size_t i = 1; i < sizeof...(Ts); ++i) {
            if (!(values[i - 1] < values[i]))
                return false;
        }
        return true;
    }
    static_assert(ascending_indicators()); // indicators must be in ascending order to ensure they are all different


    using wrt::variant<Ts...>::variant;
    using parent_t1 = wrt::variant<Ts...>;
    indicator_variant(wrt::variant<Ts...> v)
        : wrt::variant<Ts...>(std::move(v))
    {
    }
    indicator_variant(Reader& r)
        : indicator_variant(parse_throw(r))
    {
    }
    size_t byte_size() const
    {
        return 1 + this->visit([](auto& t) { return t.byte_size(); });
    }
    void serialize(RawSerializer auto& s) const
    {
        this->visit([&](auto& v) {
            s << v.INDICATOR << v;
        });
    }
    std::vector<uint8_t> to_bytes() const
    {
        std::vector<uint8_t> out;
        out.resize(byte_size());
        Writer w(out);
        w << *this;
        assert(w.remaining() == 0);
        return out;
    }
};

}
