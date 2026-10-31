// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <gtest/gtest.h>

#include "gtest/gtestutils.h"
#include "random.h"
#include "wallet/walletdb.h"

#include <functional>
#include <atomic>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <thread>

// Use real Berkeley DB transactions. Failure injection leaves commit's handle
// consumed, as Berkeley DB does, and snapshots check the complete durable DB.
class PersistenceFaultDB : public CWalletDB
{
public:
    using SnapshotType = std::vector<std::pair<std::vector<char>, std::vector<char>>>;
    explicit PersistenceFaultDB(const std::string& file) : CWalletDB(file) {}

    std::string failure;
    bool throwFailure = false;
    bool failureReached = false;
    int plaintextWrites = 0;
    int aborts = 0;
    std::function<void()> beforeCommit;

    bool TxnBegin() { return Step("begin") && CWalletDB::TxnBegin(); }
    bool TxnAbort() { ++aborts; return CWalletDB::TxnAbort(); }
    bool TxnCommit()
    {
        if (beforeCommit) beforeCommit();
        if (failure == "commit") {
            // A failed commit consumes the transaction; roll the injected one
            // back before returning or throwing instead of leaving it pending.
            TxnAbort();
            return Step("commit");
        }
        return CWalletDB::TxnCommit();
    }

#define PERSISTENCE_WRITE(method, stage, plaintext) \
    template <class... Args> bool method(Args&&... args) { \
        if (!Step(stage)) return false; \
        if (plaintext) ++plaintextWrites; \
        return CWalletDB::method(std::forward<Args>(args)...); \
    }
    PERSISTENCE_WRITE(WriteTx, "transaction", true)
    PERSISTENCE_WRITE(WriteArcTx, "archive", true)
    PERSISTENCE_WRITE(WriteArcSaplingOp, "sapling-outpoint", true)
    PERSISTENCE_WRITE(WriteArcIronwoodOp, "ironwood-outpoint", true)
    PERSISTENCE_WRITE(WriteSaplingPaymentAddress, "sapling-address", true)
    PERSISTENCE_WRITE(WriteIronwoodPaymentAddress, "ironwood-address", true)
    PERSISTENCE_WRITE(WriteBestBlock, "locator", false)
    PERSISTENCE_WRITE(WriteSaplingWitnesses, "sapling-witnesses", true)
    PERSISTENCE_WRITE(WriteIronwoodWitnesses, "ironwood-witnesses", true)
    PERSISTENCE_WRITE(WriteCryptedTx, "transaction", false)
    PERSISTENCE_WRITE(WriteCryptedArcTx, "archive", false)
    PERSISTENCE_WRITE(WriteCryptedArcSaplingOp, "sapling-outpoint", false)
    PERSISTENCE_WRITE(WriteCryptedArcIronwoodOp, "ironwood-outpoint", false)
    PERSISTENCE_WRITE(WriteCryptedSaplingPaymentAddress, "sapling-address", false)
    PERSISTENCE_WRITE(WriteCryptedIronwoodPaymentAddress, "ironwood-address", false)
    PERSISTENCE_WRITE(WriteCryptedSaplingWitnesses, "sapling-witnesses", false)
    PERSISTENCE_WRITE(WriteCryptedIronwoodWitnesses, "ironwood-witnesses", false)
#undef PERSISTENCE_WRITE

    SnapshotType Snapshot()
    {
        SnapshotType result;
        Dbc* cursor = GetCursor();
        if (!cursor) throw std::runtime_error("Cannot snapshot test database");
        CDataStream key(SER_DISK, CLIENT_VERSION), value(SER_DISK, CLIENT_VERSION);
        int status = ReadAtCursor(cursor, key, value, DB_FIRST);
        while (status == 0) {
            result.push_back({{key.begin(), key.end()}, {value.begin(), value.end()}});
            status = ReadAtCursor(cursor, key, value, DB_NEXT);
        }
        cursor->close();
        if (status != DB_NOTFOUND) throw std::runtime_error("Cannot finish test database snapshot");
        return result;
    }

private:
    bool Step(const char* stage)
    {
        if (failure != stage) return true;
        failureReached = true;
        if (throwFailure) throw std::runtime_error("Injected wallet persistence failure");
        return false;
    }
};

class PersistenceTestWallet : public CWallet
{
public:
    PersistenceTestWallet() : CWallet("persistence-test.dat") {}
    bool TryKeyStoreLock()
    {
        TRY_LOCK(cs_KeyStore, locked);
        return static_cast<bool>(locked);
    }
    template <class DB> bool Save(DB& db, const CBlockLocator& locator, int height)
    {
        return SetBestChainINTERNAL(db, locator, height);
    }

