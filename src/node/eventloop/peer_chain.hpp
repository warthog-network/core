#pragma once
#include "block/chain/fork_range.hpp"
#include "block/chain/signed_snapshot.hpp"
#include "block/chain/state.hpp"
#include "general/fwd.hpp"
#include <cstdint>
#include <memory>

class Headerchain;
struct InitMsgV1;
struct InitMsgV3;
struct AppendMsg;
struct ForkMsg;
struct ProberepMsg;
struct ProbereqMsg;
struct SignedPinRollbackMsg;
class StageAndConsensus;

enum class PeerchainMatch {
    NOMATCH,
    CONSENSUSMATCH,
    BLOCKDOWNLOADMATCH
};

class PeerChain {
public:
    void initialize(const InitMsgV1& parsed, const StageAndConsensus&);
    void initialize(const InitMsgV3& parsed, const StageAndConsensus&);

    // the following functions may throw ChainError
    void on_peer_append(const AppendMsg& msg, const StageAndConsensus&);
    void on_peer_fork(const ForkMsg& msg, const StageAndConsensus&);
    void on_peer_shrink(const SignedPinRollbackMsg& msg, const StageAndConsensus&);

    void on_consensus_fork(NonzeroHeight forkHeight, const StageAndConsensus&);
    void on_consensus_append(const StageAndConsensus&);
    void on_consensus_shrink(const StageAndConsensus&);

    void on_stage_fork(NonzeroHeight forkHeight, const Headerchain&);
    void on_stage_set(Height length);
    void on_stage_append_or_shrink(const Headerchain&);
    PeerchainMatch on_proberep(const ProbereqMsg&, const ProberepMsg& msg, const StageAndConsensus&);

    auto&& consensus_fork_range(this auto&& self) { return FWD(self).consensusForkRange; }
    auto&& stage_fork_range(this auto&& self) { return FWD(self).stageForkRange; }
    const auto& descripted() const { return desc; }

private:
    std::shared_ptr<Descripted> desc;
    ForkRange consensusForkRange;
    ForkRange stageForkRange;
    SignedSnapshot::Priority priority;
};
