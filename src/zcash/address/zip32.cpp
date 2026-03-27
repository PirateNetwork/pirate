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