    bool EnableTestEncryption()
    {
        // Exercise real serialization/encryption without deriving a passphrase
        // key for each injected failure. This wallet contains synthetic data.
        seedEncyptionFP = GetRandHash();
        return SetCrypted() && UnlockUnchecked(CKeyingMaterial(32, 7));
    }

    void PreparePendingWork()
    {
        CMutableTransaction transaction;
        transaction.vin.emplace_back(COutPoint(GetRandHash(), 0));
        transaction.vout.emplace_back(1, CScript());
        CWalletTx stored(this, CTransaction(transaction));
        mapWallet.emplace(stored.GetHash(), stored);
        mapArcTxs.emplace(GetRandHash(), ArchiveTxPoint(GetRandHash(), 1));
        mapArcSaplingOutPoints.emplace(GetRandHash(), SaplingOutPoint(GetRandHash(), 0));
        mapArcIronwoodOutPoints.emplace(GetRandHash(), IronwoodOutPoint(GetRandHash(), 0));

        CKeyingMaterial seedBytes(32, 1);
        const HDSeed seed(seedBytes);
        const auto saplingKey = libzcash::SaplingExtendedSpendingKey::Master(seed);
        const auto saplingViewing = saplingKey.ToXFVK();
        libzcash::SaplingIncomingViewingKey saplingIncoming;
        saplingViewing.fvk.DeriveIVK(&saplingIncoming);
        mapUnsavedSaplingIncomingViewingKeys.emplace(saplingKey.DefaultAddress(),
            std::make_pair(saplingIncoming, KeyScope::External));

        const auto ironwoodKey = libzcash::IronwoodExtendedSpendingKeyPirate::Master(seed);
        const auto ironwoodViewing = ironwoodKey.GetXFVK();
        if (!ironwoodViewing) throw std::runtime_error("Cannot derive persistence test viewing key");
        libzcash::IronwoodIncomingViewingKey ironwoodIncoming;
        libzcash::IronwoodPaymentAddress ironwoodAddress;
        if (!ironwoodViewing->fvk.DeriveIVK(&ironwoodIncoming) ||
            !ironwoodKey.sk.DeriveDefaultAddress(&ironwoodAddress)) {
            throw std::runtime_error("Cannot derive persistence test address");
        }
        mapUnsavedIronwoodIncomingViewingKeys.emplace(ironwoodAddress,
            std::make_pair(ironwoodIncoming, KeyScope::External));
        AdvanceTrees(1);
    }

    void AdvanceTrees(int height)
    {
        if (!saplingWallet.CheckpointNoteCommitmentTree(height) ||
            !ironwoodWallet.CheckpointNoteCommitmentTree(height)) {
            throw std::runtime_error("Cannot checkpoint persistence test trees");
        }
    }

    bool Pending() const
    {
        return mapArcTxs.size() == 1 && mapArcTxs.begin()->second.writeToDisk &&
            mapArcSaplingOutPoints.size() == 1 && mapArcSaplingOutPoints.begin()->second.writeToDisk &&
            mapArcIronwoodOutPoints.size() == 1 && mapArcIronwoodOutPoints.begin()->second.writeToDisk &&
            mapUnsavedSaplingIncomingViewingKeys.size() == 1 && mapUnsavedIronwoodIncomingViewingKeys.size() == 1;
    }

    bool Clean() const
    {
        return !mapArcTxs.begin()->second.writeToDisk && !mapArcSaplingOutPoints.begin()->second.writeToDisk &&
            !mapArcIronwoodOutPoints.begin()->second.writeToDisk && mapUnsavedSaplingIncomingViewingKeys.empty() &&
            mapUnsavedIronwoodIncomingViewingKeys.empty();
    }
};

struct PersistenceFailure {
    bool encrypted;
    const char* stage;
    bool throws;
};

class WalletPersistenceTest : public BitcoinTestingSetup,
                              public ::testing::WithParamInterface<PersistenceFailure>
{
protected:
    void SelectTestParams() override { SelectParams(CBaseChainParams::REGTEST); }
    void SetUp() override
    {
        BitcoinTestingSetup::SetUp();
        wallet = std::make_unique<PersistenceTestWallet>();
        bool firstRun;
        ASSERT_EQ(DB_LOAD_OK, wallet->LoadWallet(firstRun));
    }
    void TearDown() override
    {
        wallet.reset();
        BitcoinTestingSetup::TearDown();
    }
    std::unique_ptr<PersistenceTestWallet> wallet;
};

