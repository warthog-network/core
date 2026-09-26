#include "consensus_headers.hpp"
#include "block/header/difficulty_scale.hpp"
#include "general/is_testnet.hpp"
#include "general/now.hpp"
#include "general/result.hpp"

HeaderVerifier::HeaderVerifier(const SharedBatch& b)
    : nextTarget(TargetV1())
    , finalHash(Hash::uninitialized())
{
    length = b.upper_height();
    HeaderView finalHeader { b.getBatch().last() };
    finalHash = finalHeader.hash();
    latestRetargetHeight = length.retarget_floor();

    // find latestRetarget header
    const SharedBatch* tmp = &b;
    while (tmp->lower_height() > latestRetargetHeight) {
        tmp = &tmp->prev();
    }
    HeaderView uhv = tmp->getBatch()[latestRetargetHeight - tmp->lower_height()];
    latestRetargetTime = uhv.timestamp();

    timeValidator.clear();
    bool override = false;
    if (!is_testnet()) {
        if (length == JANUSV1RETARGETSTART) {
            override = true;
            nextTarget = TargetV2::initial();
        }
        if (length == JANUSV2RETARGETSTART) {
            override = true;
            nextTarget = TargetV2::initialv2();
        }
    }
    if (!override) {
        if (latestRetargetHeight == 1) {
            if (is_testnet())
                nextTarget = TargetV2::genesis_testnet();
            else
                nextTarget = TargetV1::genesis();
        } else {
            nextTarget = finalHeader.target(length.nonzero_assert(), is_testnet());
            if (length.retarget_floor() == length) { // need retarget
                auto prevRetargetHeight = (length - 1).retarget_floor();
                while (tmp->lower_height() > prevRetargetHeight) {
                    tmp = &tmp->prev();
                }
                HeaderView lhv = tmp->getBatch()[prevRetargetHeight - tmp->lower_height()];
                nextTarget.scale(finalHeader.timestamp() - lhv.timestamp(),
                    BLOCKTIME * (length - prevRetargetHeight), length + 1);
            }
        }
    }
    static_assert(MEDIAN_N < HEADERBATCHSIZE);
    assert(MEDIAN_N < b.size());
    for (size_t i = 0; i < MEDIAN_N; ++i) {
        timeValidator.append(b.getBatch()[b.size() - MEDIAN_N + i].timestamp());
    }
}

std::expected<HeaderVerifier, ChainError> HeaderVerifier::copy_apply(const std::optional<SignedSnapshot>& sp, const HeaderSpan& hrange) const
{
    HeaderVerifier res { *this };
    assert(hrange.begin_height() == length + 1);
    for (auto h : hrange) {
        auto e { res.prepare_append(sp, h) };
        if (!e.has_value()) {
            return std::unexpected(ChainError(e.error(), h.height));
        }
        res.append(h.height, e.value());
    }
    return res;
}

HeaderVerifier::HeaderVerifier()
    : nextTarget(TargetV1::genesis())
    , finalHash(BlockHash::genesis())
{
    if (is_testnet()) {
        nextTarget = TargetV2::genesis_testnet();
    }
    length = Height(0);
    latestRetargetHeight = Height(0);
    latestRetargetTime = 0;
    timeValidator.clear();
}

void HeaderVerifier::append(NonzeroHeight newlength, const PreparedAppend& p)
{
    assert(newlength == length + 1);
    length = newlength;
    // adjust height and hash
    finalHash = p.hash;

    // adjust timestamp validator
    const uint32_t timestamp = p.hv.timestamp();
    assert(timestamp != 0);
    timeValidator.append(timestamp);

    // adjust next Target
    const Height upperHeight = newlength.retarget_floor();
    static_assert(::retarget_floor(JANUSV1RETARGETSTART) == JANUSV1RETARGETSTART);
    if (upperHeight == newlength) { // need retarget
        bool override = false;
        if (!is_testnet()) {
            if (length == JANUSV1RETARGETSTART) {
                override = true;
                nextTarget = TargetV2::initial();
            }
            if (length == JANUSV2RETARGETSTART) {
                override = true;
                nextTarget = TargetV2::initialv2();
            }
        }
        if (!override) {
            if (upperHeight != 1) {
                assert(latestRetargetHeight != 0);
                assert(latestRetargetTime != 0);
                assert(latestRetargetTime < timestamp);
                Height lowerHeight((upperHeight - 1).retarget_floor());
                assert(upperHeight - lowerHeight > 0);
                nextTarget.scale(timestamp - latestRetargetTime,
                    BLOCKTIME * (upperHeight - lowerHeight), newlength);
            }
        }
        latestRetargetHeight = upperHeight;
        latestRetargetTime = timestamp;
    }
}

auto HeaderVerifier::prepare_append(const std::optional<SignedSnapshot>& sp, HeaderView hv, bool verifyPOW) const -> Result<PreparedAppend>
{
    auto hash { hv.hash() };
    NonzeroHeight appendHeight { height() + 1 };

    // Check header link
    if (hv.prevhash() != finalHash)
        return Error(EHEADERLINK);

    // // Check version
    auto powVersion { POWVersion::from_params(appendHeight, hv.version(), is_testnet()) };
    if (!powVersion) {
        return Error(EBLOCKVERSION);
    }

    // Check difficulty
    if (hv.target(appendHeight, is_testnet()) != nextTarget)
        return Error(EDIFFICULTY);

    // Check POW
    if (verifyPOW && !hv.validPOW(hash, *powVersion)) {
        return Error(EPOW);
    }

    // Check signed pin
    if (sp && length + 1 == sp->priority.height && sp->hash != hash)
        return Error(ELEADERMISMATCH);

    const uint32_t t = hv.timestamp();

    // Check increasing median
    // Check no time drops (should be automatically valid if no future times)
    if (!timeValidator.valid(t)
        || latestRetargetTime >= t)
        return Error(ETIMESTAMP);

    // Check no future block times
    // LATER: use network time
    if (t > now_timestamp() + TOLERANCEMINUTES * 60)
        return Error(ECLOCKTOLERANCE);
    return PreparedAppend { hv, hash };
}

