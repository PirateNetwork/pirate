// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <gtest/gtest.h>

#include "gtest/gtestutils.h"
#include "komodo_extern_globals.h"
#include "main.h"
#include "random.h"
#include "streams.h"
#include "transaction_builder.h"
#include "txdb.h"
#include "undo.h"
#include "util/strencodings.h"
#include "zcash/Address.hpp"
#include "zcash/History.hpp"

#include <array>
#include <limits>

#ifdef WIN32
#include <io.h>
#include <winioctl.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

bool FindUndoPos(CValidationState&, int, CDiskBlockPos&, unsigned int);
void UpdateCoins(const CTransaction&, CCoinsViewCache&, CTxUndo&, int);

namespace {

// Captured from the pre-fix 32-bit field serializers, at a fixed stream version.
constexpr int LEGACY_STORAGE_VERSION = 6001450;
const char* LEGACY_INDEX = "81eda52a2a1d030786fefeff098efefefe70222222222222222222222222222222222222222222222222222222222222222204000000111111111111111111111111111111111111111111111111111111111111111133333333333333333333333333333333333333333333333333333333333333334444444444444444444444444444444444444444444444444444444444444444d2029649ffff071f555555555555555555555555555555555555555555555555555555555555555500017b000000000000000138feffffffffffff010700000000000000010800000000000000090000000000000066666666666666666666666666666666666666666666666666666666666666667777777777777777777777777777777777777777777777777777777777777777888888888888888888888888888888888888888888888888888888888888888899999999999999999999999999999999999999999999999999999999999999990a00000000000000";
const char* LEGACY_FILE_INFO = "0386fefeff098efefefe70012a7b8248";
const char* LEGACY_POSITION = "078efefefe70";

template<typename T>
std::string StorageHex(const T& value)
{
    CDataStream stream(SER_DISK, LEGACY_STORAGE_VERSION);
    stream << value;
    return HexStr(stream.begin(), stream.end());
}

class UndoStorageTest : public BitcoinTestingSetup
{
protected:
    void SelectTestParams() override { SelectParams(CBaseChainParams::REGTEST); }

    void SetUp() override
    {
        previousLoading = KOMODO_LOADINGBLOCKS;
        previousIdentity = chainName;
        BitcoinTestingSetup::SetUp();
        chainName = assetchain("TST");
    }

    void TearDown() override
    {
        BitcoinTestingSetup::TearDown();
        chainName = previousIdentity;
        KOMODO_LOADINGBLOCKS = previousLoading;
    }

    // Prefer sparse files. Some filesystems and compatibility layers cannot
    // report holes reliably, so allow the bounded four-GiB fixture there only
    // when at least five GiB is free. Never fill the gap by writing zeroes.
    bool PrepareLargeFile(FILE* file)
    {
        bool sparse = false;
#ifdef WIN32
        DWORD returned;
        sparse = DeviceIoControl(reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(file))),
                                 FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr) != 0;
#else
        constexpr off_t probeSize = 1024 * 1024;
        struct stat status;
        sparse = ftruncate(fileno(file), probeSize) == 0 &&
            fstat(fileno(file), &status) == 0 && status.st_blocks >= 0 &&
            static_cast<uint64_t>(status.st_blocks) < uint64_t(probeSize) / 512;
        if (ftruncate(fileno(file), 0) != 0 || !FileSeek(file, 0, SEEK_SET))
            return false;
#endif
        if (sparse)
            return true;
        boost::system::error_code error;
        const auto space = boost::filesystem::space(pathTemp, error);
        return !error && space.available >= (uint64_t{5} << 30);
    }

    bool PrepareUndoFile(int number)
    {
        CAutoFile file(OpenUndoFile(CDiskBlockPos(number, 0)), SER_DISK, CLIENT_VERSION);
        return !file.IsNull() && PrepareLargeFile(file.Get());
    }

    // Load a real file-info database record through the startup loader. This
    // seeds the allocator without physically filling the preceding file range.
    void LoadUndoSize(uint64_t size)
    {
        CBlockFileInfo info;
        info.nUndoSize = size;
        const std::vector<std::pair<int, const CBlockFileInfo*>> files{{0, &info}};
        ASSERT_TRUE(pblocktree->WriteBatchSync(files, 0, {}));
        UnloadBlockIndex();
        ASSERT_TRUE(LoadBlockIndex(false));
        EXPECT_EQ(size, CalculateCurrentUsage());
    }

