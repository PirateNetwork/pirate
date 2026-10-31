// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <gtest/gtest.h>

#include "gtest/gtestutils.h"
#include "random.h"
#include "rpc/server.h"
#include "script/interpreter.h"
#include "script/standard.h"
#include "transaction_builder.h"
#include "wallet/walletdb.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <vector>

enum class WitnessPool { Sapling, Ironwood };

class WitnessTestWallet : public CWallet
{
public:
    explicit WitnessTestWallet(const std::string& filename) : CWallet(filename) {}
    std::optional<libzcash::SaplingPaymentAddress> testSaplingAddress;
    std::optional<libzcash::IronwoodPaymentAddress> testIronwoodAddress;

    uint256 Anchor(WitnessPool pool) const
    {
        return pool == WitnessPool::Sapling ? saplingWallet.GetLatestAnchor() : ironwoodWallet.GetLatestAnchor();
    }

    int Checkpoint(WitnessPool pool) const
    {
        return pool == WitnessPool::Sapling ? saplingWallet.GetLastCheckpointHeight() : ironwoodWallet.GetLastCheckpointHeight();
    }

    bool Validated(WitnessPool pool) const
    {
        return pool == WitnessPool::Sapling ? saplingWalletValidated : ironwoodWalletValidated;
    }

    void Process(WitnessPool pool, const CBlockIndex* index, const CBlock& block)
    {
        LOCK(cs_wallet);
        if (pool == WitnessPool::Sapling) ProcessSaplingBlockTransactions(index, &block);
        else ProcessIronwoodBlockTransactions(index, &block);
    }

    void Increment(WitnessPool pool, const CBlockIndex* index, const CBlock& block)
    {
        LOCK(cs_wallet);
        if (pool == WitnessPool::Sapling) IncrementSaplingWallet(index, &block, true);
        else IncrementIronwoodWallet(index, &block, true);
    }

    void Rewind(WitnessPool pool, const CBlockIndex* removed)
    {
        LOCK(cs_wallet);
        if (pool == WitnessPool::Sapling) DecrementSaplingWallet(removed);
        else DecrementIronwoodWallet(removed);
    }

    bool Validate(WitnessPool pool, const CBlockIndex* index)
    {
        LOCK(cs_wallet);
        return pool == WitnessPool::Sapling ? ValidateSaplingWalletTrackedPositions(index, true) :
            ValidateIronwoodWalletTrackedPositions(index, true);
    }

    bool Path(WitnessPool pool, const uint256& txid, int output, libzcash::MerklePath& path)
    {
        return pool == WitnessPool::Sapling ? saplingWallet.GetMerklePathOfNote(txid, output, path) :
            ironwoodWallet.GetMerklePathOfNote(txid, output, path);
    }

    bool TrackedPosition(WitnessPool pool, const uint256& txid, int output, uint64_t& position)
    {
        return pool == WitnessPool::Sapling ? saplingWallet.IsNoteTracked(txid, output, position) :
            ironwoodWallet.IsNoteTracked(txid, output, position);
    }

    bool PathRoot(WitnessPool pool, libzcash::MerklePath& path, const uint256& commitment, uint256& root)
    {
        return pool == WitnessPool::Sapling ? saplingWallet.GetPathRootWithCMU(path, commitment, root) :
            ironwoodWallet.GetPathRootWithCMU(path, commitment, root);
    }

    std::vector<unsigned char> SaveTree(WitnessPool pool) const
    {
        CDataStream stream(SER_DISK, CLIENT_VERSION);
        if (pool == WitnessPool::Sapling) stream << SaplingWalletNoteCommitmentTreeWriter(saplingWallet);
        else stream << IronwoodWalletNoteCommitmentTreeWriter(ironwoodWallet);
        return {stream.begin(), stream.end()};
    }

    void LoadTree(WitnessPool pool, const std::vector<unsigned char>& bytes)
    {
        CDataStream stream(bytes, SER_DISK, CLIENT_VERSION);
        if (pool == WitnessPool::Sapling) {
            SaplingWalletNoteCommitmentTreeLoader loader(saplingWallet);
            stream >> loader;
        } else {
            IronwoodWalletNoteCommitmentTreeLoader loader(ironwoodWallet);
            stream >> loader;
        }
    }

    void InitFrontier(WitnessPool pool, SaplingMerkleFrontier sapling, IronwoodMerkleFrontier ironwood, bool corrupt = false)
    {
        if (pool == WitnessPool::Sapling) {
            if (corrupt) sapling.append(uint256S("03"));
            saplingWallet.InitNoteCommitmentTree(sapling);
        } else {
            if (corrupt) ironwood.append(uint256S("03"));
            ironwoodWallet.InitNoteCommitmentTree(ironwood);
        }
    }
};

class WitnessCountingCoinsView : public CCoinsViewCache
{
public:
    explicit WitnessCountingCoinsView(CCoinsView* base) : CCoinsViewCache(base) {}
    mutable int frontierReads = 0;

    bool GetSaplingFrontierAnchorAt(const uint256& root, SaplingMerkleFrontier& tree) const override
    {
        ++frontierReads;
        return CCoinsViewCache::GetSaplingFrontierAnchorAt(root, tree);
    }

