#include "structured_reader.hpp"
#include "block/body/container.hpp"
#include "block/chain/height.hpp"
#include "general/is_testnet.hpp"
#include "spdlog/spdlog.h"

std::vector<uint8_t> MerkleLeaves::merkle_prefix() const
{
    const std::vector<Hash>* from;
    std::vector<Hash> tmp, to;
    from = &hashes;

    do {
        const size_t I { (from->size() + 1) / 2 };
        to.clear();
        to.reserve(I);
        size_t j = 0;
        for (size_t i = 0; i < I; ++i) {
            if (I == 1) {
                std::vector<uint8_t> res;
                std::copy((*from)[j].begin(), (*from)[j].end(), std::back_inserter(res));
                if (j + 1 < from->size())
                    std::copy((*from)[j + 1].begin(), (*from)[j + 1].end(), std::back_inserter(res));
                return res;
            }
            HasherSHA256 hasher {};
            hasher.write((*from)[j]);
            if (j + 1 < from->size()) {
                hasher.write((*from)[j + 1]);
            }

            to.push_back(std::move(hasher));
            j += 2;
        }
        std::swap(tmp, to);
        from = &tmp;
    } while (from->size() > 1);
    assert(false);
}

Hash MerkleLeaves::merkle_root(const BodyData& data, NonzeroHeight h) const
{
    const std::vector<Hash>* from;
    std::vector<Hash> tmp, to;
    from = &hashes;
    spdlog::info("MerkleLeaves {}", hashes.size());

    bool new_root_type = is_testnet() || h.value() >= NEWMERKLEROOT;
    bool block_v2 = is_testnet() || h.value() >= NEWBLOCKSTRUCUTREHEIGHT;
    if (new_root_type) {
        do {
            const size_t I { (from->size() + 1) / 2 };
            to.clear();
            to.reserve(I);
            size_t j = 0;
            for (size_t i = 0; i < I; ++i) {
                HasherSHA256 hasher {};
                hasher.write((*from)[j]);
                if (j + 1 < from->size())
                    hasher.write((*from)[j + 1]);
                if (I == 1)
                    hasher.write({ data.data(), block_v2 ? 10u : 4u });
                to.push_back(std::move(hasher));
                j += 2;
            }
            std::swap(tmp, to);
            from = &tmp;
        } while (from->size() > 1);
        return from->front();
    } else {
        bool includedSeed = false;
        bool finish = false;
        do {
            const size_t I { (from->size() + 1) / 2 };
            to.clear();
            to.reserve(I);
            if (from->size() <= 2 && !includedSeed) {

                HasherSHA256 hasher {};
                hasher.write((*from)[0]);
                if (1 < from->size()) {
                    hasher.write((*from)[1]);
                }
                hasher.write({ data.data(), 4u });
                includedSeed = true;
                to.push_back(std::move(hasher));
            } else {
                if (from->size() == 1)
                    finish = true;
                size_t j = 0;
                for (size_t i = 0; i < (from->size() + 1) / 2; ++i) {
                    HasherSHA256 hasher {};
                    hasher.write((*from)[j]);
                    if (j + 1 < from->size()) {
                        hasher.write((*from)[j + 1]);
                    }
                    to.push_back(std::move(hasher));
                    j += 2;
                }
            }
            std::swap(tmp, to);
            from = &tmp;
        } while (!finish);
        return from->front();
    }
}
