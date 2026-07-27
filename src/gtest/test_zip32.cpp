// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <gtest/gtest.h>
#include <gmock/gmock.h>

//#include "miner.h"
#include "zcash/address/zip32.h"

// ZIP 32 hierarchical deterministic key derivation. TestVectors below covers
// SaplingExtendedSpendingKey derivation against known-good vectors (master
// key, hardened child derivation, XFVK conversion). IronwoodStructuralDerivation
// covers IronwoodExtendedSpendingKeyPirate::Derive, this fork's own
// second-generation shielded pool - there are no published known-good test
// vectors for it (it's Pirate-specific, not an upstream Zcash ZIP 32 scheme),
// so that test checks structural invariants (depth/chaincode change on
// derivation, distinct addresses per account index) rather than exact values.

// Derivation path is m -> m/1h -> m/1h/2h, following
// https://github.com/zcash-hackworks/zcash-test-vectors/blob/master/zcash_test_vectors/zip_0032.py,
// which derives every level with hardened() (index | 0x80000000). Sapling spending-key
// derivation is hardened-only: the vendored sapling-crypto crate's ChildIndex::from_index
// requires the hardened bit set and panics otherwise, so this must be passed explicitly
// via HARDENED_KEY_LIMIT rather than relying on implicit hardening.
// Sapling consistently uses little-endian encoding, but uint256S takes its input in
// big-endian byte order, so the test vectors below are byte-reversed.
TEST(ZIP32, TestVectors) {
    std::vector<unsigned char, secure_allocator<unsigned char>> rawSeed {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
        17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
    HDSeed seed(rawSeed);

    auto m = libzcash::SaplingExtendedSpendingKey::Master(seed, false);
    EXPECT_EQ(m.depth, 0);
    EXPECT_EQ(m.parentFVKTag, 0);
    EXPECT_EQ(m.childIndex, 0);
    EXPECT_EQ(
        m.chaincode,
        uint256S("8e661820750d557e8b34733ebf7ecdfdf31c6d27724fb47aa372bf034b7c94d0"));
    EXPECT_EQ(
        m.expsk.ask,
        uint256S("06257454c907f6510ba1c1830ebf60657760a8869ee968a2b93260d3930cc0b6"));
    EXPECT_EQ(
        m.expsk.nsk,
        uint256S("06ea21888a749fd38eb443d20a030abd2e6e997f5db4f984bd1f2f3be8ed0482"));
    EXPECT_EQ(
        m.expsk.ovk,
        uint256S("21fb4adfa42183848306ffb27719f27d76cf9bb81d023c93d4b9230389845839"));
    EXPECT_EQ(
        m.dk,
        uint256S("72a196f93e8abc0935280ea2a96fa57d6024c9913e0f9fb3af96775bb77cc177"));
    EXPECT_THAT(
        m.ToXFVK().DefaultAddress().d,
        testing::ElementsAreArray({ 0xd8, 0x62, 0x1b, 0x98, 0x1c, 0xf3, 0x00, 0xe9, 0xd4, 0xcc, 0x89 }));

    // m/1h
    auto m_1 = m.Derive(1 | HARDENED_KEY_LIMIT);
    EXPECT_EQ(m_1.depth, 1);
    EXPECT_EQ(m_1.parentFVKTag, 0x3a71c214);
    EXPECT_EQ(m_1.childIndex, 1 | HARDENED_KEY_LIMIT);
    EXPECT_EQ(
        m_1.chaincode,
        uint256S("dbaeca68fd2ef8b45ec23ee91bd694aa2759e010c668bb3e066b20a845aacc6f"));
    EXPECT_EQ(
        m_1.expsk.ask,
        uint256S("04bd31e1a6218db693ff0802f029043ec20f3b0b8b148cdc04be7afb2ee9f7d5"));
    EXPECT_EQ(
        m_1.expsk.nsk,
        uint256S("0a75e557f6fcbf672e0134d4ec2d51a3f358659b4b5c46f303e6cb22687c2a37"));
    EXPECT_EQ(
        m_1.expsk.ovk,
        uint256S("691c33ec470a1697ca37ceb237bb7f1691d2a833543514cf1f8c343319763025"));
    EXPECT_EQ(
        m_1.dk,
        uint256S("26d53444cbe2e9929f619d810a0d05ae0deece0a72c3a7e3df9a5fd60f4088f2"));
    EXPECT_THAT(
        m_1.ToXFVK().DefaultAddress().d,
        testing::ElementsAreArray({ 0xbc, 0xc3, 0x23, 0xe8, 0xda, 0x39, 0xb4, 0x96, 0xc0, 0x50, 0x51 }));

    // m/1h/2h
    auto m_1_2h = m_1.Derive(2 | HARDENED_KEY_LIMIT);
    EXPECT_EQ(m_1_2h.depth, 2);
    EXPECT_EQ(m_1_2h.parentFVKTag, 0xcb238476);
    EXPECT_EQ(m_1_2h.childIndex, 2 | HARDENED_KEY_LIMIT);
    EXPECT_EQ(
        m_1_2h.chaincode,
        uint256S("daf7be6f80503ab34f14f236da9de2cf540ae3c100f520607980d0756c087944"));
    EXPECT_EQ(
        m_1_2h.expsk.ask,
        uint256S("06512f33a6f9ae4b42fd71f9cfa08d3727522dd3089cad596fc3139eb65df37f"));
    EXPECT_EQ(
        m_1_2h.expsk.nsk,
        uint256S("00debf5999f564a3e05a0d418cf40714399a32c1bdc98ba2eb4439a0e46e9c77"));
    EXPECT_EQ(
        m_1_2h.expsk.ovk,
        uint256S("ac85619305763dc29b67b75e305e5323bda7d6a530736a88417f90bf0171fcd9"));
    EXPECT_EQ(
        m_1_2h.dk,
        uint256S("d148325ff6faa682558de97a9fec61dd8dc10a96d0cd214bc531e0869a9e69e4"));
    EXPECT_THAT(
        m_1_2h.ToXFVK().DefaultAddress().d,
        testing::ElementsAreArray({ 0x98, 0x82, 0x40, 0xce, 0xa4, 0xdb, 0xc3, 0x0a, 0x73, 0x75, 0x50 }));

    // Full viewing key for m/1h/2h, derived directly rather than via non-hardened
    // xfvk derivation (see note below), so it must match m_1_2h field-for-field.
    auto m_1_2hv = m_1_2h.ToXFVK();
    EXPECT_EQ(m_1_2hv.depth, 2);
    EXPECT_EQ(m_1_2hv.parentFVKTag, 0xcb238476);
    EXPECT_EQ(m_1_2hv.childIndex, 2 | HARDENED_KEY_LIMIT);
    EXPECT_EQ(
        m_1_2hv.chaincode,
        uint256S("daf7be6f80503ab34f14f236da9de2cf540ae3c100f520607980d0756c087944"));
    EXPECT_EQ(
        m_1_2hv.fvk.ak,
        uint256S("4eab7275725c76ee4247ae8d941d4b53682e39da641785e097377144953f859a"));
    EXPECT_EQ(
        m_1_2hv.fvk.nk,
        uint256S("be4f5d4f36018511d23a1b9a9c87af8c6dbd20212da84121c1ce884f8aa266f1"));
    EXPECT_EQ(
        m_1_2hv.fvk.ovk,
        uint256S("ac85619305763dc29b67b75e305e5323bda7d6a530736a88417f90bf0171fcd9"));
    EXPECT_EQ(
        m_1_2hv.dk,
        uint256S("d148325ff6faa682558de97a9fec61dd8dc10a96d0cd214bc531e0869a9e69e4"));
    EXPECT_EQ(m_1_2hv.DefaultAddress(), m_1_2h.ToXFVK().DefaultAddress());

    // librustzcash_zip32_xfvk_derive is a documented stub in the vendored
    // sapling-crypto version ("XFVK child derivation is not supported in
    // sapling-crypto 0.7") and unconditionally returns false, so both
    // hardened and non-hardened derivation from an xfvk currently fail.
    EXPECT_FALSE(m_1_2hv.Derive(3 | HARDENED_KEY_LIMIT));
    EXPECT_FALSE(m_1_2hv.Derive(3));
}

// IronwoodExtendedSpendingKeyPirate::Derive was never called anywhere in the
// gtest suite prior to this - every other test touching an Ironwood extended
// key only called Master(). No published test vectors exist for this scheme
// (see file comment above), so this checks derivation actually changes the
// key material deterministically and distinct accounts yield distinct keys,
// rather than asserting exact byte values.
TEST(ZIP32, IronwoodStructuralDerivation) {
    std::vector<unsigned char, secure_allocator<unsigned char>> rawSeed {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
        17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
    HDSeed seed(rawSeed);

    auto m = libzcash::IronwoodExtendedSpendingKeyPirate::Master(seed, false);
    EXPECT_EQ(m.depth, 0);
    EXPECT_EQ(m.parentFVKTag, 0);
    EXPECT_EQ(m.childIndex, 0);

    uint32_t bip44CoinType = 133; // Pirate's registered SLIP-44 coin type
    auto account0Opt = m.Derive(bip44CoinType, 0);
    ASSERT_TRUE(account0Opt.has_value());
    auto account0 = account0Opt.value();

    // Derivation must actually move away from the master key.
    EXPECT_NE(account0.depth, m.depth);
    EXPECT_NE(account0.chaincode, m.chaincode);
    EXPECT_FALSE(account0.sk == m.sk);

    // Deterministic: deriving the same (coin type, account) twice from the
    // same master key must yield an identical child key.
    auto account0AgainOpt = m.Derive(bip44CoinType, 0);
    ASSERT_TRUE(account0AgainOpt.has_value());
    EXPECT_TRUE(account0 == account0AgainOpt.value());

    // Distinct accounts must yield distinct keys and distinct addresses.
    auto account1Opt = m.Derive(bip44CoinType, 1);
    ASSERT_TRUE(account1Opt.has_value());
    auto account1 = account1Opt.value();
    EXPECT_FALSE(account0 == account1);

    libzcash::IronwoodPaymentAddress addr0, addr1;
    ASSERT_TRUE(account0.sk.DeriveDefaultAddress(&addr0));
    ASSERT_TRUE(account1.sk.DeriveDefaultAddress(&addr1));
    EXPECT_FALSE(addr0 == addr1);

    // FVK conversion must carry over the same depth/chaincode/childIndex as
    // the spending key it was derived from (mirrors the Sapling ToXFVK check
    // above, via IronwoodExtendedSpendingKeyPirate::GetXFVK instead).
    auto fvkOpt = account0.GetXFVK();
    ASSERT_TRUE(fvkOpt.has_value());
    EXPECT_EQ(fvkOpt.value().depth, account0.depth);
    EXPECT_EQ(fvkOpt.value().chaincode, account0.chaincode);
    EXPECT_EQ(fvkOpt.value().childIndex, account0.childIndex);
}