    bool GetIronwoodFrontierAnchorAt(const uint256& root, IronwoodMerkleFrontier& tree) const override
    {
        ++frontierReads;
        return CCoinsViewCache::GetIronwoodFrontierAnchorAt(root, tree);
    }
};

// Use real key-backed, encrypted and proved transactions inside a synthetic disk
// chain. The independent node frontier includes every output, including padding
// and foreign notes. Wallet witnesses must agree after each lifecycle operation.
class WalletWitnessTest : public BitcoinTestingSetup,
                          public ::testing::WithParamInterface<WitnessPool>
{
protected:
    void SelectTestParams() override
    {
        SelectParams(CBaseChainParams::REGTEST);
        for (int i = 0; i < 3; ++i) {
            const auto upgrade = static_cast<Consensus::UpgradeIndex>(Consensus::UPGRADE_OVERWINTER + i);
            previousUpgrades[i] = Params().GetConsensus().vUpgrades[upgrade].nActivationHeight;
            UpdateNetworkUpgradeParameters(upgrade, 1);
        }
    }

    void SetUp() override
    {
        BitcoinTestingSetup::SetUp();
        previousThreads = maxProcessingThreads;
        previousMinimum = minTxValue;
        previousBuilding = fBuilingWitnessCache;
        previousBuilt = fInitWitnessesBuilt;
        maxProcessingThreads = 2;
        minTxValue = 1;
        originalCoins = pcoinsTip;
        coins.reset(new WitnessCountingCoinsView(originalCoins));
        pcoinsTip = coins.get();
        wallet = NewWallet("witness-incremental.dat", 0);
        foreign = NewWallet("witness-foreign.dat", 1);
        CKey fundingKey;
        fundingKey.MakeNewKey(true);
        ASSERT_TRUE(fundingKeys.AddKey(fundingKey));
        fundingScript = GetScriptForDestination(fundingKey.GetPubKey().GetID());

        AddBlock({Shield(false)});
        AddBlock({Shield(false), Shield(true), Shield(false)});
        AddBlock({Shield(false)});
        ASSERT_EQ(3, chainActive.Height());
    }

    void TearDown() override
    {
        wallet.reset();
        foreign.reset();
        coins.reset();
        pcoinsTip = originalCoins;
        maxProcessingThreads = previousThreads;
        minTxValue = previousMinimum;
        fBuilingWitnessCache = previousBuilding;
        fInitWitnessesBuilt = previousBuilt;
        for (int i = 0; i < 3; ++i) {
            UpdateNetworkUpgradeParameters(static_cast<Consensus::UpgradeIndex>(Consensus::UPGRADE_OVERWINTER + i), previousUpgrades[i]);
        }
        BitcoinTestingSetup::TearDown();
    }

    std::unique_ptr<WitnessTestWallet> NewWallet(const std::string& filename, unsigned char seedByte)
    {
        auto result = std::make_unique<WitnessTestWallet>(filename);
        bool firstRun;
        if (result->LoadWallet(firstRun) != DB_LOAD_OK) throw std::runtime_error("Cannot create witness test wallet");
        CKeyingMaterial bytes(32, seedByte);
        result->LoadHDSeed(HDSeed(bytes));
        LOCK(result->cs_wallet);
        if (GetParam() == WitnessPool::Sapling) result->testSaplingAddress = result->GenerateNewSaplingZKey();
        else result->testIronwoodAddress = result->GenerateNewIronwoodZKey();
        return result;
    }

    libzcash::SaplingPaymentAddress SaplingAddress(WitnessTestWallet& target)
    {
        if (!target.testSaplingAddress) throw std::runtime_error("Missing Sapling test address");
        return *target.testSaplingAddress;
    }

    libzcash::IronwoodPaymentAddress IronwoodAddress(WitnessTestWallet& target)
    {
        if (!target.testIronwoodAddress) throw std::runtime_error("Missing Ironwood test address");
        return *target.testIronwoodAddress;
    }

    CTransaction Shield(bool mine, int ownedOutputs = 2)
    {
        const int outputs = mine ? ownedOutputs + 1 : 1;
        TransactionBuilder builder(Params().GetConsensus(), 2, &fundingKeys);
        builder.AddTransparentInput(COutPoint(GetRandHash(), 0), fundingScript, outputs * 20000 + 10000);
        if (GetParam() == WitnessPool::Sapling) {
            builder.InitializeSapling(uint256());
            auto own = SaplingAddress(*wallet);
            auto other = SaplingAddress(*foreign);
            if (mine && !builder.AddSaplingOutputRaw(own, 20000, {})) throw std::runtime_error("Cannot add owned Sapling output");
            if (!builder.AddSaplingOutputRaw(other, 20000, {})) throw std::runtime_error("Cannot add foreign Sapling output");
            for (int i = 1; mine && i < ownedOutputs; ++i) {
                if (!builder.AddSaplingOutputRaw(own, 20000, {})) throw std::runtime_error("Cannot add another owned Sapling output");
            }
            libzcash::SaplingExtendedSpendingKey key;
            if (!foreign->GetSaplingExtendedSpendingKey(other, key) || !builder.ConvertRawSaplingOutput(key.expsk.ovk)) {
                throw std::runtime_error("Cannot convert Sapling outputs");
            }
        } else {
            builder.InitializeIronwood(false, true, uint256());
            auto own = IronwoodAddress(*wallet);
            auto other = IronwoodAddress(*foreign);
            if (mine && !builder.AddIronwoodOutputRaw(own, 20000, {})) throw std::runtime_error("Cannot add owned Ironwood output");
            if (!builder.AddIronwoodOutputRaw(other, 20000, {})) throw std::runtime_error("Cannot add foreign Ironwood output");
            for (int i = 1; mine && i < ownedOutputs; ++i) {
                if (!builder.AddIronwoodOutputRaw(own, 20000, {})) throw std::runtime_error("Cannot add another owned Ironwood output");
            }
            libzcash::IronwoodExtendedSpendingKeyPirate key;
            libzcash::IronwoodFullViewingKey viewing;
            libzcash::IronwoodOutgoingViewingKey outgoing;
            if (!foreign->GetIronwoodExtendedSpendingKey(other, key) || !key.sk.DeriveFVK(&viewing) ||
                !viewing.DeriveOVK(&outgoing) || !builder.ConvertRawIronwoodOutput(outgoing.ovk)) {
                throw std::runtime_error("Cannot convert Ironwood outputs");
            }
        }
        const CTransaction tx = builder.Build().GetTxOrThrow();
        const CAmount inputValue = outputs * 20000 + 10000;
        if (!ProofValid(tx, inputValue)) throw std::runtime_error("Real shielding bundle failed authorization verification");
        ++verifiedBundles;
        if (!corruptProofChecked) {
            CMutableTransaction corrupted(tx);
            if (GetParam() == WitnessPool::Sapling) {
                sapling::test_only_replace_output_proof(corrupted.saplingBundle.GetDetailsMut(), 0, std::array<uint8_t, 192>{});
            } else {
                ironwood_bundle::test_only_corrupt_proof(corrupted.ironwoodBundle.GetDetailsMut());
            }
            if (ProofValid(CTransaction(corrupted), inputValue)) throw std::runtime_error("Corrupted shielding proof was accepted");
            corruptProofChecked = true;
        }
        return tx;
    }

    bool ProofValid(const CTransaction& tx, CAmount inputValue)
    {
        std::vector<CTxOut> previousOutputs;
        if (!tx.vin.empty()) previousOutputs.emplace_back(inputValue, fundingScript);
        const PrecomputedTransactionData precomputed(tx, previousOutputs);
        const uint256 sighash = SignatureHash(CScript(), tx, NOT_AN_INPUT, SIGHASH_ALL, 0,
            CurrentEpochBranchId(2, Params().GetConsensus()), precomputed);
        if (GetParam() == WitnessPool::Sapling) {
            auto batch = sapling::init_batch_validator(false);
            return tx.GetSaplingBundle().QueueAuthValidation(*batch, sighash) && batch->validate();
        }
        auto batch = ironwood::init_batch_validator(false);
        tx.GetIronwoodBundle().QueueAuthValidation(*batch, sighash);
        return batch->validate();
    }

    void AddBlock(std::vector<CTransaction> transactions)
    {
        LOCK(cs_main);
        const int height = chainActive.Height() + 1;
        CBlock block;
        block.nVersion = 4;
        block.nTime = chainActive.Tip()->nTime + 60;
        block.hashPrevBlock = chainActive.Tip()->GetBlockHash();
        block.vtx = std::move(transactions);
        block.hashMerkleRoot = block.BuildMerkleTree();
        for (const auto& tx : block.vtx) {
            if (tx.GetSaplingBundle().IsPresent()) saplingFrontier.AppendBundle(tx.GetSaplingBundle());
            if (tx.GetIronwoodBundle().IsPresent()) ironwoodFrontier.AppendBundle(tx.GetIronwoodBundle());
        }
        pcoinsTip->PushAnchor(saplingFrontier);
        pcoinsTip->PushAnchor(ironwoodFrontier);
        CDiskBlockPos position(7, nextOffset);
        ASSERT_TRUE(WriteBlockToDisk(block, position, Params().MessageStart()));
        nextOffset = position.nPos + GetSerializeSize(block, SER_DISK, CLIENT_VERSION);
        auto* index = new CBlockIndex(block);
        index->pprev = chainActive.Tip();
        index->nHeight = height;
        index->nFile = position.nFile;
        index->nDataPos = position.nPos;
        index->nStatus = BLOCK_HAVE_DATA;
        index->hashFinalSaplingRoot = saplingFrontier.root();
        index->hashFinalIronwoodRoot = ironwoodFrontier.root();
        auto inserted = mapBlockIndex.emplace(block.GetHash(), index);
        ASSERT_TRUE(inserted.second);
        index->phashBlock = &inserted.first->first;
        index->BuildSkip();
        chainActive.SetTip(index);
        blocks.push_back(block);
        saplingFrontiers.push_back(saplingFrontier);
        ironwoodFrontiers.push_back(ironwoodFrontier);
    }

    void Discover(WitnessTestWallet& target, int height)
    {
        LOCK(target.cs_wallet);
        const auto& block = blocks.at(height - 1);
        std::vector<CTransaction> added;
        std::set<libzcash::SaplingPaymentAddress> saplingAddresses;
        std::set<libzcash::IronwoodPaymentAddress> ironwoodAddresses;
        target.AddToWalletIfInvolvingMe(block.vtx, added, &block, height, true,
            saplingAddresses, ironwoodAddresses, true);
    }

    void DiscoverAll(WitnessTestWallet& target)
    {
        for (int height = 1; height <= 3; ++height) Discover(target, height);
    }

    uint256 ExpectedRoot(int height) const
    {
        return GetParam() == WitnessPool::Sapling ? saplingFrontiers.at(height - 1).root() : ironwoodFrontiers.at(height - 1).root();
    }

    struct ExpectedNote {
        uint256 txid;
        int output;
        uint64_t position;
        uint256 commitment;
        uint256 nullifier;
    };

    struct SingleMarkResult {
        bool tracked = false;
        uint64_t position = UINT64_MAX;
        uint256 root;
        libzcash::MerklePath path;
    };

    // Reproduce the old validation strategy independently: a fresh tree for
    // every note, with exactly one marked output and every commitment appended.
    template <typename Backend, typename Frontier>
    SingleMarkResult SingleMarkReference(const ExpectedNote& note, int throughHeight,
                                         int owningHeight, const Frontier& frontier)
    {
        Backend reference;
        reference.InitNoteCommitmentTree(frontier);
        for (int height = owningHeight; height <= throughHeight; ++height) {
            const auto& block = blocks.at(height - 1);
            for (int txIndex = 0; txIndex < block.vtx.size(); ++txIndex) {
                const auto& tx = block.vtx[txIndex];
                if (tx.GetHash() == note.txid) {
                    if (!reference.CreateEmptyPositionsForTxid(height, note.txid)) throw std::runtime_error("Cannot initialize single-mark reference");
                    const auto append = [&](const auto& outputs) {
                        for (int output = 0; output < outputs.size(); ++output) {
                            if (!reference.AppendNoteCommitment(height, note.txid, txIndex, output,
                                    &outputs[output], output == note.output)) {
                                throw std::runtime_error("Cannot append single-mark reference output");
                            }
                        }
                    };
                    if constexpr (std::is_same_v<Backend, SaplingWallet>) append(tx.GetSaplingOutputs());
                    else append(tx.GetIronwoodActions());
                } else if (!reference.AppendNoteCommitments(height, tx, txIndex)) {
                    throw std::runtime_error("Cannot append single-mark reference transaction");
                }
            }
        }
        SingleMarkResult result;
        result.root = reference.GetLatestAnchor();
        result.tracked = reference.IsNoteTracked(note.txid, note.output, result.position);
        if (result.tracked && !reference.GetMerklePathOfNote(note.txid, note.output, result.path)) {
            throw std::runtime_error("Single-mark reference lost its witness");
        }
        return result;
    }

    SingleMarkResult SingleMarkReference(const ExpectedNote& note, int throughHeight)
    {
        const int height = mapBlockIndex.at(wallet->mapWallet.at(note.txid).hashBlock)->nHeight;
        if (GetParam() == WitnessPool::Sapling) {
            const auto frontier = height > 1 ? saplingFrontiers.at(height - 2) : SaplingMerkleFrontier();
            return SingleMarkReference<SaplingWallet>(note, throughHeight, height, frontier);
        }
        const auto frontier = height > 1 ? ironwoodFrontiers.at(height - 2) : IronwoodMerkleFrontier();
        return SingleMarkReference<IronwoodWallet>(note, throughHeight, height, frontier);
    }

    void SpendRebuiltNote(WitnessTestWallet& source, const ExpectedNote& expected)
    {
        libzcash::MerklePath path;
        ASSERT_TRUE(source.Path(GetParam(), expected.txid, expected.output, path));
        const auto& received = source.mapWallet.at(expected.txid);
        TransactionBuilder builder(Params().GetConsensus(), 4);
        builder.SetFee(10000);
        if (GetParam() == WitnessPool::Sapling) {
            const auto address = SaplingAddress(source);
            libzcash::SaplingIncomingViewingKey ivk;
            libzcash::SaplingExtendedSpendingKey key;
            ASSERT_TRUE(source.GetSaplingIncomingViewingKey(address, ivk));
            ASSERT_TRUE(source.GetSaplingExtendedSpendingKey(address, key));
            const auto outputs = received.GetSaplingOutputs();
            const auto plaintext = libzcash::SaplingNotePlaintext::AttemptDecryptSaplingOutput(outputs[expected.output], ivk);
            ASSERT_TRUE(plaintext.has_value());
            const auto note = plaintext->note(ivk);
            ASSERT_TRUE(note.has_value());
            ASSERT_GT(note->value(), 10000);
            builder.InitializeSapling(source.Anchor(GetParam()));
            ASSERT_TRUE(builder.AddSaplingSpendRaw(SaplingOutPoint(expected.txid, expected.output), address,
                note->value(), note->rcm(), path, source.Anchor(GetParam())));
            ASSERT_TRUE(builder.ConvertRawSaplingSpend(key));
            ASSERT_TRUE(builder.AddSaplingOutputRaw(SaplingAddress(*foreign), note->value() - 10000, {}));
            ASSERT_TRUE(builder.ConvertRawSaplingOutput(key.expsk.ovk));
        } else {
            const auto address = IronwoodAddress(source);
            libzcash::IronwoodIncomingViewingKey ivk;
            libzcash::IronwoodExtendedSpendingKeyPirate key;
            libzcash::IronwoodFullViewingKey viewing;
            libzcash::IronwoodOutgoingViewingKey outgoing;
            ASSERT_TRUE(source.GetIronwoodIncomingViewingKey(address, ivk));
            ASSERT_TRUE(source.GetIronwoodExtendedSpendingKey(address, key));
            ASSERT_TRUE(key.sk.DeriveFVK(&viewing));
            ASSERT_TRUE(viewing.DeriveOVK(&outgoing));
            const auto actions = received.GetIronwoodActions();
            const auto plaintext = libzcash::IronwoodNotePlaintext::AttemptDecryptIronwoodAction(&actions[expected.output], ivk);
            ASSERT_TRUE(plaintext.has_value());
            const auto note = plaintext->note();
            ASSERT_TRUE(note.has_value());
            ASSERT_GT(note->value(), 10000);
            builder.InitializeIronwood(true, true, source.Anchor(GetParam()));
            ASSERT_TRUE(builder.AddIronwoodSpendRaw(IronwoodOutPoint(expected.txid, expected.output), address,
                note->value(), note->rho(), note->rseed(), path, source.Anchor(GetParam())));
            ASSERT_TRUE(builder.ConvertRawIronwoodSpend(key));
            ASSERT_TRUE(builder.AddIronwoodOutputRaw(IronwoodAddress(*foreign), note->value() - 10000, {}));
            ASSERT_TRUE(builder.ConvertRawIronwoodOutput(outgoing.ovk));
        }
        const auto spend = builder.Build().GetTxOrThrow();
        ASSERT_TRUE(spend.vin.empty());
        EXPECT_TRUE(ProofValid(spend, 0));
        const auto nullifiers = GetParam() == WitnessPool::Sapling ? spend.GetSaplingBundle().GetNullifiers() :
            spend.GetIronwoodBundle().GetNullifiers();
        EXPECT_NE(nullifiers.end(), std::find(nullifiers.begin(), nullifiers.end(), expected.nullifier));
        EXPECT_TRUE(pcoinsTip->CheckShieldedRequirements(spend).has_value());
        pcoinsTip->SetNullifiers(spend, true);
        EXPECT_FALSE(pcoinsTip->CheckShieldedRequirements(spend).has_value());
        pcoinsTip->SetNullifiers(spend, false);
    }

    std::vector<ExpectedNote> NoteOracle()
    {
        std::vector<ExpectedNote> result;
        uint64_t position = 0;
        for (const auto& block : blocks) for (const auto& tx : block.vtx) {
            if (GetParam() == WitnessPool::Sapling) {
                libzcash::SaplingIncomingViewingKey ivk;
                libzcash::SaplingExtendedSpendingKey key;
                auto address = SaplingAddress(*wallet);
                EXPECT_TRUE(wallet->GetSaplingIncomingViewingKey(address, ivk));
                EXPECT_TRUE(wallet->GetSaplingExtendedSpendingKey(address, key));
                auto outputs = tx.GetSaplingOutputs();
                for (int i = 0; i < outputs.size(); ++i, ++position) {
                    auto plaintext = libzcash::SaplingNotePlaintext::AttemptDecryptSaplingOutput(outputs[i], ivk);
                    if (plaintext && plaintext->value() > 0) {
                        auto note = plaintext->note(ivk);
                        if (!note) throw std::runtime_error("Cannot reconstruct Sapling test note");
                        auto nullifier = note->nullifier(key.ToXFVK().fvk, position);
                        if (!nullifier) throw std::runtime_error("Cannot derive Sapling oracle nullifier");
                        result.push_back({tx.GetHash(), i, position, uint256::FromRawBytes(outputs[i].cmu()), *nullifier});
                    }
                }
            } else {
                libzcash::IronwoodIncomingViewingKey ivk;
                libzcash::IronwoodExtendedSpendingKeyPirate key;
                libzcash::IronwoodFullViewingKey viewing;
                auto address = IronwoodAddress(*wallet);
                EXPECT_TRUE(wallet->GetIronwoodIncomingViewingKey(address, ivk));
                EXPECT_TRUE(wallet->GetIronwoodExtendedSpendingKey(address, key));
                EXPECT_TRUE(key.sk.DeriveFVK(&viewing));
                auto actions = tx.GetIronwoodActions();
                for (int i = 0; i < actions.size(); ++i, ++position) {
                    auto plaintext = libzcash::IronwoodNotePlaintext::AttemptDecryptIronwoodAction(&actions[i], ivk);
                    if (plaintext && plaintext->value() > 0) {
                        auto note = plaintext->note();
                        if (!note) throw std::runtime_error("Cannot reconstruct Ironwood test note");
                        auto nullifier = note->nullifier(viewing);
                        if (!nullifier) throw std::runtime_error("Cannot derive Ironwood oracle nullifier");
                        result.push_back({tx.GetHash(), i, position, uint256::FromRawBytes(actions[i].cmx()), *nullifier});
                    }
                }
            }
        }
        return result;
    }

    void CheckNotes(WitnessTestWallet& target, const std::vector<ExpectedNote>& expected, int height)
    {
        EXPECT_EQ(height, target.Checkpoint(GetParam()));
        EXPECT_EQ(ExpectedRoot(height), target.Anchor(GetParam()));
        for (const auto& note : expected) {
            auto& tx = target.mapWallet.at(note.txid);
            const auto position = GetParam() == WitnessPool::Sapling ?
                tx.mapSaplingNoteData.at(SaplingOutPoint(note.txid, note.output)).getPosition() :
                tx.mapIronwoodNoteData.at(IronwoodOutPoint(note.txid, note.output)).getPosition();
            const auto nullifier = GetParam() == WitnessPool::Sapling ?
                tx.mapSaplingNoteData.at(SaplingOutPoint(note.txid, note.output)).nullifier :
                tx.mapIronwoodNoteData.at(IronwoodOutPoint(note.txid, note.output)).nullifier;
            ASSERT_TRUE(position.has_value());
            ASSERT_TRUE(nullifier.has_value());
            EXPECT_EQ(note.position, *position);
            EXPECT_EQ(note.nullifier, *nullifier);
            libzcash::MerklePath path;
            ASSERT_TRUE(target.Path(GetParam(), note.txid, note.output, path));
            uint256 root;
            ASSERT_TRUE(target.PathRoot(GetParam(), path, note.commitment, root));
            EXPECT_EQ(ExpectedRoot(height), root);
        }
    }

    std::unique_ptr<WitnessTestWallet> wallet;
    std::unique_ptr<WitnessTestWallet> foreign;
    std::vector<CBlock> blocks;
    std::vector<SaplingMerkleFrontier> saplingFrontiers;
    std::vector<IronwoodMerkleFrontier> ironwoodFrontiers;
    SaplingMerkleFrontier saplingFrontier;
    IronwoodMerkleFrontier ironwoodFrontier;
    CBasicKeyStore fundingKeys;
    CScript fundingScript;
    std::unique_ptr<WitnessCountingCoinsView> coins;
    CCoinsViewCache* originalCoins = nullptr;
    int previousUpgrades[3]{};
    int previousThreads = 0;
    CAmount previousMinimum = 0;
    bool previousBuilding = false;
    bool previousBuilt = false;
    unsigned int nextOffset = 0;
    int verifiedBundles = 0;
    bool corruptProofChecked = false;
};

