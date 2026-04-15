// Copyright (c) 2018 The Zcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "zip32.h"

#include "hash.h"
#include "random.h"
#include "streams.h"
#include "version.h"
#include <rust/bridge.h>

#include <librustzcash.h>
#include <sodium.h>

const unsigned char ZCASH_HD_SEED_FP_ENCRYTION[crypto_generichash_blake2b_PERSONALBYTES] =
    {'P', 'i', 'r', 'a', 't', 'e', 'E', 'n', 'c', 'r', 'y', 'p', 't','_','F', 'P'};

const unsigned char ZCASH_HD_SEED_FP_PERSONAL[crypto_generichash_blake2b_PERSONALBYTES] =
    {'Z', 'c', 'a', 's', 'h', '_', 'H', 'D', '_', 'S', 'e', 'e', 'd', '_', 'F', 'P'};

HDSeed HDSeed::Random(size_t len)
{
    assert(len == 32);
    std::array<uint8_t, 32> buf;
    hd_seed::random_bytes(buf);
    RawHDSeed rawSeed(buf.begin(), buf.end());
    return HDSeed(rawSeed);
}

HDSeed HDSeed::RestoreFromPhrase(std::string &phrase, uint32_t langCode)
{
    bool bResult;
    auto lang = static_cast<hd_seed::MnemonicLanguage>(langCode);

    //Count the nr of words in the phrase:
    std::stringstream stream( phrase );
    unsigned int iCount = std::distance(std::istream_iterator<std::string>(stream), std::istream_iterator<std::string>());

    if (iCount==12) //12 word mnemonic: 16 byte entropy
    {
      RawHDSeed restoredSeed(16, 0);
      bResult = hd_seed::phrase_to_entropy(phrase, lang, rust::Slice<uint8_t>{restoredSeed.data(), 16});
      if (bResult==false)
      {
        throw std::runtime_error("phrase_to_entropy(): Restore from 12-word phrase failed");
      }
      return HDSeed(restoredSeed);
    }
    else if (iCount==18) //18 word mnemonic : 24 byte entropy
    {
      RawHDSeed restoredSeed(24, 0);
      bResult = hd_seed::phrase_to_entropy(phrase, lang, rust::Slice<uint8_t>{restoredSeed.data(), 24});
      if (bResult==false)
      {
        throw std::runtime_error("phrase_to_entropy(): Restore from 18-word phrase failed");
      }
      return HDSeed(restoredSeed);
    }
    else //24 word mnemonic: 32 byte entropy
    {
      RawHDSeed restoredSeed(32, 0);
      bResult = hd_seed::phrase_to_entropy(phrase, lang, rust::Slice<uint8_t>{restoredSeed.data(), 32});
      if (bResult==false)
      {
        throw std::runtime_error("phrase_to_entropy(): Restore from 24-word phrase failed");
      }
      return HDSeed(restoredSeed);
    }
}

bool HDSeed::IsValidPhrase(std::string &phrase, uint32_t langCode)
{
    auto lang = static_cast<hd_seed::MnemonicLanguage>(langCode);
    //Count the nr of words in the phrase:
    std::stringstream stream(phrase);
    unsigned int iCount = std::distance(std::istream_iterator<std::string>(stream), std::istream_iterator<std::string>());

    if (iCount==12) //12 word mnemonic: 16 byte entropy
    {
      RawHDSeed restoredSeed(16, 0);
      return hd_seed::phrase_to_entropy(phrase, lang, rust::Slice<uint8_t>{restoredSeed.data(), 16});
    }
    else if (iCount==18) //18 word mnemonic : 24 byte entropy
    {
      RawHDSeed restoredSeed(24, 0);
      return hd_seed::phrase_to_entropy(phrase, lang, rust::Slice<uint8_t>{restoredSeed.data(), 24});
    }
    else if (iCount==24) //24 word mnemonic: 32 byte entropy
    {
      RawHDSeed restoredSeed(32, 0);
      return hd_seed::phrase_to_entropy(phrase, lang, rust::Slice<uint8_t>{restoredSeed.data(), 32});
    }
    else
    {
      printf("Invalid number of words in the phrase\n");
      return false;
    }
}