private:
    bool previousLoading = false;
    assetchain previousIdentity;
};

TEST_F(UndoStorageTest, ReadsLegacyIndexAndFileMetadataWithoutChangingTheirBytes)
{
    const auto indexBytes = ParseHex(LEGACY_INDEX);
    CDataStream indexStream(indexBytes, SER_DISK, LEGACY_STORAGE_VERSION);
    CDiskBlockIndex index;
    indexStream >> index;
    EXPECT_TRUE(indexStream.empty());
    EXPECT_EQ(uint64_t{0x80000009}, index.nDataPos);
    EXPECT_EQ(uint64_t{0xfffffff0}, index.nUndoPos);
    EXPECT_EQ(LEGACY_INDEX, StorageHex(index));

    const auto infoBytes = ParseHex(LEGACY_FILE_INFO);
    CDataStream infoStream(infoBytes, SER_DISK, LEGACY_STORAGE_VERSION);
    CBlockFileInfo info;
    infoStream >> info;
    EXPECT_EQ(uint64_t{0x80000009}, info.nSize);
    EXPECT_EQ(uint64_t{0xfffffff0}, info.nUndoSize);
    EXPECT_EQ(LEGACY_FILE_INFO, StorageHex(info));

    const auto posBytes = ParseHex(LEGACY_POSITION);
    CDataStream posStream(posBytes, SER_DISK, LEGACY_STORAGE_VERSION);
    CDiskBlockPos position;
    posStream >> position;
    EXPECT_EQ(CDiskBlockPos(7, uint64_t{0xfffffff0}), position);
    EXPECT_EQ(LEGACY_POSITION, StorageHex(position));
}

TEST_F(UndoStorageTest, RetainsOffsetsAboveFourGiBInIndexAndFileMetadata)
{
    CDiskBlockIndex index;
    index.nStatus = BLOCK_HAVE_DATA | BLOCK_HAVE_UNDO;
    index.nFile = 7;
    index.nDataPos = (uint64_t{1} << 32) + 17;
    index.nUndoPos = (uint64_t{1} << 33) + 29;
    CDataStream encoded(SER_DISK, LEGACY_STORAGE_VERSION);
    encoded << index;
    CDiskBlockIndex decoded;
    encoded >> decoded;
    EXPECT_EQ(index.GetBlockPos(), decoded.GetBlockPos());
    EXPECT_EQ(index.GetUndoPos(), decoded.GetUndoPos());

    CBlockFileInfo info;
    info.nSize = index.nDataPos + 128;
    info.nUndoSize = index.nUndoPos + 512;
    const std::vector<std::pair<int, const CBlockFileInfo*>> files{{7, &info}};
    ASSERT_TRUE(pblocktree->WriteBatchSync(files, 7, {}));
    CBlockFileInfo loaded;
    ASSERT_TRUE(pblocktree->ReadBlockFileInfo(7, loaded));
    EXPECT_EQ(info.nSize, loaded.nSize);
    EXPECT_EQ(info.nUndoSize, loaded.nUndoSize);
}

