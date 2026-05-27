#pragma once
#include "../request_sender_declaration.hpp"
#include "attorney_declaration.hpp"
#include "block/block.hpp"
#include "block/chain/header_chain.hpp"
#include "forks.hpp"
#include "block/chain/offender.hpp"
#include "communication/messages.hpp"
#include "communication/stage_operation/request.hpp"
#include "communication/stage_operation/result.hpp"
#include "eventloop/types/conndata.hpp"
#include "eventloop/types/peer_requests.hpp"
#include "focus.hpp"
#include "stage_state.hpp"
#include <array>
namespace HeaderDownload {
class LeaderInfo;
}

namespace BlockDownload {
enum class ServerCall {
    NO,
    STAGE_ADD,
    STAGE_SET
};

class Downloader {
    friend struct Focus;

public:
    Downloader(Attorney, size_t windowLength = 10);

    //////////////////////////////
    // Control functions
    [[nodiscard]] std::vector<ChainOffender> init(std::tuple<HeaderDownload::LeaderInfo, Headerchain, BanList>);
    void insert(Conref cr);
    [[nodiscard]] bool erase(Conref cr); // if true please get_reachable_totalwork()
    void set_min_worksum(const Worksum& ws);
    
    //////////////////////////////
    // Peer Callbacks
    void on_fork(Conref cr);
    void on_append(Conref cr);
    void on_rollback(Conref c);
    void on_blockreq_reply(Conref, BlockrepMsg&&, Blockrequest&);
    void on_blockreq_expire(Conref cr);
    void on_probe_reply(Conref cr, const ProbereqMsg&, const ProberepMsg&);
    void on_probe_expire(Conref cr);

    //////////////////////////////
    // Stage Callbacks
    [[nodiscard]] std::vector<ChainOffender> on_stage_result(stage_operation::Result&&);

    //////////////////////////////
    // Getters
    [[nodiscard]] std::optional<stage_operation::Operation> pop_stage();
    void do_block_requests(RequestSender);
    void do_probe_requests(RequestSender);
    const Worksum& get_reachable_totalwork() const { return reachableWork; }

    [[nodiscard]] auto forks_end() { return forks.end(); }
    [[nodiscard]] auto focus_end() { return focus.map_end(); }

    bool is_active(){return initialized;}

    void reset();
private:
    std::vector<ChainOffender> handle_stage_result(stage_operation::StageAddResult&&);
    std::vector<ChainOffender> handle_stage_result(stage_operation::StageSetResult&&);

    ServerCall next_stage_call();
    [[nodiscard]] stage_operation::StageAddOperation pop_stage_add();
    [[nodiscard]] stage_operation::StageSetOperation pop_stage_set();
    const Headerchain& headers() const;
    auto connections();
    bool update_reachable(bool reset = false); // returns whether reachable was actually updated
    bool can_do_requests();

    void check_upgrade_descripted(Conref cr);
    std::optional<Height> reachable_length();

private:
    Attorney attorney; // access chain

    // download target related
    Worksum reachableWork;
    Height reachableHeight { 0 };
    // Forkmap forks;
    Forks forks;

    // download focus related
    Focus focus;

    // state helper variables
    bool initialized = false;
    StageState stageState;
};

}