void HDSeed::GetPhrase(std::string &phrase, uint32_t langCode)
{
    auto rawSeed = this->RawSeed();
    auto lang = static_cast<hd_seed::MnemonicLanguage>(langCode);
    phrase = std::string(hd_seed::entropy_to_phrase(
        rust::Slice<const uint8_t>{rawSeed.data(), rawSeed.size()},
        lang));
}

uint256 HDSeed::Fingerprint() const
{
    CBLAKE2bWriter h(SER_GETHASH, 0, ZCASH_HD_SEED_FP_PERSONAL);
    h << seed;
    return h.GetHash();
}

uint256 HDSeed::EncryptionFingerprint() const
{
    CBLAKE2bWriter h(SER_GETHASH, 0, ZCASH_HD_SEED_FP_ENCRYTION);
    h << seed;
    return h.GetHash();
}

uint256 ovkForShieldingFromTaddr(HDSeed& seed) {
    auto rawSeed = seed.RawSeed();
    std::array<unsigned char, 32> ovk_out;
    transparent_keys::ovk_for_shielding_from_taddr(
        rust::Slice<const uint8_t>{rawSeed.data(), rawSeed.size()},
        ovk_out);
    uint256 ovk;
    memcpy(ovk.begin(), ovk_out.data(), 32);
    return ovk;
}

namespace libzcash {

std::optional<SaplingExtendedFullViewingKey> SaplingExtendedFullViewingKey::Derive(uint32_t i) const
{
    CDataStream ss_p(SER_NETWORK, PROTOCOL_VERSION);
    ss_p << *this;
    CSerializeData p_bytes(ss_p.begin(), ss_p.end());

    CSerializeData i_bytes(SAPLING_ZIP32_XFVK_SIZE);
    if (librustzcash_zip32_xfvk_derive(
        reinterpret_cast<unsigned char*>(p_bytes.data()),
        i,
        reinterpret_cast<unsigned char*>(i_bytes.data())
    )) {
        CDataStream ss_i(i_bytes, SER_NETWORK, PROTOCOL_VERSION);
        SaplingExtendedFullViewingKey xfvk_i;
        ss_i >> xfvk_i;
        return xfvk_i;
    } else {
        return std::nullopt;
    }
}

std::optional<std::pair<diversifier_index_t, libzcash::SaplingPaymentAddress>>
    SaplingExtendedFullViewingKey::Address(diversifier_index_t j) const
{
    CDataStream ss_xfvk(SER_NETWORK, PROTOCOL_VERSION);
    ss_xfvk << *this;
    CSerializeData xfvk_bytes(ss_xfvk.begin(), ss_xfvk.end());

    diversifier_index_t j_ret;
    CSerializeData addr_bytes(libzcash::SerializedSaplingPaymentAddressSize);
    if (librustzcash_zip32_xfvk_address(
        reinterpret_cast<unsigned char*>(xfvk_bytes.data()),
        j.begin(), j_ret.begin(),
        reinterpret_cast<unsigned char*>(addr_bytes.data()))) {
        CDataStream ss_addr(addr_bytes, SER_NETWORK, PROTOCOL_VERSION);
        libzcash::SaplingPaymentAddress addr;
        ss_addr >> addr;
        return std::make_pair(j_ret, addr);
    } else {
        return std::nullopt;
    }
}

libzcash::SaplingPaymentAddress SaplingExtendedFullViewingKey::DefaultAddress() const
{
    diversifier_index_t j0;
    auto addr = Address(j0);
    // If we can't obtain a default address, we are *very* unlucky...
    if (!addr) {
        throw std::runtime_error("SaplingExtendedFullViewingKey::DefaultAddress(): No valid diversifiers out of 2^88!");
    }
    return addr.value().second;
}

bool SaplingExtendedFullViewingKey::DeriveIVKinternal(libzcash::SaplingIncomingViewingKey* ivk) const
{
    // Build the 128-byte DFVK: fvk (96 bytes) followed by dk (32 bytes).
    // sapling_derive_internal_fvk hashes fvk||dk, so the real dk is essential.
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION);

