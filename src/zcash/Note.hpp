#ifndef ZC_NOTE_H_
#define ZC_NOTE_H_

#include "uint256.h"
#include "Zcash.h"
#include "Address.hpp"
#include "NoteEncryption.hpp"
#include "consensus/params.h"
#include "consensus/consensus.h"
#include "primitives/orchard.h"

#include "rust/orchard/orchard_actions.h"

#include <array>
#include <optional>

namespace libzcash {

class BaseNote {
protected:
    uint64_t value_ = 0;
public:
    BaseNote() {}
    BaseNote(uint64_t value) : value_(value) {};
    virtual ~BaseNote() {};

    inline uint64_t value() const { return value_; };
};

class SproutNote : public BaseNote {
public:
    uint256 a_pk;
    uint256 rho;
    uint256 r;

    SproutNote(uint256 a_pk, uint64_t value, uint256 rho, uint256 r)
        : BaseNote(value), a_pk(a_pk), rho(rho), r(r) {}

    SproutNote();

    virtual ~SproutNote() {};

    uint256 cm() const;

    uint256 nullifier(const SproutSpendingKey& a_sk) const;
};

inline bool plaintext_version_is_valid(const Consensus::Params& params, int height, unsigned char leadbyte) {
    int orchardActivationHeight = params.vUpgrades[Consensus::UPGRADE_ORCHARD].nActivationHeight;

    if (height < orchardActivationHeight && leadbyte != 0x01) {
        // non-0x01 received before Orchard activation height
        return false;
    }
    if (height >= orchardActivationHeight
        && height < orchardActivationHeight + ZIP212_GRACE_PERIOD
        && leadbyte != 0x01
        && leadbyte != 0x02)
    {
        // non-{0x01,0x02} received after Orchard activation and before grace period has elapsed
        return false;
    }
    if (orchardActivationHeight > 0 && height >= orchardActivationHeight + ZIP212_GRACE_PERIOD && leadbyte != 0x02) {
        // non-0x02 received past (Orchard activation height + grace period)
        return false;
    }
    return true;
};

enum class Zip212Enabled {
    BeforeZip212,
    AfterZip212
};
class SaplingNote : public BaseNote {
private:
    uint256 rseed;
    friend class SaplingNotePlaintext;
    Zip212Enabled zip_212_enabled;
public:
    diversifier_t d;
    uint256 pk_d;

    SaplingNote(diversifier_t d, uint256 pk_d, uint64_t value, uint256 rseed, Zip212Enabled zip_212_enabled)
            : BaseNote(value), d(d), pk_d(pk_d), rseed(rseed), zip_212_enabled(zip_212_enabled) {}

    SaplingNote(const SaplingPaymentAddress &address, uint64_t value, Zip212Enabled zip_212_enabled);

    virtual ~SaplingNote() {};

    std::optional<uint256> cmu() const;
    std::optional<uint256> nullifier(const SaplingFullViewingKey &vk, const uint64_t position) const;
    uint256 rcm() const;

    Zip212Enabled get_zip_212_enabled() const {
        return zip_212_enabled;
    }
};

class OrchardNote : public BaseNote {
private:
    uint256 rho_;
    uint256 rseed_;
    uint256 cmx_;
public:
    OrchardPaymentAddressPirate address;

    OrchardNote(OrchardPaymentAddressPirate address, uint64_t value, uint256 rho, uint256 rseed, uint256 cmx)
            : BaseNote(value), address(address), rho_(rho), rseed_(rseed), cmx_(cmx) {}

    virtual ~OrchardNote() {};

    uint256 rho() {
      return rho_;
    }

    uint256 rseed() {
      return rseed_;
    }

    uint256 cmx() {
      return cmx_;
    }

    std::optional<uint256> GetNullifier(const libzcash::OrchardFullViewingKeyPirate fvk)
    {

        // Datastreams for serialization
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); // sending stream
        CDataStream as(SER_NETWORK, PROTOCOL_VERSION); // sending stream
        CDataStream rs(SER_NETWORK, PROTOCOL_VERSION); // returning stream

        // Tranfer Data
        libzcash::OrchardFullViewingKey_t fvk_t;
        libzcash::OrchardPaymentAddress_t address_t;
        uint256 nullifier_t;

        // Serialize sending data
        ss << fvk;
        ss >> fvk_t;

        as << address;
        as >> address_t;

