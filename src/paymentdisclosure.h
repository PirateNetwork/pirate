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
    std::array<uint8_t, 32> ock;      // The Outgoing Cipher Key (32 bytes)

    SaplingOutputDisclosure() : txid(), outputIndex(0), ock() {}

    SaplingOutputDisclosure(const uint256& txid_, uint32_t outputIndex_, const std::array<uint8_t, 32>& ock_)
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
 * Structure representing an Orchard Output Disclosure with associated transaction information.
 * This is used for proof-of-payment functionality, allowing someone to prove they paid to a
 * specific Orchard output without revealing the full spending key.
 */
class OrchardOutputDisclosure
{
public:
    uint256 txid;                      // Transaction ID
    uint32_t outputIndex;              // Output index in the transaction (action index)
    std::array<uint8_t, 32> ock;      // The Outgoing Cipher Key (32 bytes)

    OrchardOutputDisclosure() : txid(), outputIndex(0), ock() {}

    OrchardOutputDisclosure(const uint256& txid_, uint32_t outputIndex_, const std::array<uint8_t, 32>& ock_)
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

    bool operator==(const OrchardOutputDisclosure& other) const {
        return txid == other.txid && outputIndex == other.outputIndex && ock == other.ock;
    }

    bool operator!=(const OrchardOutputDisclosure& other) const {
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
 * Generate an Orchard action disclosure key for a given transaction and action index.
 * This function searches the wallet for an OVK that can decrypt the action and creates
 * an encoded disclosure that can be shared for proof-of-payment.
 * 
 * @param wallet The wallet to search for OVKs
 * @param txid The transaction ID
 * @param actionIndex The index of the Orchard action to create a disclosure for
 * @return The encoded disclosure string, or empty string if no matching OVK found
 */
std::string GenerateOrchardDisclosure(CWallet* wallet, const uint256& txid, int actionIndex);

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
 * Structure representing the result of verifying an Orchard action disclosure
 */
struct OrchardDisclosureVerificationResult {
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
 * Verify and decrypt an Orchard action disclosure.
 * 
 * @param disclosureStr The bech32-encoded Orchard disclosure key
 * @return A result structure containing the decrypted data or error information
 */
OrchardDisclosureVerificationResult VerifyOrchardDisclosure(const std::string& disclosureStr);

/**
 * Unified structure for disclosure verification results that handles both Sapling and Orchard
 */
struct UnifiedDisclosureVerificationResult {
    bool success;
    std::string error;
    std::string disclosureType;  // "Sapling" or "Orchard"
    uint256 txid;
    uint32_t outputIndex;  // For both output index (Sapling) and action index (Orchard)
    uint64_t value;
    std::string address;
    std::string memoHex;
};

/**
 * Verify and decrypt a payment disclosure of any type (Sapling or Orchard).
 * Automatically detects the disclosure type by attempting to decode it.
 * 
 * @param disclosureStr The bech32-encoded disclosure key (Sapling or Orchard)
 * @return A unified result structure containing the decrypted data or error information
 */
UnifiedDisclosureVerificationResult VerifyPaymentDisclosure(const std::string& disclosureStr);

#endif // ZCASH_PAYMENTDISCLOSURE_H
