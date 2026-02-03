#include "emit.hpp"
#include "api/http/endpoint.hpp"
#include "api/http/json.hpp"
#include "block/header/difficulty.hpp"
#include "general/hex.hpp"
#include "global/globals.hpp"
#include "mempool/updates.hpp"
#include "nlohmann/json.hpp"
#include "transport/connection_base.hpp"
using nlohmann::json;

#ifdef DISABLE_LIBUV
#include "api/wasm/api.hpp"
#else
namespace api {
void emit_connect_count(size_t)
{
}
void emit_connect(json)
{
}
void emit_disconnect(json)
{
}
void emit_chain(json)
{
}
void emit_mempool_add(json)
{
}
void emit_mempool_erase(json)
{
}
}
#endif

namespace api {
namespace event {
    void emit_connect(size_t total, const ConnectionBase& c)
    {

        return api::emit_connect({ { "total", total },
            { "id", c.id },
            { "since", c.created_at_timestmap() },
            { "inbound", c.inbound() },
            { "type", c.type_str() },
            { "address", c.peer_addr().to_string() } });
    }

    void emit_disconnect(size_t total, uint64_t id)
    {
        return api::emit_disconnect({ { "total", total }, { "id", id } });
    }

    void emit_chain_state(OnChainEvent e)
    {
        api::emit_chain(
            {
                { "length", e.length.value() },
                { "target", e.target.hex_string() },
                { "difficulty", e.target.difficulty() },
                { "worksum", e.totalWork.getdouble() },
                { "worksumHex", e.totalWork.to_string() },
            });
    }
    void emit_mempool_add(const mempool::Put& p, size_t total)
    {
        auto& e { p.entry };
        api::emit_mempool_add({
            { "total", total },
            { "id", e.txid().hex_string() },
            // { "fromAddress", e.from_address().to_string() },
            { "pinHeight", e.pin_height().value() },
            { "txHash", e.txhash },
            { "nonceId", e.nonce_id() },
            // { "feeStr", jsonmsg::to_json(e.fee().uncompact()) }, // TODO
            // { "toAddress", e.to_address().to_string() },
            // { "amount", jsonmsg::to_json(e.) },
        });
    }

    void emit_mempool_erase(const mempool::Erase& e, size_t total)
    {
        api::emit_mempool_erase({ { "id", e.id.hex_string() }, { "total", total } });
    }
    void emit_rollback(Height h)
    {
#ifndef DISABLE_LIBUV
        http_endpoint().push_event(api::Rollback(h));
#endif
    }
    void emit_block_append(api::Block&& b)
    {
#ifndef DISABLE_LIBUV
        http_endpoint().push_event(std::move(b));
#endif
    }
}
}