    SaplingDiversifiableFullViewingKey_FFI_t dfvk_t;
    SaplingIncomingViewingKey_FFI_t ivk_t;

    ss << fvk;
    ss << dk;
    ss >> dfvk_t;

    bool rustCompleted = sapling_keys::dfvk_to_ivk_internal(dfvk_t, ivk_t);

    if (rustCompleted) {
        rs << ivk_t;
        rs >> *ivk;
    }

    memory_cleanse(ss.data(), ss.size());
    memory_cleanse(rs.data(), rs.size());
    memory_cleanse(dfvk_t.data(), dfvk_t.size());
    memory_cleanse(ivk_t.data(), ivk_t.size());

    return rustCompleted;
}

bool SaplingExtendedFullViewingKey::DeriveNKinternal(uint256* nk) const
{
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION);

    SaplingDiversifiableFullViewingKey_FFI_t dfvk_t;
    SaplingIncomingViewingKey_FFI_t nk_t;  // reuse 32-byte type

    ss << fvk;
    ss << dk;
    ss >> dfvk_t;

    bool rustCompleted = sapling_keys::dfvk_to_nk_internal(dfvk_t, nk_t);

    if (rustCompleted) {
        rs << nk_t;
        rs >> *nk;
    }

    memory_cleanse(ss.data(), ss.size());
    memory_cleanse(rs.data(), rs.size());
    memory_cleanse(dfvk_t.data(), dfvk_t.size());
    memory_cleanse(nk_t.data(), nk_t.size());

    return rustCompleted;
}

bool SaplingExtendedFullViewingKey::DeriveOVKinternal(libzcash::SaplingOutgoingViewingKey* ovk) const
{
    // The internal OVK is derived by sapling_derive_internal_fvk(fvk, dk) as r[32..].
    // The real dk is required: zeroing it produces a wrong ovk_internal.
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION);

    SaplingDiversifiableFullViewingKey_FFI_t dfvk_t;
    SaplingOutgoingViewingKey_FFI_t ovk_t;

    ss << fvk;
    ss << dk;
    ss >> dfvk_t;

    bool rustCompleted = sapling_keys::dfvk_to_ovk_internal(dfvk_t, ovk_t);

    if (rustCompleted) {
        rs << ovk_t;
        rs >> *ovk;
    }

    memory_cleanse(ss.data(), ss.size());
    memory_cleanse(rs.data(), rs.size());
    memory_cleanse(dfvk_t.data(), dfvk_t.size());
    memory_cleanse(ovk_t.data(), ovk_t.size());

    return rustCompleted;
}

bool SaplingExtendedFullViewingKey::DeriveAddressInternal(libzcash::SaplingPaymentAddress* addr, diversifier_t diversifier) const
{
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION);

    SaplingDiversifiableFullViewingKey_FFI_t dfvk_t;
    SaplingPaymentAddress_FFI_t address_t;

    ss << fvk;
    ss << dk;
    ss >> dfvk_t;

    bool rustCompleted = sapling_keys::dfvk_to_address_internal(dfvk_t, diversifier, address_t);

    if (rustCompleted) {
        rs << address_t;
        rs >> *addr;
    }

    memory_cleanse(ss.data(), ss.size());
    memory_cleanse(rs.data(), rs.size());
    memory_cleanse(dfvk_t.data(), dfvk_t.size());
    memory_cleanse(address_t.data(), address_t.size());

    return rustCompleted;
}

bool SaplingExtendedFullViewingKey::DeriveAddressFromIndexInternal(libzcash::SaplingPaymentAddress* addr, blob88 diversifier_index) const
{
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION);
    CDataStream di_ss(SER_NETWORK, PROTOCOL_VERSION);

    SaplingDiversifiableFullViewingKey_FFI_t dfvk_t;
    SaplingPaymentAddress_FFI_t address_t;
    uint88_t di_t;

    ss << fvk;
    ss << dk;
    ss >> dfvk_t;

    di_ss << diversifier_index;
    di_ss >> di_t;

    bool rustCompleted = sapling_keys::dfvk_to_address_from_index_internal(dfvk_t, di_t, address_t);

    if (rustCompleted) {
        rs << address_t;
        rs >> *addr;
    }

    memory_cleanse(ss.data(), ss.size());
    memory_cleanse(rs.data(), rs.size());
    memory_cleanse(di_ss.data(), di_ss.size());
    memory_cleanse(dfvk_t.data(), dfvk_t.size());
    memory_cleanse(address_t.data(), address_t.size());
    memory_cleanse(di_t.data(), di_t.size());

    return rustCompleted;
}