TEST_P(WalletPersistenceTest, FailurePreservesDurableStateAndRetriesAllPendingRecords)
{
    LOCK2(cs_main, wallet->cs_wallet);
    const auto scenario = GetParam();
    SCOPED_TRACE(::testing::Message() << scenario.encrypted << ", " << scenario.stage << ", " << scenario.throws);
    if (scenario.encrypted) ASSERT_TRUE(wallet->EnableTestEncryption());
    PersistenceFaultDB database(wallet->strWalletFile);
    const CBlockLocator oldLocator(std::vector<uint256>{GetRandHash()});
    const CBlockLocator newLocator(std::vector<uint256>{GetRandHash()});
    wallet->AdvanceTrees(0);
    ASSERT_TRUE(wallet->Save(database, oldLocator, 0));
    const auto original = database.Snapshot();
    wallet->PreparePendingWork();
    ASSERT_TRUE(wallet->Pending());

    database.failure = scenario.stage;
    database.throwFailure = scenario.throws;
    database.beforeCommit = [&] { EXPECT_TRUE(wallet->Pending()); };
    EXPECT_FALSE(wallet->Save(database, newLocator, 1));
    EXPECT_TRUE(database.failureReached);
    EXPECT_EQ(original, database.Snapshot());
    EXPECT_TRUE(wallet->Pending());
    EXPECT_TRUE(wallet->fRunSetBestChain);
    EXPECT_EQ(0, wallet->walletHeight);
    EXPECT_EQ(std::string(scenario.stage) == "begin" ? 0 : 1, database.aborts);

    database.failure.clear();
    database.throwFailure = false;
    ASSERT_TRUE(wallet->Save(database, newLocator, 1));
    EXPECT_TRUE(wallet->Clean());
    EXPECT_FALSE(wallet->fRunSetBestChain);
    EXPECT_EQ(1, wallet->walletHeight);
    CBlockLocator saved;
    ASSERT_TRUE(database.ReadBestBlock(saved));
    EXPECT_EQ(newLocator.vHave, saved.vHave);
    EXPECT_NE(original, database.Snapshot());
    if (scenario.encrypted) EXPECT_EQ(0, database.plaintextWrites);
}

static std::vector<PersistenceFailure> PersistenceFailures()
{
    std::vector<PersistenceFailure> result;
    for (bool encrypted : {false, true}) {
        for (const char* stage : {"begin", "transaction", "archive", "sapling-outpoint", "ironwood-outpoint",
                                 "sapling-address", "ironwood-address", "locator", "sapling-witnesses",
                                 "ironwood-witnesses", "commit"}) {
            result.push_back({encrypted, stage, false});
        }
        // A witness-stage exception used to escape the old transaction catch.
        result.push_back({encrypted, "ironwood-witnesses", true});
        result.push_back({encrypted, "commit", true});
    }
    return result;
}

INSTANTIATE_TEST_SUITE_P(AtomicWrites, WalletPersistenceTest, ::testing::ValuesIn(PersistenceFailures()));

TEST_F(WalletPersistenceTest, LockedEncryptedWalletDefersWithoutPlaintextWritesOrLosingPendingRecords)
{
    LOCK2(cs_main, wallet->cs_wallet);
    ASSERT_TRUE(wallet->EnableTestEncryption());
    PersistenceFaultDB database(wallet->strWalletFile);
    const CBlockLocator locator(std::vector<uint256>{GetRandHash()});
    wallet->AdvanceTrees(0);
    ASSERT_TRUE(wallet->Save(database, locator, 0));
    const auto original = database.Snapshot();
    wallet->PreparePendingWork();
    ASSERT_TRUE(wallet->Lock());

    EXPECT_FALSE(wallet->Save(database, CBlockLocator(std::vector<uint256>{GetRandHash()}), 1));
    EXPECT_TRUE(wallet->Pending());
    EXPECT_TRUE(wallet->fRunSetBestChain);
    EXPECT_EQ(0, wallet->walletHeight);
    EXPECT_EQ(original, database.Snapshot());
    EXPECT_EQ(0, database.plaintextWrites);
    EXPECT_EQ(1, database.aborts);
}

TEST_F(WalletPersistenceTest, EncryptionStateRemainsLockedThroughCommit)
{
    LOCK2(cs_main, wallet->cs_wallet);
    ASSERT_TRUE(wallet->EnableTestEncryption());
    PersistenceFaultDB database(wallet->strWalletFile);
    wallet->AdvanceTrees(0);
    bool observed = false;
    database.beforeCommit = [&] {
        std::atomic<bool> acquired{true};
        std::thread probe([&] { acquired = wallet->TryKeyStoreLock(); });
        probe.join();
        EXPECT_FALSE(acquired.load());
        observed = true;
    };
    EXPECT_TRUE(wallet->Save(database, CBlockLocator(std::vector<uint256>{GetRandHash()}), 0));
    EXPECT_TRUE(observed);
}
