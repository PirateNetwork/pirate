// Copyright (c) 2021-2023 The Zcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#ifndef ZCASH_WALLET_ORCHARD_H
#define ZCASH_WALLET_ORCHARD_H

#include <array>

#include "primitives/transaction.h"
#include "transaction_builder.h"

// #include "rust/orchard/keys.h"
#include "rust/orchard/wallet.h"
// #include "zcash/address/orchard.hpp"
#include "zcash/address/pirate_orchard.hpp"
#include "zcash/IncrementalMerkleTree.hpp"

class OrchardWallet;
class OrchardWalletNoteCommitmentTreeWriter;
class OrchardWalletNoteCommitmentTreeLoader;

// class OrchardNoteMetadata
// {
// private:
//     OrchardOutPoint op;
//     libzcash::OrchardRawAddress address;
//     CAmount noteValue;
//     std::optional<libzcash::Memo> memo;
//     int confirmations;
// public:
//     OrchardNoteMetadata(
//         OrchardOutPoint op,
//         const libzcash::OrchardRawAddress& address,
//         CAmount noteValue,
//         const std::optional<libzcash::Memo>& memo):
//         op(op), address(address), noteValue(noteValue), memo(memo), confirmations(0) {}
//
//     const OrchardOutPoint& GetOutPoint() const {
//         return op;
//     }
//
//     const libzcash::OrchardRawAddress& GetAddress() const {
//         return address;
//     }
//
//     void SetConfirmations(int c) {
//         confirmations = c;
//     }
//
//     int GetConfirmations() const {
//         return confirmations;
//     }
//
//     CAmount GetNoteValue() const {
//         return noteValue;
//     }
//
//     const std::optional<libzcash::Memo>& GetMemo() const {
//         return memo;
//     }
// };

/**
 * A container and serialization wrapper for storing information derived from
 * a transaction that is relevant to restoring Orchard wallet caches.
 */
// class OrchardWalletTxMeta
// {
// private:
//     // A map from action index to the IVK belonging to our wallet that decrypts
//     // that action
//     std::map<uint32_t, libzcash::OrchardIncomingViewingKey> mapOrchardActionData;
//     // A vector of the action indices that spend notes belonging to our wallet
//     std::vector<uint32_t> vActionsSpendingMyNotes;
//
//     friend class OrchardWallet;
// public:
//     OrchardWalletTxMeta() {}
//
//     ADD_SERIALIZE_METHODS;
//
//     template <typename Stream, typename Operation>
//     inline void SerializationOp(Stream& s, Operation ser_action) {
//         int nVersion = s.GetVersion();
//         if (!(s.GetType() & SER_GETHASH)) {
//             READWRITE(nVersion);
//         }
//         READWRITE(mapOrchardActionData);
//         READWRITE(vActionsSpendingMyNotes);
//     }
//
//     const std::map<uint32_t, libzcash::OrchardIncomingViewingKey>& GetMyActionIVKs() const {
//         return mapOrchardActionData;
//     }
//
//     const std::vector<uint32_t>& GetActionsSpendingMyNotes() const {
//         return vActionsSpendingMyNotes;
//     }
//
//     bool empty() const {
//         return (mapOrchardActionData.empty() && vActionsSpendingMyNotes.empty());
//     }
//
//     friend bool operator==(const OrchardWalletTxMeta& a, const OrchardWalletTxMeta& b) {
//         return (a.mapOrchardActionData == b.mapOrchardActionData &&
//                 a.vActionsSpendingMyNotes == b.vActionsSpendingMyNotes);
//     }
//
//     friend bool operator!=(const OrchardWalletTxMeta& a, const OrchardWalletTxMeta& b) {
//         return !(a == b);
//     }
// };

// class OrchardActionSpend {
// private:
//     OrchardOutPoint outPoint;
//     libzcash::OrchardRawAddress receivedAt;
//     CAmount noteValue;
// public:
//     OrchardActionSpend(OrchardOutPoint outPoint, libzcash::OrchardRawAddress receivedAt, CAmount noteValue):
//         outPoint(outPoint), receivedAt(receivedAt), noteValue(noteValue) { }
//
//     OrchardOutPoint GetOutPoint() const {
//         return outPoint;
//     }
//
//     const libzcash::OrchardRawAddress& GetReceivedAt() const {
//         return receivedAt;
//     }
//
//     CAmount GetNoteValue() const {
//         return noteValue;
//     }
// };
//
// class OrchardActionOutput {
// private:
//     libzcash::OrchardRawAddress recipient;
//     CAmount noteValue;
//     std::optional<libzcash::Memo> memo;
//     bool isOutgoing;
// public:
//     OrchardActionOutput(
//             libzcash::OrchardRawAddress recipient,
//             CAmount noteValue,
//             std::optional<libzcash::Memo> memo,
//             bool isOutgoing):
//             recipient(recipient), noteValue(noteValue), memo(memo), isOutgoing(isOutgoing) { }
//
//     const libzcash::OrchardRawAddress& GetRecipient() const {
//         return recipient;
//     }
//
//     CAmount GetNoteValue() const {
//         return noteValue;
//     }
//
//     const std::optional<libzcash::Memo>& GetMemo() const {
//         return memo;
//     }
//
//     bool IsOutgoing() const {
//         return isOutgoing;
//     }
// };

