// Copyright (c) 2017 The Zcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "paymentdisclosure.h"

#include "key_io.h"
#include "util.h"
#include "wallet/wallet.h"
#include "main.h"
#include "zcash/address/zip32.h"
#include <rust/bridge.h>
#include <set>

std::string GenerateSaplingDisclosure(CWallet* wallet, const uint256& txid, int outputIndex)
{
    // Get the transaction
    CTransaction tx;
    uint256 hashBlock;
    if (!GetTransaction(txid, tx, hashBlock, true)) {
        return "";
    }

    // Check if this is a Sapling transaction with outputs
    const auto& saplingBundle = tx.GetSaplingBundle();
    if (!saplingBundle.IsPresent()) {
        return "";
    }

    const auto& bundleDetails = saplingBundle.GetDetails();
    if (outputIndex < 0 || outputIndex >= (int)bundleDetails.num_outputs()) {
        return "";
    }

    // Get the specific output
    rust::Box<sapling::Output> output = bundleDetails.get_output(outputIndex);

    // Get the OVK from the wallet
    // We need to find which OVK can actually decrypt this output
    // Try all OVKs in the wallet (both Sapling and Orchard) and use the one that successfully decrypts
    uint256_t ovkBytes;
    uint256_t ock;
    bool found = false;

    // Get output components needed for OCK derivation and decryption
    auto cv = output->cv();
    auto cmu = output->cmu();
    auto epk = output->ephemeral_key();

    // Iterate through all Sapling addresses in the wallet
    std::set<libzcash::SaplingPaymentAddress> addresses;
    wallet->GetSaplingPaymentAddresses(addresses);

    for (const auto& addr : addresses) {
        // Get the spending key for this address if we have it
        libzcash::SaplingExtendedSpendingKey extsk;
        if (wallet->GetSaplingExtendedSpendingKey(addr, extsk)) {
            auto ovk = extsk.expsk.ovk;
            std::copy(ovk.begin(), ovk.end(), ovkBytes.begin());
            
            // Derive the OCK for this OVK
            if (!sapling::derive_sapling_ock(ovkBytes, cv, cmu, epk, ock)) {
                continue; // Try next OVK
            }
            
            // Test if this OCK can decrypt the output
            uint64_t test_value;
            std::array<uint8_t, 11> test_diversifier;
            uint256_t test_pk_d;
            std::array<uint8_t, 512> test_memo;
            uint256_t test_rseed;
            uint8_t test_leadbyte;
            uint256_t test_cmu_out;
            uint256_t test_rcm;
            
            if (output->try_decrypt_output_ock(
                    ock, test_value, test_diversifier, test_pk_d, test_memo,
                    test_rseed, test_leadbyte, test_cmu_out, test_rcm)) {
                // Successfully decrypted, this is the right OVK
                found = true;
                break;
            }
        }
    }

    // If not found with Sapling OVKs, try Orchard OVKs
    // Transactions that spend from Orchard and send to Sapling use Orchard OVKs
    if (!found) {
        std::set<libzcash::OrchardPaymentAddress> orchardAddresses;
        wallet->GetOrchardPaymentAddresses(orchardAddresses);

        for (const auto& addr : orchardAddresses) {
            // Get the spending key for this address if we have it
            libzcash::OrchardExtendedSpendingKeyPirate extsk;
            if (wallet->GetOrchardExtendedSpendingKey(addr, extsk)) {
                // Get the FVK and then the OVK
                libzcash::OrchardFullViewingKey fvk;
                if (!extsk.sk.DeriveFVK(&fvk)) {
                    continue; // Try next key
                }
                
                libzcash::OrchardOutgoingViewingKey ovkObj;
                if (!fvk.DeriveOVK(&ovkObj)) {
                    continue; // Try next key
                }
                
                auto ovk = ovkObj.ovk;
                std::copy(ovk.begin(), ovk.end(), ovkBytes.begin());
                
                // Derive the OCK for this OVK
                if (!sapling::derive_sapling_ock(ovkBytes, cv, cmu, epk, ock)) {
                    continue; // Try next OVK
                }
                
                // Test if this OCK can decrypt the output
                uint64_t test_value;
                std::array<uint8_t, 11> test_diversifier;
                uint256_t test_pk_d;
                std::array<uint8_t, 512> test_memo;
                uint256_t test_rseed;
                uint8_t test_leadbyte;
                uint256_t test_cmu_out;
                uint256_t test_rcm;
                
                if (output->try_decrypt_output_ock(
                        ock, test_value, test_diversifier, test_pk_d, test_memo,
                        test_rseed, test_leadbyte, test_cmu_out, test_rcm)) {
                    // Successfully decrypted, this is the right OVK
                    found = true;
                    break;
                }
            }
        }
    }

    // If not found with Sapling or Orchard OVKs, try transparent OVK
    // Transactions that spend from transparent addresses use a derived transparent OVK
    if (!found) {
        HDSeed seed;
        if (wallet->GetHDSeed(seed)) {
            auto ovk = ovkForShieldingFromTaddr(seed);
            std::copy(ovk.begin(), ovk.end(), ovkBytes.begin());
            
            // Derive the OCK for this OVK
            if (sapling::derive_sapling_ock(ovkBytes, cv, cmu, epk, ock)) {
                // Test if this OCK can decrypt the output
                uint64_t test_value;
                std::array<uint8_t, 11> test_diversifier;
                uint256_t test_pk_d;
                std::array<uint8_t, 512> test_memo;
                uint256_t test_rseed;
                uint8_t test_leadbyte;
                uint256_t test_cmu_out;
                uint256_t test_rcm;
                
                if (output->try_decrypt_output_ock(
                        ock, test_value, test_diversifier, test_pk_d, test_memo,
                        test_rseed, test_leadbyte, test_cmu_out, test_rcm)) {
                    // Successfully decrypted, this is the right OVK
                    found = true;
                }
            }
        }
    }

    if (!found) {
        return "";
    }

    // Create the disclosure structure and encode it
    SaplingOutputDisclosure disclosureStruct(txid, static_cast<uint32_t>(outputIndex), ock);
    return EncodeSaplingOutputDisclosure(disclosureStruct);
}