TEST_F(UndoStorageTest, StartupReloadPreservesLargeUndoPositionsInRealBlockIndexes)
{
    LOCK(cs_main);
    CBlockIndex* genesis = chainActive.Genesis();
    ASSERT_NE(nullptr, genesis);
    const uint256 genesisHash = Params().GenesisBlock().GetHash();
    ASSERT_EQ(genesisHash, genesis->GetBlockHash());
    const uint64_t originalDataPosition = genesis->nDataPos;
    const uint64_t undoPosition = (uint64_t{1} << 32) + 123;
    genesis->nUndoPos = undoPosition;
    genesis->nStatus |= BLOCK_HAVE_UNDO;

    CBlockFileInfo info;
    ASSERT_TRUE(pblocktree->ReadBlockFileInfo(genesis->nFile, info));
    info.nUndoSize = undoPosition + 512;
    const std::vector<std::pair<int, const CBlockFileInfo*>> files{{genesis->nFile, &info}};
    ASSERT_TRUE(pblocktree->WriteBatchSync(files, genesis->nFile, {genesis}));
    UnloadBlockIndex();
    ASSERT_TRUE(LoadBlockIndex(false));
    const auto loaded = mapBlockIndex.find(genesisHash);
    ASSERT_NE(mapBlockIndex.end(), loaded);
    EXPECT_EQ(genesisHash, loaded->second->GetBlockHash());
    EXPECT_EQ(originalDataPosition, loaded->second->GetBlockPos().nPos);
    EXPECT_EQ(undoPosition, loaded->second->GetUndoPos().nPos);
    CBlockFileInfo loadedInfo;
    ASSERT_TRUE(pblocktree->ReadBlockFileInfo(loaded->second->nFile, loadedInfo));
    EXPECT_EQ(info.nUndoSize, loadedInfo.nUndoSize);
    EXPECT_EQ(info.nSize + info.nUndoSize, CalculateCurrentUsage());
}

TEST_F(UndoStorageTest, AppendsAcrossFourGiBWithoutWrapping)
{
    LOCK(cs_main);
    const uint64_t offset = (uint64_t{1} << 32) - 16;
    LoadUndoSize(offset);
    ASSERT_TRUE(PrepareUndoFile(0));
    CBlockUndo undo;
    undo.old_sprout_tree_root = GetRandHash();
    const unsigned int recordSize = GetSerializeSize(undo, SER_DISK, CLIENT_VERSION) + 40;
    CValidationState state;
    CDiskBlockPos position;
    ASSERT_TRUE(FindUndoPos(state, 0, position, recordSize));
    EXPECT_EQ(offset, position.nPos);
    EXPECT_EQ(offset + recordSize, CalculateCurrentUsage());
    const uint256 previousBlock = GetRandHash();
    ASSERT_TRUE(UndoWriteToDisk(undo, position, previousBlock, Params().MessageStart()));
    CBlockUndo loaded;
    ASSERT_TRUE(UndoReadFromDisk(loaded, position, previousBlock));
    EXPECT_EQ(undo.old_sprout_tree_root, loaded.old_sprout_tree_root);
}

TEST_F(UndoStorageTest, FailedAllocationDoesNotPublishWrappedMetadata)
{
    LOCK(cs_main);
    const uint64_t maxOffset = std::numeric_limits<int64_t>::max();
    for (const uint64_t offset : {maxOffset - 16, std::numeric_limits<uint64_t>::max() - 16}) {
        LoadUndoSize(offset);
        const CDiskBlockPos original(9, 17);
        CDiskBlockPos position = original;
        CValidationState state;
        EXPECT_FALSE(FindUndoPos(state, 0, position, 64));
        EXPECT_EQ(original, position);
        EXPECT_EQ(offset, CalculateCurrentUsage());
    }
    // The append itself fits, but rounding its allocation past the signed
    // file-position range must also fail before publishing a new size.
    LoadUndoSize(maxOffset - 2);
    CValidationState state;
    CDiskBlockPos position(9, 17);
    EXPECT_FALSE(FindUndoPos(state, 0, position, 1));
    EXPECT_EQ(CDiskBlockPos(9, 17), position);
    EXPECT_EQ(maxOffset - 2, CalculateCurrentUsage());
}

