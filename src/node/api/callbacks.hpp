#pragma once
#include "api/types/forward_declarations.hpp"
#include "general/result.hpp"
#include "wrt/expected.hpp"
#include "wrt/optional.hpp"
#include <cstdint>
#include <functional>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <variant>
#include <vector>

// forward declarations
struct TransactionMessage;
class Hash;
class PrivKey;
class TxHash;
class Grid;
class NonzeroHeight;
struct ChainMiningTask;
struct Error;
namespace HeaderDownload {
class Downloader;
}
namespace chainserver {
struct TransactionIds;
}

class Header;
struct TCPPeeraddr;

template <typename T>
using ResultCb = std::function<void(const Result<T>&)>;

using PeersCb = std::function<void(const std::vector<api::Peerinfo>&)>;
using IpCounterCb = std::function<void(const api::IPCounter&)>;
using ThrottledCb = std::function<void(const std::vector<api::ThrottledPeer>&)>;
using SyncedCb = std::function<void(bool)>;
using MempoolTxsCb = std::function<void(std::vector<wrt::optional<TransactionMessage>>&)>;
using SampledPeersCb = std::function<void(const std::vector<TCPPeeraddr>&)>;
using TransmissionCb = std::function<void(const api::TransmissionTimeseries&)>;
using ErrorCb = std::function<void(const wrt::optional<Error>&)>;
using ConnectedConnectionCb = std::function<void(const api::PeerinfoConnections&)>;

using WartBalanceCb = ResultCb<api::WartBalance>;
using TokenBalanceCb = ResultCb<api::TokenBalanceLookup>;
using JSONCb = ResultCb<nlohmann::json>;
using MempoolCb = ResultCb<api::MempoolEntries>;
using MempoolInsertCb = ResultCb<TxHash>;
using MempoolConstraintCb = ResultCb<api::MempoolUpdate>;
using ChainMiningCb = ResultCb<ChainMiningTask>;
using MiningCb = ResultCb<api::MiningState>;
using TxcacheCb = ResultCb<chainserver::TransactionIds>;
using HashrateCb = ResultCb<api::HashrateInfo>;
using HashrateBlockChartCb = ResultCb<api::HashrateBlockChart>;
using HashrateTimeChartCb = ResultCb<api::HashrateTimeChart>;
using HeadCb = ResultCb<api::Head>;
using ChainHeadCb = ResultCb<api::ChainHead>;
using RoundCb = ResultCb<api::Round16Bit>;
using HeaderdownloadCb = std::function<void(const HeaderDownload::Downloader&)>;
using HeaderCb = ResultCb<std::pair<NonzeroHeight, Header>>;
using BlockBinaryCb = ResultCb<api::BlockBinary>;
using HashCb = ResultCb<Hash>;
using GridCb = ResultCb<Grid>;
using TxCb = ResultCb<api::Transaction>;
using LatestTxsCb = ResultCb<api::TransactionsByBlocks>;
using TransactionMinfeeCb = ResultCb<api::TransactionMinfee>;
using BlockCb = ResultCb<api::Block>;
using HistoryCb = ResultCb<api::AccountHistory>;
using RichlistCb = ResultCb<api::RichlistInfo>;
using WalletCb = ResultCb<api::Wallet>;
using DBSizeCb = ResultCb<api::DBSize>;
using InfoCb = ResultCb<api::NodeInfo>;