// class OrchardActions {
// private:
//     std::map<uint32_t, OrchardActionSpend> spends;
//     std::map<uint32_t, OrchardActionOutput> outputs;
// public:
//     OrchardActions() {}
//
//     void AddSpend(uint32_t actionIdx, OrchardActionSpend spend) {
//         spends.insert({actionIdx, spend});
//     }
//
//     void AddOutput(uint32_t actionIdx, OrchardActionOutput output) {
//         outputs.insert({actionIdx, output});
//     }
//
//     const std::map<uint32_t, OrchardActionSpend>& GetSpends() {
//         return spends;
//     }
//
//     const std::map<uint32_t, OrchardActionOutput>& GetOutputs() {
//         return outputs;
//     }
// };

// const size_t SerializedOrchardMemoSize = 512;
// typedef std::array<unsigned char, SerializedOrchardMemoSize> OrchardMemo_t;

class OrchardDecryptedActions {
private:
  CAmount value;
  libzcash::OrchardPaymentAddressPirate address;
  std::array<unsigned char, ZC_MEMO_SIZE> memo;
  uint256 rho;
  uint256 rseed;
  std::optional<uint256> nullifier;
public:
    OrchardDecryptedActions() {}

    OrchardDecryptedActions(
      const CAmount value,
      const libzcash::OrchardPaymentAddressPirate address,
      const std::array<unsigned char, ZC_MEMO_SIZE> memo,
      const uint256 rho,
      const uint256 rseed,
      const std::optional<uint256> nullifier)
      : value(value), address(address), memo(memo), rho(rho), rseed(rseed), nullifier(nullifier)
    {}

    virtual ~OrchardDecryptedActions() {}

    libzcash::OrchardPaymentAddressPirate GetAddress() {
        return address;
    };

    CAmount GetValue() {
      return value;
    };

    uint256 GetRseed() {
      return rseed;
    };

    uint256 GetRho() {
      return rho;
    };

    std::optional<uint256> GetNullifier() {
      return nullifier;
    };

    std::array<unsigned char, ZC_MEMO_SIZE> GetMemo() {
      return memo;
    };

    static std::optional<OrchardDecryptedActions> AttemptDecryptOrchardAction(
        const orchard_bundle::Action* action,
        const libzcash::OrchardIncomingViewingKeyPirate ivk
    )
    {

        // Datastreams for serialization
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); // sending stream
        CDataStream rs(SER_NETWORK, PROTOCOL_VERSION); // returning stream

        // Tranfer Data
        libzcash::OrchardIncomingViewingKey_t ivk_t;
        libzcash::OrchardPaymentAddress_t address_t;
        std::array<unsigned char, ZC_MEMO_SIZE> memo_t;
        uint64_t value_t;
        uint256 rho_t;
        uint256 rseed_t;

        // rust result
        bool rustCompleted;

        // Serialize sending data
        ss << ivk;
        ss >> ivk_t;

        if(!try_orchard_decrypt_action_ivk(
            action->as_ptr(),
            ivk_t.begin(),
            &value_t,
            address_t.begin(),
            memo_t.begin(),
            rho_t.begin(),
            rseed_t.begin())) {
              return std::nullopt;
            }


        // deserialize returned data
        libzcash::OrchardPaymentAddressPirate address_r;
        rs << address_t;
        rs >> address_r;

        OrchardDecryptedActions ret = OrchardDecryptedActions(value_t, address_r, memo_t, rho_t, rseed_t, std::nullopt);

