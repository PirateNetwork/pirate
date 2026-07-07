// Copyright (c) 2021-2023 The Zcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#ifndef ZCASH_PRIMITIVES_IRONWOOD_H
#define ZCASH_PRIMITIVES_IRONWOOD_H

#include "streams.h"
#include "streams_rust.h"

#include <amount.h>

#include <rust/bridge.h>

class IronwoodMerkleFrontier;
class IronwoodWallet;
namespace ironwood { class UnauthorizedBundle; }

/**
 * The Ironwood component of an authorized transaction.
 */
class IronwoodBundle
{
private:
    /// An optional Ironwood bundle.
    /// Memory is allocated by Rust.
    rust::Box<ironwood_bundle::IronwoodBundle> inner;

    IronwoodBundle(IronwoodBundlePtr* bundle) : inner(ironwood_bundle::from_raw_box(bundle)) {}

    friend class IronwoodMerkleFrontier;
    friend class IronwoodWallet;
    friend class ironwood::UnauthorizedBundle;
public:
    IronwoodBundle() : inner(ironwood_bundle::none()) {}

    IronwoodBundle(IronwoodBundle&& bundle) : inner(std::move(bundle.inner)) {}

    IronwoodBundle(const IronwoodBundle& bundle) :
        inner(bundle.inner->box_clone()) {}

    IronwoodBundle& operator=(IronwoodBundle&& bundle)
    {
        if (this != &bundle) {
            inner = std::move(bundle.inner);
        }
        return *this;
    }

    IronwoodBundle& operator=(const IronwoodBundle& bundle)
    {
        if (this != &bundle) {
            inner = bundle.inner->box_clone();
        }
        return *this;
    }

    const ironwood_bundle::IronwoodBundle& GetDetails() const {
        return *inner;
    }

    size_t RecursiveDynamicUsage() const {
        return inner->recursive_dynamic_usage();
    }

    template<typename Stream>
    void Serialize(Stream& s) const {
        try {
            inner->serialize(*ToRustStream(s));
        } catch (const std::exception& e) {
            throw std::ios_base::failure(e.what());
        }
    }

    template<typename Stream>
    void Unserialize(Stream& s, uint32_t consensusBranchId) {
        try {
            inner = ironwood_bundle::parse(*ToRustStream(s), consensusBranchId);
        } catch (const std::exception& e) {
            throw std::ios_base::failure(e.what());
        }
    }

    /// Returns true if this contains an Ironwood bundle, or false if there is no
    /// Ironwood component.
    bool IsPresent() const { return inner->is_present(); }

    /// Returns the net value entering or exiting the Ironwood pool as a result of this
    /// bundle.
    CAmount GetValueBalance() const {
        return inner->value_balance_zat();
    }

    /// Queues this bundle's authorization for validation.
    ///
    /// `sighash` must be for the transaction this bundle is within.
    void QueueAuthValidation(
        ironwood::BatchValidator& batch, const uint256& sighash) const
    {
        batch.add_bundle(inner->box_clone(), sighash.GetRawBytes());
    }

    const size_t GetNumActions() const {
        return inner->num_actions();
    }

    const std::vector<uint256> GetNullifiers() const {
        const auto actions = inner->actions();
        std::vector<uint256> result;
        result.reserve(actions.size());
        for (const auto& action : actions) {
            result.push_back(uint256::FromRawBytes(action.nullifier()));
        }
        return result;
    }

    const std::optional<uint256> GetAnchor() const {
        if (IsPresent()) {
            return uint256::FromRawBytes(inner->anchor());
        } else {
            return std::nullopt;
        }
    }

    bool OutputsEnabled() const {
        return inner->enable_outputs();
    }

    bool SpendsEnabled() const {
        return inner->enable_spends();
    }

    bool CoinbaseOutputsAreValid() const {
        return inner->coinbase_outputs_are_valid();
    }
};

#endif // ZCASH_PRIMITIVES_IRONWOOD_H