std::string GenerateOrchardDisclosure(CWallet* wallet, const uint256& txid, int actionIndex)
{
    // Get the transaction
    CTransaction tx;
    uint256 hashBlock;
    if (!GetTransaction(txid, tx, hashBlock, true)) {
        return "";
    }

    // Check if this is an Orchard transaction with actions
    const auto& orchardBundle = tx.GetOrchardBundle();
    if (!orchardBundle.IsPresent()) {
        return "";
    }

    const auto& bundleDetails = orchardBundle.GetDetails();
    if (actionIndex < 0 || actionIndex >= (int)bundleDetails.num_actions()) {
        return "";
    }

    // Get all actions from the bundle
    auto actions = bundleDetails.actions();
    const auto& action = actions[actionIndex];

    // Get the OVK from the wallet
    // We need to find which OVK can actually decrypt this action
    // Try all OVKs in the wallet and use the one that successfully decrypts
    uint256_t ovkBytes;
    uint256_t ock;
    bool found = false;

    // Iterate through all Orchard addresses in the wallet
    std::set<libzcash::OrchardPaymentAddress> addresses;
    wallet->GetOrchardPaymentAddresses(addresses);

    for (const auto& addr : addresses) {
        // Get the spending key for this address if we have it
        libzcash::OrchardExtendedSpendingKeyPirate extsk;
        if (wallet->GetOrchardExtendedSpendingKey(addr, extsk)) {
            // Get the FVK and then the OVK
            libzcash::OrchardFullViewingKey fvk;
            if (!extsk.sk.DeriveFVK(&fvk)) {
                continue; // Try next key
            }
            
            libzcash::OrchardOutgoingViewingKey ovkObj;
            if (!fvk.DeriveOVK(&ovkObj)) {
                continue; // Try next key
            }
            
            auto ovk = ovkObj.ovk;
            std::copy(ovk.begin(), ovk.end(), ovkBytes.begin());
            
            // Derive the OCK for this OVK
            if (!orchard::derive_orchard_ock(&action, &ovkBytes, &ock)) {
                continue; // Try next OVK
            }
            
            // Test if this OCK can decrypt the action
            uint64_t test_value;
            std::array<uint8_t, 43> test_address;
            std::array<uint8_t, 512> test_memo;
            uint256_t test_rho;
            uint256_t test_rseed;
            
            if (orchard::try_orchard_decrypt_action_ock(
                    &action, &ock, &test_value, &test_address, &test_memo,
                    &test_rho, &test_rseed)) {
                // Successfully decrypted, this is the right OVK
                found = true;
                break;
            }
        }
    }

    // If not found with Orchard OVKs, try Sapling OVKs
    // Transactions that spend from Sapling and send to Orchard use Sapling OVKs
    if (!found) {
        std::set<libzcash::SaplingPaymentAddress> saplingAddresses;
        wallet->GetSaplingPaymentAddresses(saplingAddresses);

        for (const auto& addr : saplingAddresses) {
            // Get the spending key for this address if we have it
            libzcash::SaplingExtendedSpendingKey extsk;
            if (wallet->GetSaplingExtendedSpendingKey(addr, extsk)) {
                auto ovk = extsk.expsk.ovk;
                std::copy(ovk.begin(), ovk.end(), ovkBytes.begin());
                
                // Derive the OCK for this OVK
                if (!orchard::derive_orchard_ock(&action, &ovkBytes, &ock)) {
                    continue; // Try next OVK
                }
                
                // Test if this OCK can decrypt the action
                uint64_t test_value;
                std::array<uint8_t, 43> test_address;
                std::array<uint8_t, 512> test_memo;
                uint256_t test_rho;
                uint256_t test_rseed;
                
                if (orchard::try_orchard_decrypt_action_ock(
                        &action, &ock, &test_value, &test_address, &test_memo,
                        &test_rho, &test_rseed)) {
                    // Successfully decrypted, this is the right OVK
                    found = true;
                    break;
                }
            }
        }
    }

    // If not found with Orchard or Sapling OVKs, try transparent OVK
    // Transactions that spend from transparent addresses use a derived transparent OVK
    if (!found) {
        HDSeed seed;
        if (wallet->GetHDSeed(seed)) {
            auto ovk = ovkForShieldingFromTaddr(seed);
            std::copy(ovk.begin(), ovk.end(), ovkBytes.begin());
            
            // Derive the OCK for this OVK
            if (orchard::derive_orchard_ock(&action, &ovkBytes, &ock)) {
                // Test if this OCK can decrypt the action
                uint64_t test_value;
                std::array<uint8_t, 43> test_address;
                std::array<uint8_t, 512> test_memo;
                uint256_t test_rho;
                uint256_t test_rseed;
                
                if (orchard::try_orchard_decrypt_action_ock(
                        &action, &ock, &test_value, &test_address, &test_memo,
                        &test_rho, &test_rseed)) {
                    // Successfully decrypted, this is the right OVK
                    found = true;
                }
            }
        }
    }

    if (!found) {
        return "";
    }

    // Create the disclosure structure and encode it
    OrchardOutputDisclosure disclosureStruct(txid, static_cast<uint32_t>(actionIndex), ock);
    return EncodeOrchardOutputDisclosure(disclosureStruct);
}