        return ret;
    };

    static std::optional<OrchardDecryptedActions> DecryptOrchardAction(
      const orchard_bundle::Action* action,
      const libzcash::OrchardFullViewingKeyPirate fvk
    ) {

      // Datastreams for serialization
      CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); // sending stream
      CDataStream rs(SER_NETWORK, PROTOCOL_VERSION); // returning stream

      // Tranfer Data
      libzcash::OrchardFullViewingKey_t fvk_t;
      libzcash::OrchardPaymentAddress_t address_t;
      std::array<unsigned char, ZC_MEMO_SIZE> memo_t;
      uint64_t value_t;
      uint256 rho_t;
      uint256 rseed_t;
      uint256 nullifier_t;

      // rust result
      bool rustCompleted;

      // Serialize sending data
      ss << fvk;
      ss >> fvk_t;

      if(!try_orchard_decrypt_action_fvk(
          action->as_ptr(),
          fvk_t.begin(),
          &value_t,
          address_t.begin(),
          memo_t.begin(),
          rho_t.begin(),
          rseed_t.begin(),
          nullifier_t.begin())) {
            return std::nullopt;
          }

      // deserialize returned data
      libzcash::OrchardPaymentAddressPirate address_r;
      rs << address_t;
      rs >> address_r;

      OrchardDecryptedActions ret = OrchardDecryptedActions(value_t, address_r, memo_t, rho_t, rseed_t, nullifier_t);

      return ret;

    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
      int nVersion = s.GetVersion();
      if (!(s.GetType() & SER_GETHASH)) {
         READWRITE(nVersion);
      }
      READWRITE(value);
      READWRITE(address);
      READWRITE(memo);
    }

};


class OrchardWallet
{
private:
    std::unique_ptr<OrchardWalletPtr, decltype(&orchard_wallet_free)> inner;

    // friend class ::orchard::UnauthorizedBundle;
    friend class OrchardWalletNoteCommitmentTreeWriter;
    friend class OrchardWalletNoteCommitmentTreeLoader;
public:
    OrchardWallet() : inner(orchard_wallet_new(), orchard_wallet_free) {}
    OrchardWallet(OrchardWallet&& wallet_data) : inner(std::move(wallet_data.inner)) {}
    OrchardWallet& operator=(OrchardWallet&& wallet)
    {
        if (this != &wallet) {
            inner = std::move(wallet.inner);
        }
        return *this;
    }

    // OrchardWallet should never be copied
    OrchardWallet(const OrchardWallet&) = delete;
    OrchardWallet& operator=(const OrchardWallet&) = delete;

    /**
     * Reset the state of the wallet to be suitable for rescan from the NU5 activation
     * height.  This removes all witness and spentness information from the wallet. The
     * keystore is unmodified and decrypted note, nullifier, and conflict data are left
     * in place with the expectation that they will be overwritten and/or updated in the
     * rescan process.
     */
    void Reset() {
        orchard_wallet_reset(inner.get());
    }

    /**
     * Overwrite the first bridge of the Orchard note commitment tree to have the
     * provided frontier as its latest state. This will fail with an assertion error
     * if any checkpoints exist in the tree.
     */
    void InitNoteCommitmentTree(const OrchardMerkleFrontier& frontier) {
        assert(!GetLastCheckpointHeight() >= 0);
        assert(frontier.inner->init_wallet(
            reinterpret_cast<merkle_frontier::OrchardWallet*>(inner.get())));

        LogPrint("orchardwallet","Initialized Orchard Commitment Tree with LastCheckpointHeight %i\n", GetLastCheckpointHeight());
    }

    /**
     * Checkpoint the note commitment tree. This returns `false` and leaves the note
     * commitment tree unmodified if the block height specified is not the successor
     * to the last block height checkpointed.
     */
    bool CheckpointNoteCommitmentTree(int nBlockHeight) {
        assert(nBlockHeight >= 0);
        return orchard_wallet_checkpoint(inner.get(), (uint32_t) nBlockHeight);
    }

    /**
     * Return whether the orchard note commitment tree contains any checkpoints.
     */
    int GetLastCheckpointHeight() const {
        uint32_t lastHeight{0};
        if (orchard_wallet_get_last_checkpoint(inner.get(), &lastHeight)) {
            return (int) lastHeight;
        }

        return -1;
    }

    /**
     * Rewinds to the most recent checkpoint, and marks as unspent any notes
     * previously identified as having been spent by transactions in the
     * latest block.
     */
    bool Rewind(int nBlockHeight, uint32_t& uResultHeight) {
        assert(nBlockHeight >= 0);
        return orchard_wallet_rewind(inner.get(), (uint32_t) nBlockHeight, &uResultHeight);
    }

    /**
    Clear the postions for a given Txid
    */
    bool ClearPositionsForTxid(const uint256 txid) {
        if (!clear_orchard_note_positions_for_txid(inner.get(), txid.begin())) {
            return false;
        }

        return true;
    }