TEST_F(UndoStorageTest, ChecksummedUndoRecordsCrossTwoAndFourGiBBoundaries)
{
    ASSERT_TRUE(PrepareUndoFile(7));
    CBlockUndo undo;
    undo.old_sprout_tree_root = GetRandHash();
    undo.vtxundo.resize(1);
    undo.vtxundo[0].vprevout.emplace_back(CTxOut(123, CScript() << OP_TRUE), true, 17, 1);
    const uint256 previousBlock = GetRandHash();
    for (uint64_t offset : {(uint64_t{1} << 31) - 4, (uint64_t{1} << 32) - 4, (uint64_t{1} << 32) + 128}) {
        CDiskBlockPos position(7, offset);
        ASSERT_TRUE(UndoWriteToDisk(undo, position, previousBlock, Params().MessageStart()));
        EXPECT_EQ(offset + MESSAGE_START_SIZE + sizeof(unsigned int), position.nPos);
        CBlockUndo loaded;
        ASSERT_TRUE(UndoReadFromDisk(loaded, position, previousBlock));
        EXPECT_EQ(StorageHex(undo), StorageHex(loaded));
        EXPECT_FALSE(UndoReadFromDisk(loaded, position, GetRandHash()));
    }
}

TEST_F(UndoStorageTest, LargeFileSeekBufferedReadAndTruncatePreservePositions)
{
    const auto path = pathTemp / "sparse-file.dat";
    CAutoFile file(fopen(path.string().c_str(), "w+b"), SER_DISK, CLIENT_VERSION);
    ASSERT_FALSE(file.IsNull());
    ASSERT_TRUE(PrepareLargeFile(file.Get()));
    const uint64_t offset = (uint64_t{1} << 32) + 19;
    ASSERT_TRUE(FileSeek(file.Get(), offset, SEEK_SET));
    const unsigned char value = 0x5a;
    ASSERT_EQ(1u, fwrite(&value, 1, 1, file.Get()));
    ASSERT_EQ(0, fflush(file.Get()));
    EXPECT_EQ(static_cast<int64_t>(offset + 1), FileTell(file.Get()));
    ASSERT_TRUE(TruncateFile(file.Get(), offset + 3));
    EXPECT_EQ(offset + 3, boost::filesystem::file_size(path));
    EXPECT_FALSE(TruncateFile(file.Get(), std::numeric_limits<uint64_t>::max()));
    EXPECT_EQ(offset + 3, boost::filesystem::file_size(path));
    file.fclose();

    CBufferedFile buffered(fopen(path.string().c_str(), "rb"), 128, 64, SER_DISK, CLIENT_VERSION);
    ASSERT_TRUE(buffered.Seek(offset));
    EXPECT_EQ(offset, buffered.GetPos());
    unsigned char actual = 0;
    buffered.read_u8(&actual, 1);
    EXPECT_EQ(value, actual);
    EXPECT_FALSE(buffered.Seek(std::numeric_limits<uint64_t>::max()));
    EXPECT_EQ(offset + 1, buffered.GetPos());
}