HeaderVerifier::HeaderVerifier(const Headerchain& hc, Height length)
    : HeaderVerifier()
{
    assert(length <= hc.length());
    finalHash = hc.hash_at(length);

    this->length = length;

    //////////////////////////////
    // time validator
    //////////////////////////////
    timeValidator.clear();
    for (auto h(Height(length.value() > timeValidator.N ? length.value() - timeValidator.N : 1).nonzero_assert());
        h < length; ++h) {
        timeValidator.append(hc[h].timestamp());
    }

    //////////////////////////////
    // initialize target
    //////////////////////////////
    Height upperHeight = length.retarget_floor();
    static_assert(JANUSV1RETARGETSTART > 1);
    if (length == 0) {
        if (is_testnet()) {
            nextTarget = TargetV2::genesis_testnet();
        } else {
            nextTarget = TargetV1::genesis();
        }
        latestRetargetHeight = Height(0);
        latestRetargetTime = 0;
    } else {
        latestRetargetHeight = upperHeight;
        auto upper { hc.get_header(upperHeight).value() };
        latestRetargetTime = upper.timestamp();

        // assign nextTarget
        bool override = false;
        if (!is_testnet()) {
            if (length == JANUSV1RETARGETSTART) {
                override = true;
                nextTarget = TargetV2::initial();
            }
            if (length == JANUSV2RETARGETSTART) {
                override = true;
                nextTarget = TargetV2::initialv2();
            }
        }
        if (!override) {
            nextTarget = hc.get_header(length).value().target(length.nonzero_assert(), is_testnet());
            if (upperHeight != 1 && upperHeight == length) {
                Height lowerHeight = (upperHeight - 1).retarget_floor();
                assert(upperHeight - lowerHeight > 0);
                auto lower { hc.get_header(lowerHeight).value() };
                nextTarget.scale(latestRetargetTime - lower.timestamp(),
                    BLOCKTIME * (upperHeight - lowerHeight), length + 1);
            }
        }
    }
}

void ExtendableHeaderchain::initialize()
{
    initialize_worksum();
    checker = { *this, length() };
}

void ExtendableHeaderchain::shrink(Height newlength)
{
    assert(newlength <= length());
    size_t numComplete = newlength.complete_batches();
    if (numComplete == completeBatches.size()) {
        // only need to shrink incompleteBatch
        incompleteBatch.shrink(newlength.incomplete_batch_size());
    } else {
        incompleteBatch = completeBatches[numComplete].getBatch();
        incompleteBatch.shrink(newlength.incomplete_batch_size());
        completeBatches.erase(completeBatches.begin() + numComplete,
            completeBatches.end());
        if (completeBatches.size() > 0)
            finalPin = completeBatches.back();
        else
            finalPin = SharedBatch();
    }
    initialize();
}

ExtendableHeaderchain::ExtendableHeaderchain()
{
    initialize();
}

ExtendableHeaderchain::ExtendableHeaderchain(
    std::vector<HeaderBatch>&& init,
    BatchRegistry& br)
{
    // p.first
    Height incompleteHeightOffset { 0 };
    Worksum totalWork;
    for (size_t i = 0; i < init.size(); ++i) {
        incompleteBatch = std::move(init[i]);
        if (incompleteBatch.complete()) {
            finalPin = br.share(std::move(incompleteBatch), finalPin);
            completeBatches.push_back(finalPin);
            incompleteBatch.clear();
            incompleteHeightOffset = finalPin.upper_height();
            totalWork = finalPin.total_work();
        }
    }
    totalWork += incompleteBatch.worksum(incompleteHeightOffset);
    initialize();
    assert(totalWork == total_work());
}

ExtendableHeaderchain::ExtendableHeaderchain(const Headerchain& eh,
    Height height)
    : Headerchain(eh, height)
{
    initialize();
}
ExtendableHeaderchain::ExtendableHeaderchain(Headerchain&& hc)
    : Headerchain(std::move(hc))
{
    initialize();
}

ExtendableHeaderchain& ExtendableHeaderchain::operator=(Headerchain&& hc)
{
    Headerchain::operator=(std::move(hc));
    initialize();
    return *this;
}

void ExtendableHeaderchain::append(const HeaderVerifier::PreparedAppend& p,
    BatchRegistry& br)
{
    worksum += checker.next_target();
    incompleteBatch.append(p.hv);
    if (incompleteBatch.complete()) {
        finalPin = br.share(std::move(incompleteBatch), finalPin, worksum);
        completeBatches.push_back(finalPin);
        incompleteBatch.clear();
    }
    checker.append(length().nonzero_assert(), p);
}

auto ExtendableHeaderchain::prepare_append(const std::optional<SignedSnapshot>& sp, HeaderView hv, bool verifyPOW) const -> Result<HeaderVerifier::PreparedAppend>
{
    return checker.prepare_append(sp, hv, verifyPOW);
}

MiningData ExtendableHeaderchain::mining_data() const
{
    return {
        (length() + 1).reward(),
        final_hash(),
        next_target(),
        checker.get_valid_timestamp()
    };
}