        if (!get_nullifer_from_parts(
              fvk_t.begin(),
              address_t.begin(),
              value_,
              rho_.begin(),
              rseed_.begin(),
              nullifier_t.begin())) {
                    return std::nullopt;
        }

        return nullifier_t;

    }

};


class BaseNotePlaintext {
protected:
    uint64_t value_ = 0;
    std::array<unsigned char, ZC_MEMO_SIZE> memo_;
public:
    BaseNotePlaintext() {}
    BaseNotePlaintext(const BaseNote& note, std::array<unsigned char, ZC_MEMO_SIZE> memo)
        : value_(note.value()), memo_(memo) {}
    BaseNotePlaintext(const uint64_t value, std::array<unsigned char, ZC_MEMO_SIZE> memo)
        : value_(value), memo_(memo) {}
    virtual ~BaseNotePlaintext() {}

    inline uint64_t value() const { return value_; }
    inline const std::array<unsigned char, ZC_MEMO_SIZE> & memo() const { return memo_; }
};

class SproutNotePlaintext : public BaseNotePlaintext {
public:
    uint256 rho;
    uint256 r;

    SproutNotePlaintext() {}

    SproutNotePlaintext(const SproutNote& note, std::array<unsigned char, ZC_MEMO_SIZE> memo);

    SproutNote note(const SproutPaymentAddress& addr) const;

    virtual ~SproutNotePlaintext() {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        unsigned char leadbyte = 0x00;
        READWRITE(leadbyte);

        if (leadbyte != 0x00) {
            throw std::ios_base::failure("lead byte of SproutNotePlaintext is not recognized");
        }

        READWRITE(value_);
        READWRITE(rho);
        READWRITE(r);
        READWRITE(memo_);
    }

    static SproutNotePlaintext decrypt(const ZCNoteDecryption& decryptor,
                                 const ZCNoteDecryption::Ciphertext& ciphertext,
                                 const uint256& ephemeralKey,
                                 const uint256& h_sig,
                                 unsigned char nonce
                                );

    ZCNoteEncryption::Ciphertext encrypt(ZCNoteEncryption& encryptor,
                                         const uint256& pk_enc
                                        ) const;
};

typedef std::pair<SaplingEncCiphertext, SaplingNoteEncryption> SaplingNotePlaintextEncryptionResult;

class SaplingNotePlaintext : public BaseNotePlaintext {
private:
    uint256 rseed;
    unsigned char leadbyte;
public:
    diversifier_t d;

    SaplingNotePlaintext() {}

    SaplingNotePlaintext(const SaplingNote& note, std::array<unsigned char, ZC_MEMO_SIZE> memo);

    static std::optional<SaplingNotePlaintext> decrypt(
        const Consensus::Params& params,
        int height,
        const SaplingEncCiphertext &ciphertext,
        const uint256 &ivk,
        const uint256 &epk,
        const uint256 &cmu
    );

    static std::optional<SaplingNotePlaintext> plaintext_checks_without_height(
        const SaplingNotePlaintext &plaintext,
        const uint256 &ivk,
        const uint256 &epk,
        const uint256 &cmu
    );

    static std::optional<SaplingNotePlaintext> attempt_sapling_enc_decryption_deserialization(
        const SaplingEncCiphertext &ciphertext,
        const uint256 &ivk,
        const uint256 &epk
    );

    static std::optional<SaplingNotePlaintext> decrypt(
        const Consensus::Params& params,
        int height,
        const SaplingEncCiphertext &ciphertext,
        const uint256 &epk,
        const uint256 &esk,
        const uint256 &pk_d,
        const uint256 &cmu
    );

    static std::optional<SaplingNotePlaintext> plaintext_checks_without_height(
        const SaplingNotePlaintext &plaintext,
        const uint256 &epk,
        const uint256 &esk,
        const uint256 &pk_d,
        const uint256 &cmu
    );

    static std::optional<SaplingNotePlaintext> attempt_sapling_enc_decryption_deserialization(
        const SaplingEncCiphertext &ciphertext,
        const uint256 &epk,
        const uint256 &esk,
        const uint256 &pk_d
    );

    std::optional<SaplingNote> note(const SaplingIncomingViewingKey& ivk) const;

