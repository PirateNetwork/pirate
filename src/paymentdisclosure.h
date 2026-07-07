// Copyright (c) 2017 The Zcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ZCASH_PAYMENTDISCLOSURE_H
#define ZCASH_PAYMENTDISCLOSURE_H

#include "uint256.h"
#include "clientversion.h"
#include "serialize.h"
#include "streams.h"
#include "version.h"

#include <array>
#include <cstdint>
#include <string>
#include <optional>

/**
 * Structure representing a Sapling Output Disclosure with associated transaction information.
 * This is used for proof-of-payment functionality, allowing someone to prove they paid to a
 * specific Sapling output without revealing the full spending key.
 */
class SaplingOutputDisclosure
{
public:
    uint256 txid;                      // Transaction ID
    uint32_t outputIndex;              // Output index in the transaction
    uint256_t ock;      // The Outgoing Cipher Key (32 bytes)

    SaplingOutputDisclosure() : txid(), outputIndex(0), ock() {}

    SaplingOutputDisclosure(const uint256& txid_, uint32_t outputIndex_, const uint256_t& ock_)
        : txid(txid_), outputIndex(outputIndex_), ock(ock_) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(txid);
        READWRITE(outputIndex);
        // Serialize the OCK array
        for (size_t i = 0; i < 32; ++i) {
            READWRITE(ock[i]);
        }
    }

    bool operator==(const SaplingOutputDisclosure& other) const {
        return txid == other.txid && outputIndex == other.outputIndex && ock == other.ock;
    }

    bool operator!=(const SaplingOutputDisclosure& other) const {
        return !(*this == other);
    }
};

/**
 * Structure representing an Ironwood Output Disclosure with associated transaction information.
 * This is used for proof-of-payment functionality, allowing someone to prove they paid to a
 * specific Ironwood output without revealing the full spending key.
 */
class IronwoodOutputDisclosure
{
public:
    uint256 txid;                      // Transaction ID
    uint32_t outputIndex;              // Output index in the transaction (action index)
    uint256_t ock;      // The Outgoing Cipher Key (32 bytes)

    IronwoodOutputDisclosure() : txid(), outputIndex(0), ock() {}

    IronwoodOutputDisclosure(const uint256& txid_, uint32_t outputIndex_, const uint256_t& ock_)
        : txid(txid_), outputIndex(outputIndex_), ock(ock_) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(txid);
        READWRITE(outputIndex);
        // Serialize the OCK array
        for (size_t i = 0; i < 32; ++i) {
            READWRITE(ock[i]);
        }
    }

    bool operator==(const IronwoodOutputDisclosure& other) const {
        return txid == other.txid && outputIndex == other.outputIndex && ock == other.ock;
    }

    bool operator!=(const IronwoodOutputDisclosure& other) const {
        return !(*this == other);
    }
};

// Forward declaration
class CWallet;

/**
 * Generate a Sapling output disclosure key for a given transaction and output index.
 * This function searches the wallet for an OVK that can decrypt the output and creates
 * an encoded disclosure that can be shared for proof-of-payment.
 * 
 * @param wallet The wallet to search for OVKs
 * @param txid The transaction ID
 * @param outputIndex The index of the Sapling output to create a disclosure for
 * @return The encoded disclosure string, or empty string if no matching OVK found
 */
std::string GenerateSaplingDisclosure(CWallet* wallet, const uint256& txid, int outputIndex);

/**
 * Generate an Ironwood action disclosure key for a given transaction and action index.
 * This function searches the wallet for an OVK that can decrypt the action and creates
 * an encoded disclosure that can be shared for proof-of-payment.
 * 
 * @param wallet The wallet to search for OVKs
 * @param txid The transaction ID
 * @param actionIndex The index of the Ironwood action to create a disclosure for
 * @return The encoded disclosure string, or empty string if no matching OVK found
 */
std::string GenerateIronwoodDisclosure(CWallet* wallet, const uint256& txid, int actionIndex);

/**
 * Structure representing the result of verifying a Sapling output disclosure
 */
struct SaplingDisclosureVerificationResult {
    bool success;
    std::string error;
    uint256 txid;
    uint32_t outputIndex;
    uint64_t value;
    std::string address;
    std::string memoHex;
};

/**
 * Structure representing the result of verifying an Ironwood action disclosure
 */
struct IronwoodDisclosureVerificationResult {
    bool success;
    std::string error;
    uint256 txid;
    uint32_t actionIndex;
    uint64_t value;
    std::string address;
    std::string memoHex;
};

/**
 * Verify and decrypt a Sapling output disclosure.
 * 
 * @param disclosureStr The bech32-encoded Sapling disclosure key
 * @return A result structure containing the decrypted data or error information
 */
SaplingDisclosureVerificationResult VerifySaplingDisclosure(const std::string& disclosureStr);

/**
 * Verify and decrypt an Ironwood action disclosure.
 * 
 * @param disclosureStr The bech32-encoded Ironwood disclosure key
 * @return A result structure containing the decrypted data or error information
 */
IronwoodDisclosureVerificationResult VerifyIronwoodDisclosure(const std::string& disclosureStr);

/**
 * Unified structure for disclosure verification results that handles both Sapling and Ironwood
 */
struct UnifiedDisclosureVerificationResult {
    bool success;
    std::string error;
    std::string disclosureType;  // "Sapling" or "Ironwood"
    uint256 txid;
    uint32_t outputIndex;  // For both output index (Sapling) and action index (Ironwood)
    uint64_t value;
    std::string address;
    std::string memoHex;
};

/**
 * Verify and decrypt a payment disclosure of any type (Sapling or Ironwood).
 * Automatically detects the disclosure type by attempting to decode it.
 * 
 * @param disclosureStr The bech32-encoded disclosure key (Sapling or Ironwood)
 * @return A unified result structure containing the decrypted data or error information
 */
UnifiedDisclosureVerificationResult VerifyPaymentDisclosure(const std::string& disclosureStr);

#endif // ZCASH_PAYMENTDISCLOSURE_H