TEST_P(WalletWitnessTest, CleanupFailureRetainsMarkedNotesAndTheirPaths)
{
    LOCK2(cs_main, wallet->cs_wallet);
    DiscoverAll(*wallet);
    wallet->Increment(GetParam(), chainActive[3], blocks[2]);
    const auto expected = NoteOracle();
    ASSERT_EQ(2u, expected.size());
    CheckNotes(*wallet, expected, 3);
    const auto originalTree = wallet->SaveTree(GetParam());
    const std::string filename = wallet->strWalletFile;
    std::vector<uint256> transactions{expected.front().txid}, archives;

    // The real DB erase returns false for an unavailable handle. Do not discard
    // witness marks or in-memory transactions before that erase succeeds.
    wallet->strWalletFile.clear();
    EXPECT_THROW(wallet->DeleteTransactions(transactions, archives, true), std::runtime_error);
    wallet->strWalletFile = filename;

    ASSERT_EQ(1u, wallet->mapWallet.count(expected.front().txid));
    EXPECT_EQ(originalTree, wallet->SaveTree(GetParam()));
    CheckNotes(*wallet, expected, 3);
}

TEST_P(WalletWitnessTest, LargeSaplingBundleStillRequiresValidProofsAndBindingSignature)
{
    if (GetParam() != WitnessPool::Sapling) GTEST_SKIP();
    const auto tx = Shield(true, 32);
    const auto outputs = tx.GetSaplingOutputs();
    ASSERT_GE(outputs.size(), 32u);
    const CAmount inputValue = 33 * 20000 + 10000;
    const auto check = [&](const CTransaction& candidate, bool wrongSighash, bool expectedPreparation) {
        const PrecomputedTransactionData precomputed(candidate, {CTxOut(inputValue, fundingScript)});
        uint256 sighash = SignatureHash(CScript(), candidate, NOT_AN_INPUT, SIGHASH_ALL, 0,
            CurrentEpochBranchId(2, Params().GetConsensus()), precomputed);
        if (wrongSighash) sighash.begin()[0] ^= 1;
        auto batch = sapling::init_batch_validator(false);
        const bool prepared = candidate.GetSaplingBundle().QueueAuthValidation(*batch, sighash);
        EXPECT_EQ(expectedPreparation, prepared);
        return prepared && batch->validate();
    };
    EXPECT_TRUE(check(tx, false, true));
    EXPECT_FALSE(check(tx, true, true));
    for (size_t index : {size_t(0), outputs.size() / 2, outputs.size() - 1}) {
        // Another output's proof is well encoded, but attests to the wrong
        // statement. Preparation must pass and final verification must fail.
        CMutableTransaction wrongProof(tx);
        sapling::test_only_replace_output_proof(wrongProof.saplingBundle.GetDetailsMut(), index,
            outputs[(index + 1) % outputs.size()].zkproof());
        EXPECT_FALSE(check(CTransaction(wrongProof), false, true));

        CMutableTransaction malformed(tx);
        sapling::test_only_replace_output_proof(malformed.saplingBundle.GetDetailsMut(), index,
            std::array<uint8_t, 192>{});
        EXPECT_FALSE(check(CTransaction(malformed), false, false));
    }
}