TEST_F(UndoStorageTest, DisconnectAboveFourGiBRestoresCoinsShieldedRootsAndNullifiers)
{
    LOCK(cs_main);
    struct RestoreUpgrades {
        std::array<int, 3> heights;
        ~RestoreUpgrades()
        {
            for (size_t i = 0; i < heights.size(); ++i)
                UpdateNetworkUpgradeParameters(static_cast<Consensus::UpgradeIndex>(Consensus::UPGRADE_OVERWINTER + i), heights[i]);
        }
    } restore;
    for (size_t i = 0; i < restore.heights.size(); ++i) {
        const auto upgrade = static_cast<Consensus::UpgradeIndex>(Consensus::UPGRADE_OVERWINTER + i);
        restore.heights[i] = Params().GetConsensus().vUpgrades[upgrade].nActivationHeight;
        UpdateNetworkUpgradeParameters(upgrade, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    }
    const uint32_t branch = NetworkUpgradeInfo[Consensus::UPGRADE_IRONWOOD].nBranchId;

    SproutMerkleTree sprout;
    SaplingMerkleTree sapling;
    SaplingMerkleFrontier saplingFrontier;
    IronwoodMerkleFrontier ironwoodFrontier;
    sprout.append(uint256S("03"));
    sapling.append(uint256S("03"));
    saplingFrontier.append(uint256S("03"));
    ironwoodFrontier.append(uint256S("03"));
    pcoinsTip->PushAnchor(sprout);
    pcoinsTip->PushAnchor(sapling);
    pcoinsTip->PushAnchor(saplingFrontier);
    pcoinsTip->PushAnchor(ironwoodFrontier);
    const uint256 oldSprout = sprout.root();
    const uint256 oldSapling = sapling.root();
    const uint256 oldIronwood = ironwoodFrontier.root();
    ASSERT_EQ(oldSapling, saplingFrontier.root());

    CMutableTransaction funding;
    funding.vin.emplace_back(COutPoint(GetRandHash(), 0));
    funding.vout.emplace_back(0, CScript() << OP_TRUE);
    const CTransaction funded(funding);
    pcoinsTip->ModifyCoins(funded.GetHash())->FromTx(funded, 1);
    const CCoins expectedRestored(funded, 1);

    CMutableTransaction spend;
    spend.fOverwintered = true;
    spend.nVersionGroupId = IRONWOOD_VERSION_GROUP_ID;
    spend.nVersion = IRONWOOD_TX_VERSION;
    spend.nConsensusBranchId = branch;
    spend.vin.emplace_back(COutPoint(funded.GetHash(), 0));
    spend.vout.emplace_back(0, CScript() << OP_TRUE);
    // This fixture exercises storage and rollback, not proof verification.
    // Existing transaction validation tests cover rejecting invalid proofs.
    spend.saplingBundle = sapling::test_only_invalid_bundle(1, 1, 0);
    const uint256 saplingNullifier = uint256::FromRawBytes(spend.saplingBundle.GetDetails().spends()[0].nullifier());
    CKeyingMaterial seedBytes(32, 7);
    const auto spendingKey = libzcash::IronwoodExtendedSpendingKeyPirate::Master(HDSeed(seedBytes));
    libzcash::IronwoodPaymentAddress address;
    ASSERT_TRUE(spendingKey.sk.DeriveDefaultAddress(&address));
    ironwood::Builder builder(true, true, uint256());
    ASSERT_TRUE(builder.AddOutput(std::nullopt, address, 0));
    const auto randomKey = libzcash::IronwoodSpendingKey().random();
    ASSERT_TRUE(randomKey.has_value());
    auto unauthorized = builder.Build();
    ASSERT_TRUE(unauthorized.has_value());
    auto authorized = unauthorized->ProveAndSign({randomKey.value()}, uint256());
    ASSERT_TRUE(authorized.has_value());
    spend.ironwoodBundle = std::move(authorized.value());
    const auto ironwoodNullifiers = spend.ironwoodBundle.GetNullifiers();
    ASSERT_FALSE(ironwoodNullifiers.empty());

    CMutableTransaction coinbase;
    coinbase.vin.emplace_back();
    coinbase.vin[0].prevout.SetNull();
    coinbase.vout.emplace_back(0, CScript() << OP_TRUE);
    CBlock block;
    block.nTime = 1234567890;
    block.nBits = 0x1f07ffff;
    block.vtx.emplace_back(coinbase);
    block.vtx.emplace_back(spend);
    block.hashMerkleRoot = block.BuildMerkleTree();
    const uint256 blockHash = block.GetHash();
    const uint256 parentHash = GetRandHash();
    CBlockIndex parent;
    parent.phashBlock = &parentHash;
    parent.nHeight = 1;
    parent.hashFinalSaplingRoot = oldSapling;
    parent.hashFinalIronwoodRoot = oldIronwood;
    CBlockIndex index(block);
    index.phashBlock = &blockHash;
    index.pprev = &parent;
    index.nHeight = 2;
    index.nCachedBranchId = branch;
    index.nStatus = BLOCK_HAVE_UNDO;
    index.nFile = 7;

    CBlockUndo undo;
    undo.old_sprout_tree_root = oldSprout;
    undo.vtxundo.resize(1);
    CTxUndo unusedCoinbaseUndo;
    UpdateCoins(block.vtx[0], *pcoinsTip, unusedCoinbaseUndo, index.nHeight);
    UpdateCoins(block.vtx[1], *pcoinsTip, undo.vtxundo[0], index.nHeight);
    CCoins coins;
    EXPECT_FALSE(pcoinsTip->HaveCoins(funded.GetHash()));
    ASSERT_TRUE(pcoinsTip->GetNullifier(saplingNullifier, SAPLINGFRONTIER));
    for (const auto& nullifier : ironwoodNullifiers)
        ASSERT_TRUE(pcoinsTip->GetNullifier(nullifier, IRONWOODFRONTIER));

    sprout.append(uint256S("05"));
    sapling.append(uint256S("05"));
    saplingFrontier.append(uint256S("05"));
    ironwoodFrontier.append(uint256S("05"));
    pcoinsTip->PushAnchor(sprout);
    pcoinsTip->PushAnchor(sapling);
    pcoinsTip->PushAnchor(saplingFrontier);
    pcoinsTip->PushAnchor(ironwoodFrontier);
    pcoinsTip->PushHistoryNode(branch, libzcash::NewV2Leaf(blockHash, block.nTime, block.nBits,
        saplingFrontier.root(), ironwoodFrontier.root(), uint256S("01"), index.nHeight, 1, 1));
    ASSERT_EQ(1u, pcoinsTip->GetHistoryLength(branch));
    pcoinsTip->SetBestBlock(blockHash);

    ASSERT_TRUE(PrepareUndoFile(index.nFile));
    CDiskBlockPos position(index.nFile, (uint64_t{1} << 32) + 41);
    ASSERT_TRUE(UndoWriteToDisk(undo, position, parentHash, Params().MessageStart()));
    index.nUndoPos = position.nPos;
    ASSERT_GT(index.GetUndoPos().nPos, uint64_t{1} << 32);
    CValidationState state;
    bool clean = false;
    ASSERT_TRUE(DisconnectBlock(block, state, &index, *pcoinsTip, &clean));
    EXPECT_TRUE(clean);
    ASSERT_TRUE(pcoinsTip->GetCoins(funded.GetHash(), coins));
    EXPECT_EQ(expectedRestored, coins);
    EXPECT_FALSE(pcoinsTip->HaveCoins(block.vtx[0].GetHash()));
    EXPECT_FALSE(pcoinsTip->HaveCoins(block.vtx[1].GetHash()));
    EXPECT_FALSE(pcoinsTip->GetNullifier(saplingNullifier, SAPLINGFRONTIER));
    for (const auto& nullifier : ironwoodNullifiers)
        EXPECT_FALSE(pcoinsTip->GetNullifier(nullifier, IRONWOODFRONTIER));
    EXPECT_EQ(oldSprout, pcoinsTip->GetBestAnchor(SPROUT));
    EXPECT_EQ(oldSapling, pcoinsTip->GetBestAnchor(SAPLING));
    EXPECT_EQ(oldSapling, pcoinsTip->GetBestAnchor(SAPLINGFRONTIER));
    EXPECT_EQ(oldIronwood, pcoinsTip->GetBestAnchor(IRONWOODFRONTIER));
    EXPECT_EQ(parentHash, pcoinsTip->GetBestBlock());
    EXPECT_EQ(0u, pcoinsTip->GetHistoryLength(branch));
}

} // namespace