/**
 * Verify and decrypt a Sapling output disclosure
 */
SaplingDisclosureVerificationResult VerifySaplingDisclosure(const std::string& disclosureStr)
{
    SaplingDisclosureVerificationResult result;
    result.success = false;

    // Parse the bech32-encoded disclosure
    auto disclosureOptional = DecodeSaplingOutputDisclosure(disclosureStr);
    
    if (!disclosureOptional) {
        result.error = "Invalid disclosure encoding";
        return result;
    }
    
    SaplingOutputDisclosure disclosureStruct = *disclosureOptional;

    // Get the transaction
    CTransaction tx;
    uint256 hashBlock;
    if (!GetTransaction(disclosureStruct.txid, tx, hashBlock, true)) {
        result.error = "Transaction not found";
        return result;
    }

    // Check if this is a Sapling transaction with outputs
    const auto& saplingBundle = tx.GetSaplingBundle();
    if (!saplingBundle.IsPresent()) {
        result.error = "Transaction has no Sapling outputs";
        return result;
    }

    const auto& bundleDetails = saplingBundle.GetDetails();
    if (bundleDetails.num_outputs() == 0) {
        result.error = "Transaction has no Sapling outputs";
        return result;
    }

    if (disclosureStruct.outputIndex >= bundleDetails.num_outputs()) {
        result.error = strprintf("Output index %d out of range", disclosureStruct.outputIndex);
        return result;
    }

    // Get the specific output
    rust::Box<sapling::Output> output = bundleDetails.get_output(disclosureStruct.outputIndex);

    // Decrypt with disclosure key
    uint64_t value;
    std::array<uint8_t, 11> diversifier;
    uint256_t pk_d;
    std::array<uint8_t, 512> memo;
    uint256_t rseed;
    uint8_t leadbyte;
    uint256_t cmu_out;
    uint256_t rcm;

    if (!output->try_decrypt_output_ock(
            disclosureStruct.ock, value, diversifier, pk_d, memo,
            rseed, leadbyte, cmu_out, rcm)) {
        result.error = "Failed to decrypt output with provided disclosure key";
        return result;
    }

    // Construct the payment address from diversifier and pk_d
    libzcash::diversifier_t div;
    std::copy(diversifier.begin(), diversifier.end(), div.begin());
    uint256 pkd = uint256::FromRawBytes(pk_d);
    libzcash::SaplingPaymentAddress paymentAddr(div, pkd);

    // Build successful result
    result.success = true;
    result.txid = disclosureStruct.txid;
    result.outputIndex = disclosureStruct.outputIndex;
    result.value = value;
    result.address = EncodePaymentAddress(paymentAddr);
    result.memoHex = HexStr(memo.begin(), memo.end());

    return result;
}