TEST_P(WalletWitnessTest, MarkedNotesSurviveRebuildSerializationAndRewind)
{
    LOCK2(cs_main, wallet->cs_wallet);
    EXPECT_EQ(5, verifiedBundles);
    EXPECT_TRUE(corruptProofChecked);
    for (int height = 1; height <= 3; ++height) {
        LOCK(wallet->cs_wallet);
        Discover(*wallet, height);
        wallet->Increment(GetParam(), chainActive[height], blocks.at(height - 1));
    }
    auto expected = NoteOracle();
    ASSERT_EQ(2u, expected.size());
    ASSERT_EQ(1u, wallet->mapWallet.size());
    CheckNotes(*wallet, expected, 3);
    coins->frontierReads = 0;
    EXPECT_TRUE(wallet->Validate(GetParam(), chainActive[3]));
    EXPECT_EQ(1, coins->frontierReads);
    CheckNotes(*wallet, expected, 3);

    auto rebuilt = NewWallet("witness-rebuilt.dat", 0);
    DiscoverAll(*rebuilt);
    rebuilt->Increment(GetParam(), chainActive[3], blocks[2]);
    CheckNotes(*rebuilt, expected, 3);
    SpendRebuiltNote(*rebuilt, expected.front());

    // A correct root can exist without marks when notes are discovered later.
    auto unmarked = NewWallet("witness-unmarked.dat", 0);
    unmarked->Increment(GetParam(), chainActive[3], blocks[2]);
    ASSERT_EQ(ExpectedRoot(3), unmarked->Anchor(GetParam()));
    DiscoverAll(*unmarked);
    EXPECT_FALSE(unmarked->Validate(GetParam(), chainActive[3]));
    unmarked->Increment(GetParam(), chainActive[3], blocks[2]);
    CheckNotes(*unmarked, expected, 3);

    auto restored = NewWallet("witness-restored.dat", 0);
    DiscoverAll(*restored);
    restored->LoadTree(GetParam(), rebuilt->SaveTree(GetParam()));
    coins->frontierReads = 0;
    EXPECT_TRUE(restored->Validate(GetParam(), chainActive[3]));
    EXPECT_EQ(1, coins->frontierReads);
    CheckNotes(*restored, expected, 3);

    restored->Rewind(GetParam(), chainActive[3]);
    CheckNotes(*restored, expected, 2);
    const auto before = restored->Anchor(GetParam());
    EXPECT_THROW(restored->Process(GetParam(), chainActive[2], blocks[1]), std::runtime_error);
    EXPECT_EQ(2, restored->Checkpoint(GetParam()));
    EXPECT_EQ(before, restored->Anchor(GetParam()));
    EXPECT_THROW(restored->Process(GetParam(), chainActive[1], blocks[0]), std::runtime_error);
    EXPECT_EQ(before, restored->Anchor(GetParam()));
    restored->Increment(GetParam(), chainActive[3], blocks[2]);
    CheckNotes(*restored, expected, 3);

    // Simulate a crash after checkpointing the last block but before appending
    // its foreign suffix. Owned positions remain correct, while the root does not.
    auto partial = NewWallet("witness-partial.dat", 0);
    DiscoverAll(*partial);
    partial->InitFrontier(GetParam(), saplingFrontiers[0], ironwoodFrontiers[0]);
    partial->Process(GetParam(), chainActive[2], blocks[1]);
    CBlock emptyLast = blocks[2];
    emptyLast.vtx.clear();
    partial->Process(GetParam(), chainActive[3], emptyLast);
    ASSERT_EQ(3, partial->Checkpoint(GetParam()));
    ASSERT_EQ(ExpectedRoot(2), partial->Anchor(GetParam()));
    EXPECT_TRUE(partial->Validate(GetParam(), chainActive[3]));
    auto resumed = NewWallet("witness-resumed.dat", 0);
    DiscoverAll(*resumed);
    resumed->LoadTree(GetParam(), partial->SaveTree(GetParam()));
    resumed->Increment(GetParam(), chainActive[3], blocks[2]);
    CheckNotes(*resumed, expected, 3);

    // A persisted checkpoint beyond the active tip must be rebuilt as well.
    auto* fullTip = chainActive.Tip();
    chainActive.SetTip(chainActive[2]);
    rebuilt->Increment(GetParam(), chainActive[2], blocks[1]);
    CheckNotes(*rebuilt, expected, 2);
    chainActive.SetTip(fullTip);

    auto wrong = NewWallet("witness-wrong-position.dat", 0);
    DiscoverAll(*wrong);
    wrong->InitFrontier(GetParam(), saplingFrontiers[0], ironwoodFrontiers[0], true);
    wrong->Process(GetParam(), chainActive[2], blocks[1]);
    wrong->Process(GetParam(), chainActive[3], blocks[2]);
    auto beforeNotes = wrong->mapWallet;
    coins->frontierReads = 0;
    EXPECT_FALSE(wrong->Validate(GetParam(), chainActive[3]));
    EXPECT_EQ(1, coins->frontierReads);
    for (const auto& note : expected) {
        if (GetParam() == WitnessPool::Sapling) {
            const SaplingOutPoint op(note.txid, note.output);
            EXPECT_EQ(beforeNotes.at(note.txid).mapSaplingNoteData.at(op).nullifier,
                wrong->mapWallet.at(note.txid).mapSaplingNoteData.at(op).nullifier);
        } else {
            const IronwoodOutPoint op(note.txid, note.output);
            EXPECT_EQ(beforeNotes.at(note.txid).mapIronwoodNoteData.at(op).nullifier,
                wrong->mapWallet.at(note.txid).mapIronwoodNoteData.at(op).nullifier);
        }
    }

    auto badRoot = NewWallet("witness-wrong-root.dat", 0);
    DiscoverAll(*badRoot);
    auto* tip = chainActive[3];
    uint256& root = GetParam() == WitnessPool::Sapling ? tip->hashFinalSaplingRoot : tip->hashFinalIronwoodRoot;
    const uint256 correctRoot = root;
    root = GetRandHash();
    EXPECT_THROW(badRoot->Increment(GetParam(), tip, blocks[2]), std::runtime_error);
    EXPECT_FALSE(badRoot->Validated(GetParam()));
    root = correctRoot;
}