bool SaplingExtendedFullViewingKey::DefaultAddressInternal(libzcash::SaplingPaymentAddress* addr) const
{
    // Derive the change address using the proper ZIP 32 internal dk derivation.
    // Serializes as [fvk(96) || dk(32)] = 128-byte DFVK and calls the Rust
    // DiversifiableFullViewingKey::change_address() which derives dk_internal
    // via sapling_derive_internal_fvk, producing a visually distinct address.
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION);

    SaplingDiversifiableFullViewingKey_FFI_t dfvk_t;
    SaplingPaymentAddress_FFI_t address_t;

    // Build the 128-byte DFVK: fvk (96 bytes) followed by dk (32 bytes)
    ss << fvk;
    ss << dk;
    ss >> dfvk_t;

    bool rustCompleted = sapling_keys::dfvk_to_change_address(dfvk_t, address_t);

    if (rustCompleted) {
        rs << address_t;
        rs >> *addr;
    }

    memory_cleanse(ss.data(), ss.size());
    memory_cleanse(rs.data(), rs.size());
    memory_cleanse(dfvk_t.data(), dfvk_t.size());
    memory_cleanse(address_t.data(), address_t.size());

    return rustCompleted;
}

SaplingExtendedSpendingKey SaplingExtendedSpendingKey::Master(const HDSeed& seed, bool bip39Enabled)
{
    auto rawSeed = seed.RawSeed();
    CSerializeData m_bytes(SAPLING_ZIP32_XSK_SIZE);

    if (bip39Enabled) {
        std::array<uint8_t, 64> bip39_seed;
        hd_seed::entropy_to_bip39_seed(
            rust::Slice<const uint8_t>{rawSeed.data(), rawSeed.size()},
            bip39_seed);
        librustzcash_zip32_xsk_master(
            bip39_seed.data(),
            64,
            reinterpret_cast<unsigned char*>(m_bytes.data()));
    } else {
        librustzcash_zip32_xsk_master(
            rawSeed.data(),
            rawSeed.size(),
            reinterpret_cast<unsigned char*>(m_bytes.data()));
    }


    CDataStream ss(m_bytes, SER_NETWORK, PROTOCOL_VERSION);
    SaplingExtendedSpendingKey xsk_m;
    ss >> xsk_m;
    return xsk_m;
}

SaplingExtendedSpendingKey SaplingExtendedSpendingKey::Derive(uint32_t i) const
{
    CDataStream ss_p(SER_NETWORK, PROTOCOL_VERSION);
    ss_p << *this;
    CSerializeData p_bytes(ss_p.begin(), ss_p.end());

    CSerializeData i_bytes(SAPLING_ZIP32_XSK_SIZE);
    librustzcash_zip32_xsk_derive(
        reinterpret_cast<unsigned char*>(p_bytes.data()),
        i,
        reinterpret_cast<unsigned char*>(i_bytes.data()));

    CDataStream ss_i(i_bytes, SER_NETWORK, PROTOCOL_VERSION);
    SaplingExtendedSpendingKey xsk_i;
    ss_i >> xsk_i;
    return xsk_i;
}

bool SaplingExtendedSpendingKey::DeriveInternal(SaplingExtendedSpendingKey* xsk_int) const
{
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION);

    SaplingExtendedSpendingKey_FFI_t xsk_in_t;
    SaplingExtendedSpendingKey_FFI_t xsk_out_t;

    ss << *this;
    ss >> xsk_in_t;

    xsk_out_t = sapling_keys::xsk_derive_internal(xsk_in_t);

    rs << xsk_out_t;
    rs >> *xsk_int;

    memory_cleanse(ss.data(), ss.size());
    memory_cleanse(rs.data(), rs.size());
    memory_cleanse(xsk_in_t.data(), xsk_in_t.size());
    memory_cleanse(xsk_out_t.data(), xsk_out_t.size());

    return true;
}