/**
 * Verify and decrypt an Orchard action disclosure
 */
OrchardDisclosureVerificationResult VerifyOrchardDisclosure(const std::string& disclosureStr)
{
    OrchardDisclosureVerificationResult result;
    result.success = false;

    // Parse the bech32-encoded disclosure
    auto disclosureOptional = DecodeOrchardOutputDisclosure(disclosureStr);
    
    if (!disclosureOptional) {
        result.error = "Invalid disclosure encoding";
        return result;
    }
    
    OrchardOutputDisclosure disclosureStruct = *disclosureOptional;

    // Get the transaction
    CTransaction tx;
    uint256 hashBlock;
    if (!GetTransaction(disclosureStruct.txid, tx, hashBlock, true)) {
        result.error = "Transaction not found";
        return result;
    }

    // Check if this is an Orchard transaction with actions
    const auto& orchardBundle = tx.GetOrchardBundle();
    if (!orchardBundle.IsPresent()) {
        result.error = "Transaction has no Orchard actions";
        return result;
    }

    const auto& bundleDetails = orchardBundle.GetDetails();
    if (bundleDetails.num_actions() == 0) {
        result.error = "Transaction has no Orchard actions";
        return result;
    }

    if (disclosureStruct.outputIndex >= bundleDetails.num_actions()) {
        result.error = strprintf("Action index %d out of range", disclosureStruct.outputIndex);
        return result;
    }

    // Get all actions from the bundle
    auto actions = bundleDetails.actions();
    const auto& action = actions[disclosureStruct.outputIndex];

    // Decrypt with disclosure key
    uint64_t value;
    std::array<uint8_t, 43> address;
    std::array<uint8_t, 512> memo;
    uint256_t rho;
    uint256_t rseed;

    if (!orchard::try_orchard_decrypt_action_ock(
            &action, &disclosureStruct.ock, &value, &address, &memo,
            &rho, &rseed)) {
        result.error = "Failed to decrypt action with provided disclosure key";
        return result;
    }

    // Construct the payment address from the raw address bytes (11-byte diversifier + 32-byte pk_d)
    libzcash::diversifier_t div;
    std::copy(address.begin(), address.begin() + 11, div.begin());
    uint256 pkd;
    std::copy(address.begin() + 11, address.end(), pkd.begin());
    libzcash::OrchardPaymentAddress paymentAddr(div, pkd);

    // Build successful result
    result.success = true;
    result.txid = disclosureStruct.txid;
    result.actionIndex = disclosureStruct.outputIndex;
    result.value = value;
    result.address = EncodePaymentAddress(paymentAddr);
    result.memoHex = HexStr(memo.begin(), memo.end());

    return result;
}

/**
 * Unified verification function that detects and verifies either Sapling or Orchard disclosures
 */
UnifiedDisclosureVerificationResult VerifyPaymentDisclosure(const std::string& disclosureStr)
{
    UnifiedDisclosureVerificationResult result;
    result.success = false;

    // Try to decode as Sapling disclosure first
    auto saplingOptional = DecodeSaplingOutputDisclosure(disclosureStr);
    if (saplingOptional) {
        // Verify as Sapling
        SaplingDisclosureVerificationResult saplingResult = VerifySaplingDisclosure(disclosureStr);
        
        result.success = saplingResult.success;
        result.error = saplingResult.error;
        result.disclosureType = "Sapling";
        result.txid = saplingResult.txid;
        result.outputIndex = saplingResult.outputIndex;
        result.value = saplingResult.value;
        result.address = saplingResult.address;
        result.memoHex = saplingResult.memoHex;
        
        return result;
    }

    // Try to decode as Orchard disclosure
    auto orchardOptional = DecodeOrchardOutputDisclosure(disclosureStr);
    if (orchardOptional) {
        // Verify as Orchard
        OrchardDisclosureVerificationResult orchardResult = VerifyOrchardDisclosure(disclosureStr);
        
        result.success = orchardResult.success;
        result.error = orchardResult.error;
        result.disclosureType = "Orchard";
        result.txid = orchardResult.txid;
        result.outputIndex = orchardResult.actionIndex;
        result.value = orchardResult.value;
        result.address = orchardResult.address;
        result.memoHex = orchardResult.memoHex;
        
        return result;
    }

    // Unable to decode as either type
    result.error = "Invalid disclosure encoding - unable to decode as Sapling or Orchard disclosure";
    return result;
}