    /**
     * Append each Sapling note commitment from the specified block to the
     * wallet's note commitment tree. Does not mark any notes for tracking.
     *
     * Returns `false` if the caller attempts to insert a block out-of-order.
     */
    bool AppendNoteCommitments(const int nBlockHeight, const CTransaction tx, const int txidx) {
        assert(nBlockHeight >= 0);

        if(tx.GetOrchardActionsCount()>0) {

            OrchardBundle orchardBundle = tx.GetOrchardBundle();

            if (!orchard_wallet_append_bundle_commitments(
                    inner.get(),
                    (uint32_t) nBlockHeight,
                    txidx,
                    orchardBundle.GetDetails().as_ptr()
                    )) {
                return false;
            }
        }

        return true;
    }

    /**
    Create an empty postions map for a given txid, to be populated by calling AppendNotCommitment
    */
    bool CreateEmptyPositionsForTxid(const int nBlockHeight, const uint256 txid) {
        if (!create_orchard_single_txid_positions(inner.get(), (uint32_t) nBlockHeight, txid.begin())) {
            return false;
        }

        return true;
    }

    /**
     *Call Create CreateEmptyPositionsForTxid before begining this function on any given tx
     *
     * Append a specific Sapling note commitment from the specified block/tx/output to the
     * wallet's note commitment tree. Marks the note for tracking if it is flagged as isMine
     *
     * Returns `false` if the caller attempts to insert a block out-of-order.
     */
    bool AppendNoteCommitment(const int nBlockHeight, const uint256 txid, int txidx, int outidx, const orchard_bundle::Action* action, bool isMine) {
        assert(nBlockHeight >= 0);

        if (!orchard_wallet_append_single_commitment(
                inner.get(),
                (uint32_t) nBlockHeight,
                txid.begin(),
                txidx,
                outidx,
                action->as_ptr(),
                isMine)) {
            return false;
        }

        return true;
    }

    uint256 GetLatestAnchor() const {
        uint256 value;
        // there is always a valid note commitment tree root at depth 0
        assert(orchard_wallet_commitment_tree_root(inner.get(), 0, value.begin()));
        return value;
    }

    bool UnMarkNoteForTransaction(const uint256 txid) {
        return orchard_wallet_unmark_transaction_notes(inner.get(),txid.begin());
    }

    bool IsNoteTracked(const uint256 txid, int outidx, uint64_t &position) {
        return orchard_is_note_tracked(inner.get(), txid.begin(), outidx, &position);
    }

    bool GetMerklePathOfNote(const uint256 txid, int outidx, libzcash::MerklePath &merklePath) {

        unsigned char serializedPath[1065] = {};
        if(!orchard_wallet_get_path_for_note(
              inner.get(),
              txid.begin(),
              outidx,
              serializedPath)) {
            return false;
        }

        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << serializedPath;
        ss >> merklePath;

        return true;
    }

    bool GetPathRootWithCMU(libzcash::MerklePath &merklePath, uint256 cmu, uint256 &anchor) {
        unsigned char serializedPath[1065] = {};
        unsigned char serializedAnchor[32] = {};
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << merklePath;
        ss >> serializedPath;

        if (!get_orchard_path_root_with_cm(serializedPath, cmu.begin(), serializedAnchor)) {
            return false;
        }

        CDataStream rs(SER_NETWORK, PROTOCOL_VERSION);
        rs << serializedAnchor;
        rs >> anchor;

        return true;
    }

    void GarbageCollect() {
        orchard_wallet_gc_note_commitment_tree(inner.get());
    }

};

class OrchardWalletNoteCommitmentTreeWriter
{
private:
    const OrchardWallet& wallet;
public:
    OrchardWalletNoteCommitmentTreeWriter(const OrchardWallet& wallet): wallet(wallet) {}

    template<typename Stream>
    void Serialize(Stream& s) const {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH)) {
            ::Serialize(s, nVersion);
        }
        RustStream rs(s);
        if (!orchard_wallet_write_note_commitment_tree(
                    wallet.inner.get(),
                    &rs, RustStream<Stream>::write_callback)) {
            throw std::ios_base::failure("Failed to serialize Orchard note commitment tree.");
        }
    }
};

class OrchardWalletNoteCommitmentTreeLoader
{
private:
    OrchardWallet& wallet;
public:
    OrchardWalletNoteCommitmentTreeLoader(OrchardWallet& wallet): wallet(wallet) {}

    template<typename Stream>
    void Unserialize(Stream& s) {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH)) {
            ::Unserialize(s, nVersion);
        }
        RustStream rs(s);
        if (!orchard_wallet_load_note_commitment_tree(
                    wallet.inner.get(),
                    &rs, RustStream<Stream>::read_callback)) {
            throw std::ios_base::failure("Failed to load Orchard note commitment tree.");
        }
    }
};

#endif // ZCASH_ORCHARD_WALLET_H