    virtual ~SaplingNotePlaintext() {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(leadbyte);

        if (leadbyte != 0x01 && leadbyte != 0x02) {
            throw std::ios_base::failure("lead byte of SaplingNotePlaintext is not recognized");
        }

        READWRITE(d);           // 11 bytes
        READWRITE(value_);      // 8 bytes
        READWRITE(rseed);       // 32 bytes
        READWRITE(memo_);       // 512 bytes
    }

    std::optional<SaplingNotePlaintextEncryptionResult> encrypt(const uint256& pk_d) const;

    uint256 rcm() const;
    uint256 generate_or_derive_esk() const;
    unsigned char get_leadbyte() const {
        return leadbyte;
    }
};

class OrchardNotePlaintext : public BaseNotePlaintext {
private:
    libzcash::OrchardPaymentAddressPirate address;
    uint256 rho;
    uint256 rseed;
    std::optional<uint256> nullifier;
    uint256 cmx;
public:

    OrchardNotePlaintext() {}

    OrchardNotePlaintext(
      const CAmount value,
      const libzcash::OrchardPaymentAddressPirate address,
      const std::array<unsigned char, ZC_MEMO_SIZE> memo,
      const uint256 rho,
      const uint256 rseed,
      const std::optional<uint256> nullifier,
      const uint256 cmx)
    : BaseNotePlaintext(value, memo), address(address), rho(rho), rseed(rseed), nullifier(nullifier), cmx(cmx) {}

    virtual ~OrchardNotePlaintext() {}

    libzcash::OrchardPaymentAddressPirate GetAddress() {
        return address;
    };

    // CAmount GetValue() {
    //   return value_;
    // };
    //
    // uint256 GetRseed() {
    //   return rseed;
    // };
    //
    // uint256 GetRho() {
    //   return rho;
    // };
    //
    // std::optional<uint256> GetNullifier() {
    //   return nullifier;
    // };

    // std::array<unsigned char, ZC_MEMO_SIZE> GetMemo() {
    //   return memo_;
    // };

    static std::optional<OrchardNotePlaintext> AttemptDecryptOrchardAction(
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

        OrchardNotePlaintext ret = OrchardNotePlaintext(value_t, address_r, memo_t, rho_t, rseed_t, std::nullopt, uint256::FromRawBytes(action->cmx()));

        return ret;
    };

    static std::optional<OrchardNotePlaintext> AttemptDecryptOrchardAction(
        const orchard_bundle::Action* action,
        const libzcash::OrchardOutgoingViewingKey ovk
    )
    {

        // Datastreams for serialization
        // CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); // sending stream
        CDataStream rs(SER_NETWORK, PROTOCOL_VERSION); // returning stream

        // Tranfer Data
        // libzcash::OrchardOutgoingViewingKey_t ovk_t;
        libzcash::OrchardPaymentAddress_t address_t;
        std::array<unsigned char, ZC_MEMO_SIZE> memo_t;
        uint64_t value_t;
        uint256 rho_t;
        uint256 rseed_t;

        // Serialize sending data
        // ss << ovk;
        // ss >> ovk_t;

        if(!try_orchard_decrypt_action_ovk(
            action->as_ptr(),
            ovk.begin(),
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

        OrchardNotePlaintext ret = OrchardNotePlaintext(value_t, address_r, memo_t, rho_t, rseed_t, std::nullopt, uint256::FromRawBytes(action->cmx()));

        return ret;
    };

    std::optional<OrchardNote> note() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(value_);      // 8 bytes
        READWRITE(address);     // 43 bytes
        READWRITE(memo_);       // 512 bytes
        READWRITE(rho);         // 32 bytes
        READWRITE(rseed);       // 32 bytes
        READWRITE(nullifier);   // 32 bytes
    }

};

class SaplingOutgoingPlaintext
{
public:
    uint256 pk_d;
    uint256 esk;

    SaplingOutgoingPlaintext() {};

    SaplingOutgoingPlaintext(uint256 pk_d, uint256 esk) : pk_d(pk_d), esk(esk) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(pk_d);        // 8 bytes
        READWRITE(esk);         // 8 bytes
    }

    static std::optional<SaplingOutgoingPlaintext> decrypt(
        const SaplingOutCiphertext &ciphertext,
        const uint256& ovk,
        const uint256& cv,
        const uint256& cm,
        const uint256& epk
    );

    SaplingOutCiphertext encrypt(
        const uint256& ovk,
        const uint256& cv,
        const uint256& cm,
        SaplingNoteEncryption& enc
    ) const;
};


}

#endif // ZC_NOTE_H_
