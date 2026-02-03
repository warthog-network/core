#include "parse.hpp"
#include "block/body/container.hpp"
#include "api/http/json_converter.hpp"
#include "block/header/header_impl.hpp"
#include "general/hex.hpp"
#include "nlohmann/json.hpp"
namespace {
using nlohmann::json;
template <typename T>
wrt::optional<T> get_optional(const json& j, std::string_view key)
{
    auto it { j.find(key) };
    if (it == j.end())
        return {};
    return it->get<T>();
}
}

using namespace nlohmann;
BlockWorker parse_block_worker(const std::vector<uint8_t>& s)
{
    try {
        json parsed = json::parse(s);

        auto height { Height(parsed.at("height").get<uint32_t>()).nonzero_throw(EBADHEIGHT) };
        Header header { HexRef(parsed.at("header").get<std::string>()) };
        VersionedBodyData bd { BodyData(HexRef(parsed.at("body").get<std::string>())), header.version() };
        BlockWorker mt {
            .block { height, header, Body::parse_throw(std::move(bd), height) },
            .worker { get_optional<std::string>(parsed, "worker").value_or(std::string()) }
        };
        return mt;
    } catch (const json::exception& e) {
        throw Error(EINV_ARGS);
    }
}

TransactionCreate parse_transaction_create(const std::vector<uint8_t>& s)
{
    try {
        auto parsed(nlohmann::json::parse(s));
        std::string type { parsed.at("type").get<std::string>() };
        return TransactionCreate::parse_from(type, parsed);
    } catch (const nlohmann::json::exception& e) {
        throw Error(ETXTYPE);
    }
}