TEST_P(WalletWitnessTest, TransactionValidationMatchesIndependentSingleNoteReplays)
{
    LOCK2(cs_main, wallet->cs_wallet);
    AddBlock({Shield(true, 3), Shield(false), Shield(true, 3)});
    for (int height = 1; height <= 4; ++height) {
        Discover(*wallet, height);
        wallet->Increment(GetParam(), chainActive[height], blocks.at(height - 1));
    }
    const auto expected = NoteOracle();
    ASSERT_EQ(8u, expected.size());
    ASSERT_EQ(3u, wallet->mapWallet.size());
    CheckNotes(*wallet, expected, 4);

    for (const auto& note : expected) {
        SCOPED_TRACE(::testing::Message() << note.txid.ToString() << ":" << note.output);
        const auto reference = SingleMarkReference(note, 4);
        ASSERT_TRUE(reference.tracked);
        EXPECT_EQ(note.position, reference.position);
        EXPECT_EQ(ExpectedRoot(4), reference.root);
        uint64_t livePosition;
        ASSERT_TRUE(wallet->TrackedPosition(GetParam(), note.txid, note.output, livePosition));
        EXPECT_EQ(reference.position, livePosition);
        libzcash::MerklePath livePath;
        ASSERT_TRUE(wallet->Path(GetParam(), note.txid, note.output, livePath));
        CDataStream expectedPath(SER_DISK, CLIENT_VERSION), actualPath(SER_DISK, CLIENT_VERSION);
        expectedPath << reference.path;
        actualPath << livePath;
        EXPECT_EQ(std::vector<unsigned char>(expectedPath.begin(), expectedPath.end()),
                  std::vector<unsigned char>(actualPath.begin(), actualPath.end()));
    }
    coins->frontierReads = 0;
    EXPECT_TRUE(wallet->Validate(GetParam(), chainActive[4]));
    EXPECT_EQ(3, coins->frontierReads); // Three transactions, eight owned notes.
    CheckNotes(*wallet, expected, 4);

    // Neither strategy can manufacture a tracked note beyond the transaction.
    auto missing = expected.front();
    missing.output = 1000;
    EXPECT_FALSE(SingleMarkReference(missing, 4).tracked);
    auto& tx = wallet->mapWallet.at(missing.txid);
    if (GetParam() == WitnessPool::Sapling) tx.mapSaplingNoteData.emplace(SaplingOutPoint(missing.txid, missing.output), SaplingNoteData());
    else tx.mapIronwoodNoteData.emplace(IronwoodOutPoint(missing.txid, missing.output), IronwoodNoteData());
    EXPECT_FALSE(wallet->Validate(GetParam(), chainActive[4]));
    if (GetParam() == WitnessPool::Sapling) tx.mapSaplingNoteData.erase(SaplingOutPoint(missing.txid, missing.output));
    else tx.mapIronwoodNoteData.erase(IronwoodOutPoint(missing.txid, missing.output));

    auto shifted = NewWallet("witness-differential-shifted.dat", 0);
    for (int height = 1; height <= 4; ++height) Discover(*shifted, height);
    shifted->InitFrontier(GetParam(), saplingFrontiers[0], ironwoodFrontiers[0], true);
    for (int height = 2; height <= 4; ++height) shifted->Process(GetParam(), chainActive[height], blocks.at(height - 1));
    for (const auto& note : expected) {
        const auto reference = SingleMarkReference(note, 4);
        uint64_t shiftedPosition;
        ASSERT_TRUE(shifted->TrackedPosition(GetParam(), note.txid, note.output, shiftedPosition));
        EXPECT_NE(reference.position, shiftedPosition);
    }
    EXPECT_FALSE(shifted->Validate(GetParam(), chainActive[4]));
}

INSTANTIATE_TEST_SUITE_P(ShieldedPools, WalletWitnessTest,
    ::testing::Values(WitnessPool::Sapling, WitnessPool::Ironwood));