SaplingExtendedFullViewingKey SaplingExtendedSpendingKey::ToXFVK() const
{
    SaplingExtendedFullViewingKey ret;
    ret.depth = depth;
    ret.parentFVKTag = parentFVKTag;
    ret.childIndex = childIndex;
    ret.chaincode = chaincode;
    expsk.DeriveFVK(&ret.fvk);
    ret.dk = dk;
    return ret;
}

libzcash::SaplingPaymentAddress SaplingExtendedSpendingKey::DefaultAddress() const
{
    return ToXFVK().DefaultAddress();
}

OrchardExtendedSpendingKeyPirate OrchardExtendedSpendingKeyPirate::Master(const HDSeed& seed, bool bip39Enabled)
{

    //Datastreams for serialization
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION); //returning stream

    //Tranfer Data
    OrchardExtendedSpendingKey_FFI_t xsk_t_out;

    //Return Type
    OrchardExtendedSpendingKeyPirate xsk;

    //Get raw seed to derive Master Spending key from
    auto rawSeed = seed.RawSeed();
    std::array<uint8_t, 64> bip39_seed = {};

    //Call rust FFI
    if (bip39Enabled) {
        hd_seed::entropy_to_bip39_seed(
            rust::Slice<const uint8_t>{rawSeed.data(), rawSeed.size()},
            bip39_seed);
        orchard_keys::derive_master_key(rust::Slice<const uint8_t>{bip39_seed.data(), 64}, xsk_t_out);
    } else {
        orchard_keys::derive_master_key(rust::Slice<const uint8_t>{rawSeed.data(), rawSeed.size()}, xsk_t_out);
    }

    //Deserialize rust result
    rs << xsk_t_out;
    rs >> xsk;

    //Cleanse the memory of the transfer and serialization objects
    memory_cleanse(rs.data(), rs.size());
    memory_cleanse(xsk_t_out.data(), xsk_t_out.size());
    memory_cleanse(rawSeed.data(), rawSeed.size());
    memory_cleanse(bip39_seed.data(), bip39_seed.size());

    //Return data
    return xsk;

}

std::optional<OrchardExtendedSpendingKeyPirate> OrchardExtendedSpendingKeyPirate::Derive(uint32_t bip44CoinType, uint32_t account) const
{
    //Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); //sending stream
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION); //returning stream

    //Tranfer Data
    OrchardExtendedSpendingKey_FFI_t xsk_t_out;
    OrchardExtendedSpendingKey_FFI_t xsk_t_in;

    //Return Type
    OrchardExtendedSpendingKeyPirate xsk;

    //rust result
    bool rustCompleted;

    //Serialize sending data
    ss << *this;
    ss >> xsk_t_in;

    //Call rust FFI
    rustCompleted = orchard_keys::derive_child_key(xsk_t_in, bip44CoinType, account, xsk_t_out);

    //Deserialize rust result on success
    if (rustCompleted) {
        rs << xsk_t_out;
        rs >> xsk;
    }

    //Cleanse the memory of the transfer and serialization objects
    memory_cleanse(ss.data(), ss.size());
    memory_cleanse(rs.data(), rs.size());
    memory_cleanse(xsk_t_in.data(), xsk_t_in.size());
    memory_cleanse(xsk_t_out.data(), xsk_t_out.size());

    //Return data
    if (rustCompleted) {
        return xsk;
    }

    return std::nullopt;
}

std::optional<OrchardExtendedFullViewingKeyPirate> OrchardExtendedSpendingKeyPirate::GetXFVK() const
{
    OrchardFullViewingKey fvk;
    if (sk.DeriveFVK(&fvk)) {
        OrchardExtendedFullViewingKeyPirate ret;
        ret.depth = depth;
        ret.parentFVKTag = parentFVKTag;
        ret.childIndex = childIndex;
        ret.chaincode = chaincode;
        ret.fvk = fvk;
        return ret;
    }
    return std::nullopt;
}

}
