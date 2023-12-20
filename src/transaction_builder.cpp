// Copyright (c) 2018 The Zcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "transaction_builder.h"

#include "main.h"
#include "pubkey.h"
#include "rpc/protocol.h"
#include "script/sign.h"
#include "utilmoneystr.h"
#include "zcash/Note.hpp"
#include "key_io.h"
#include "core_io.h" //for EncodeHexTx

#include <boost/variant.hpp>
#include <librustzcash.h>


SpendDescriptionInfo::SpendDescriptionInfo(
    libzcash::SaplingExpandedSpendingKey expsk,
    libzcash::SaplingNote note,
    uint256 anchor,
    libzcash::MerklePath saplingMerklePath) : expsk(expsk), note(note), anchor(anchor), saplingMerklePath(saplingMerklePath)
{
    librustzcash_sapling_generate_r(alpha.begin());
}

boost::optional<OutputDescription> OutputDescriptionInfo::Build(void* ctx) {
    auto cmu = this->note.cmu();
    if (!cmu) {
        return boost::none;
    }

    libzcash::SaplingNotePlaintext notePlaintext(this->note, this->memo);

    auto res = notePlaintext.encrypt(this->note.pk_d);
    if (!res) {
        return boost::none;
    }
    auto enc = res.get();
    auto encryptor = enc.second;

    libzcash::SaplingPaymentAddress address(this->note.d, this->note.pk_d);
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << address;
    std::vector<unsigned char> addressBytes(ss.begin(), ss.end());

    OutputDescription odesc;
    uint256 rcm = this->note.rcm();
    if (!librustzcash_sapling_output_proof(
            ctx,
            encryptor.get_esk().begin(),
            addressBytes.data(),
            rcm.begin(),
            this->note.value(),
            odesc.cv.begin(),
            odesc.zkproof.begin())) {
        return boost::none;
    }

    odesc.cmu = *cmu;
    odesc.ephemeralKey = encryptor.get_epk();
    odesc.encCiphertext = enc.first;

    libzcash::SaplingOutgoingPlaintext outPlaintext(this->note.pk_d, encryptor.get_esk());
    odesc.outCiphertext = outPlaintext.encrypt(
        this->ovk,
        odesc.cv,
        odesc.cmu,
        encryptor);

    return odesc;
}

TransactionBuilderResult::TransactionBuilderResult(const CTransaction& tx) : maybeTx(tx) {}

TransactionBuilderResult::TransactionBuilderResult(const std::string& error) : maybeError(error) {}

bool TransactionBuilderResult::IsTx() { return maybeTx != boost::none; }

bool TransactionBuilderResult::IsError() { return maybeError != boost::none; }

CTransaction TransactionBuilderResult::GetTxOrThrow() {
    if (maybeTx) {
        return maybeTx.get();
    } else {
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to build transaction: " + GetError());
    }
}

std::string TransactionBuilderResult::GetError() {
    if (maybeError) {
        return maybeError.get();
    } else {
        // This can only happen if isTx() is true in which case we should not call getError()
        throw std::runtime_error("getError() was called in TransactionBuilderResult, but the result was not initialized as an error.");
    }
}

TransactionBuilder::TransactionBuilder(
    const Consensus::Params& consensusParams,
    int nHeight,
    CKeyStore* keystore) : consensusParams(consensusParams), nHeight(nHeight), keystore(keystore)
{
    mtx = CreateNewContextualCMutableTransaction(consensusParams, nHeight);

    consensusBranchId = CurrentEpochBranchId(nHeight, consensusParams);

    boost::optional<CTransaction> maybe_tx = CTransaction(mtx);
    auto tx_result = maybe_tx.get();
    auto signedtxn = EncodeHexTx(tx_result);
    //printf("TransactionBuilder::TransactionBuilder(online) mtx= %s\n",signedtxn.c_str());
}

TransactionBuilder::TransactionBuilder(
    bool fOverwintered,
    uint32_t nExpiryHeight,
    uint32_t nVersionGroupId,
    int32_t nVersion,
    int nBlockHeight,
    uint32_t branchId,
    uint8_t  cZip212Enabled)
{
    mtx.fOverwintered   = fOverwintered;
    mtx.nExpiryHeight   = nExpiryHeight;
    mtx.nVersionGroupId = nVersionGroupId;
    mtx.nVersion        = nVersion;
    nHeight             = nBlockHeight;
    consensusBranchId   = branchId;
    cZip212_enabled     = cZip212Enabled;

}

bool TransactionBuilder::AddSaplingSpend(
    libzcash::SaplingExpandedSpendingKey expsk,
    libzcash::SaplingNote note,
    uint256 anchor,
    libzcash::MerklePath saplingMerklePath)
{
    // Sanity check: cannot add Sapling spend to pre-Sapling transaction
    if (mtx.nVersion < SAPLING_TX_VERSION) {
        throw std::runtime_error("TransactionBuilder cannot add Sapling spend to pre-Sapling transaction");
    }

    // Consistency check: all anchors must equal the first one
    if (!spends.empty()) {
        if (spends[0].anchor != anchor) {
            return false;
        }
    }

    spends.emplace_back(expsk, note, anchor, saplingMerklePath);

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << saplingMerklePath;
    std::vector<unsigned char> local_witness(ss.begin(), ss.end());

    myCharArray_s sWitness;
    memcpy (&sWitness.cArray[0], reinterpret_cast<unsigned char*>(local_witness.data()), sizeof(sWitness.cArray) );
    asWitness.emplace_back(sWitness);

    alWitnessPosition.emplace_back(saplingMerklePath.position());

    mtx.valueBalance += note.value();

    boost::optional<CTransaction> maybe_tx = CTransaction(mtx);
    auto tx_result = maybe_tx.get();
    auto signedtxn = EncodeHexTx(tx_result);

    return true;
}

bool TransactionBuilder::AddSaplingSpend_process_offline_transaction(
    libzcash::SaplingExpandedSpendingKey expsk,
    libzcash::SaplingNote note,
    uint256 anchor,
    uint64_t lWitnessPosition,
    unsigned char *pcWitness)
{
    myCharArray_s sWitness;

    // Consistency check: all anchors must equal the first one
    if (!spends.empty()) {
        if (spends[0].anchor != anchor) {
            return false;
        }
    }

    libzcash::MerklePath saplingMerklePath; //Unused parameter. required by spends.emplace_back()
    spends.emplace_back(expsk, note, anchor, saplingMerklePath);

    alWitnessPosition.emplace_back(lWitnessPosition);
    memcpy (&sWitness.cArray[0],pcWitness,sizeof(sWitness.cArray));
    asWitness.emplace_back(sWitness);

    mtx.valueBalance += note.value();

    boost::optional<CTransaction> maybe_tx = CTransaction(mtx);
    auto tx_result = maybe_tx.get();
    auto signedtxn = EncodeHexTx(tx_result);

    return true;
}


bool TransactionBuilder::AddSaplingSpend_prepare_offline_transaction(
    std::string sFromAddr,
    libzcash::SaplingNote note,
    uint256 anchor,
    uint64_t lWitnessPosition,
    unsigned char *pcWitness)
{
    myCharArray_s sWitness;

    // Consistency check: all anchors must equal the first one
    if (!spends.empty()) {
        if (spends[0].anchor != anchor) {
            return false;
        }
    }

    fromAddress_ = sFromAddr;

    libzcash::MerklePath saplingMerklePath;                   //Unused, just to keep spends happy
    libzcash::SaplingExpandedSpendingKey expsk;     //Unused, just to keep spends happy
    spends.emplace_back(expsk, note, anchor, saplingMerklePath);

    alWitnessPosition.emplace_back(lWitnessPosition);
    memcpy (&sWitness.cArray[0],pcWitness,sizeof(sWitness.cArray));
    asWitness.emplace_back(sWitness);

    mtx.valueBalance += note.value();

    return true;
}

bool TransactionBuilder::AddSaplingSpendRaw(
  libzcash::SaplingPaymentAddress from,
  CAmount value,
  SaplingOutPoint op)
{
    rawSpends.emplace_back(from, value, op);

    // Consistency check: all from addresses must equal the first one
    if (!rawSpends.empty()) {
        if (!(rawSpends[0].addr == from)) {
            return false;
        }
    }

    return true;
}

void TransactionBuilder::AddSaplingOutput(
    uint256 ovk,
    libzcash::SaplingPaymentAddress to,
    CAmount value,
    std::array<unsigned char, ZC_MEMO_SIZE> memo)
{
    libzcash::Zip212Enabled zip_212_enabled;

    // Sanity check: cannot add Sapling output to pre-Sapling transaction
    if (mtx.nVersion < SAPLING_TX_VERSION) {
        throw std::runtime_error("TransactionBuilder cannot add Sapling output to pre-Sapling transaction");
    }

    if (nMaxConnections>0)
    {
        //Online
        zip_212_enabled = libzcash::Zip212Enabled::BeforeZip212;
        // We use nHeight = chainActive.Height() + 1 since the output will be included in the next block
        if (NetworkUpgradeActive(nHeight + 1, consensusParams, Consensus::UPGRADE_CANOPY)) {
            zip_212_enabled = libzcash::Zip212Enabled::AfterZip212;
        }
    }
    else
    {
        //Offline
        if (cZip212_enabled==0)
        {
             zip_212_enabled = libzcash::Zip212Enabled::BeforeZip212;
        }
        else
        {
            zip_212_enabled = libzcash::Zip212Enabled::AfterZip212;
        }
    }

    auto note = libzcash::SaplingNote(to, value, zip_212_enabled);
    outputs.emplace_back(ovk, note, memo);
    mtx.valueBalance -= value;
}

void TransactionBuilder::AddSaplingOutput_offline_transaction(
    std::string address,
    CAmount value,
    std::array<unsigned char, ZC_MEMO_SIZE> memo)
{
    auto addr = DecodePaymentAddress(address);
    assert(boost::get<libzcash::SaplingPaymentAddress>(&addr) != nullptr);
    auto to = boost::get<libzcash::SaplingPaymentAddress>(addr);

    libzcash::Zip212Enabled zip_212_enabled = libzcash::Zip212Enabled::BeforeZip212;
    // We use nHeight = chainActive.Height() + 1 since the output will be included in the next block
    if (NetworkUpgradeActive(nHeight + 1, consensusParams, Consensus::UPGRADE_CANOPY)) {
        zip_212_enabled = libzcash::Zip212Enabled::AfterZip212;
    }

    auto note = libzcash::SaplingNote(to, value, zip_212_enabled);

    //Spending key not available when creating the transaction which will be signed offline
    //The offline transaction builder is not using the ovk field.
    uint256 ovk;
    outputs.emplace_back(ovk, note, memo);

    sOutputRecipients.push_back(address);

    mtx.valueBalance -= value;
}

void TransactionBuilder::AddPaymentOutput( std::string sAddr, CAmount iValue, std::string sMemo)
{
    Output_s sOutput;
    sOutput.sAddr = sAddr;
    sOutput.iValue = iValue;
    sOutput.sMemo  = sMemo;

    outputs_offline.emplace_back(sOutput);
}

void TransactionBuilder::AddSaplingOutputRaw(
    libzcash::SaplingPaymentAddress to,
    CAmount value,
    std::array<unsigned char, ZC_MEMO_SIZE> memo)
{
    rawOutputs.emplace_back(to, value, memo);
}

void TransactionBuilder::ConvertRawSaplingOutput(uint256 ovk)
{
    for (int i = 0; i < rawOutputs.size(); i++) {
        AddSaplingOutput(ovk, rawOutputs[i].addr, rawOutputs[i].value, rawOutputs[i].memo);
    }
}


void TransactionBuilder::AddTransparentInput(COutPoint utxo, CScript scriptPubKey, CAmount value, uint32_t _nSequence)
{
    if (keystore == nullptr) {
        if (!scriptPubKey.IsPayToCryptoCondition())
        {
            throw std::runtime_error("Cannot add transparent inputs to a TransactionBuilder without a keystore, except with crypto conditions");
        }
    }

    mtx.vin.emplace_back(utxo);
    mtx.vin[mtx.vin.size() - 1].nSequence = _nSequence;
    tIns.emplace_back(scriptPubKey, value);
}

bool TransactionBuilder::AddTransparentOutput(CTxDestination& to, CAmount value)
{
    if (!IsValidDestination(to)) {
        return false;
    }

    CScript scriptPubKey = GetScriptForDestination(to);
    CTxOut out(value, scriptPubKey);
    mtx.vout.push_back(out);

    return true;
}

bool TransactionBuilder::AddOpRetLast()
{
    CScript s;
    if (opReturn)
    {
        s = opReturn.value();
        CTxOut out(0, s);
        mtx.vout.push_back(out);
    }
    return true;
}

void TransactionBuilder::AddOpRet(CScript &s)
{
    opReturn.emplace(CScript(s));
}

void TransactionBuilder::SetFee(CAmount fee)
{
    this->fee = fee;
}

void TransactionBuilder::SetMinConfirmations(int iMinConf)
{
  this->iMinConf=iMinConf;
}

void TransactionBuilder::SetHeight(const Consensus::Params& consensusParams, int nHeight)
{
    this->nHeight = nHeight;
    this->consensusParams = consensusParams;
    consensusBranchId = CurrentEpochBranchId(nHeight, consensusParams);
}

void TransactionBuilder::SetExpiryHeight(int expHeight)
{
    this->mtx.nExpiryHeight = expHeight;
}

void TransactionBuilder::SendChangeTo(libzcash::SaplingPaymentAddress changeAddr, uint256 ovk)
{
    zChangeAddr = std::make_pair(ovk, changeAddr);
    tChangeAddr = boost::none;
}

bool TransactionBuilder::SendChangeTo(CTxDestination& changeAddr)
{
    if (!IsValidDestination(changeAddr)) {
        return false;
    }

    tChangeAddr = changeAddr;
    zChangeAddr = boost::none;

    return true;
}

//pcOutput size must be 2xiSize+1
bool CharArrayToHex(unsigned char *pcInput, unsigned int iSize, char *pcOutput)
{
  unsigned int iI;

  for(iI=0;iI<iSize;iI++)
  {
    sprintf(&pcOutput[iI*2], "%02X", pcInput[iI]);
  }
  pcOutput[iI*2+1]=0;//Null terminate the string
  return true;
}

std::string TransactionBuilder::Build_offline_transaction()
{
    std::string sReturn="";
    std::string sTmp;

    try
    {
        std::string sChecksumInput="";
        //printf("transaction_builder.cpp Build_offline_transaction() enter\n");fflush(stdout);
        //
        // Consistency checks
        //

        unsigned int iChecksum=0;
        unsigned int iI;

        if (spends.size()<=0)
        {
          return "Error: No spends specified";
        }

        // Valid change
        CAmount change = mtx.valueBalance - fee;

        //printf("Build_offline_transaction() change = balance - fee : %ld=%ld-%ld\n",change,mtx.valueBalance,fee); fflush(stdout);
        for (auto tIn : tIns)
        {
            change += tIn.value;
            //printf("Build_offline_transaction() change+=tIn.value  Result:%ld,%ld\n",change,tIn.value); fflush(stdout);
        }
        for (auto tOut : mtx.vout) {
            change -= tOut.nValue;
            //printf("Build_offline_transaction() change-=tOut.value  Result:%ld,%ld\n",change,tOut.nValue); fflush(stdout);
        }
        if (change < 0)
        {
            //printf("Build_offline_transaction() Change < 0 - return\n"); fflush(stdout);
            return "Error: Change < 0";
        }

        //
        // Change output
        //
        if (change > 0)
        {
            // Send change to the specified change address. If no change address
            // was set, send change to the first Sapling address given as input.
            if (zChangeAddr)
            {
                //printf("Build_offline_transaction Build() 1\n");
                AddSaplingOutput(zChangeAddr->first, zChangeAddr->second, change);
            }
            else if (!spends.empty())
            {
                //printf("Build_offline_transaction Build() 3: Pay balance to ourselved. Amount:%ld\n", change);
                //auto fvk = spends[0].expsk.full_viewing_key();
                //auto note = spends[0].note;
                //libzcash::SaplingPaymentAddress changeAddr(note.d, note.pk_d);
                //AddSaplingOutput(fvk.ovk, changeAddr,   change);
                std::array<unsigned char, ZC_MEMO_SIZE> memo = {0x00};
                AddSaplingOutput_offline_transaction(fromAddress_, change, memo);
            }
            else
            {
                //printf("Build_offline_transaction() Could not calculate change amount\n");
                return "Error: Could not calculate the amount of change";
            }
        }

        //Parameter   [0]: Project - Pirate Chain='arrr'
        //            [1]: Version - Layout of the command fields
        // Version 1: [2] Pay from address
        //            [3] Array of spending notes, which contains the funds of the 'pay from address'. Zip212 supported
        //            [4] Array of recipient: address, amount, memo
        //            [5]..[13] Blockchain parameters
        //            [15] Checksum of all the characters in the command.
        std::string sVersion="1";

        sReturn="z_sign_offline arrr "+sVersion+" ";
        sChecksumInput="arrr "+sVersion+" ";


        //Parameter [2]: Pay from address:
        sReturn += "\"" + fromAddress_ + "\" ";
        sChecksumInput+=fromAddress_+" ";



        //Parameter [3]: Spending notes '[{"witnessposition":number,"witnesspath":hex,"note_d":hex,"note_pkd":hex,"note_r":hex,"value":number,"zip212":number},{...},{...}]'
        if (spends.size() <= 0)
        {
          sReturn="Error:No spends";
          return sReturn;
        }

        sChecksumInput+="spending notes: ";
        unsigned char cAnchor[32];
        sReturn=sReturn+"'[";
        for (size_t i = 0; i < spends.size(); i++)
        {
            //printf("transaction_builder.cpp process spends-for(%ld)\n",i);
            auto spend = spends[i];
            uint64_t      lWitnessPosition = alWitnessPosition[i];
            myCharArray_s sWitness         = asWitness[i];
            char cWitnessPathHex[ 2*(1 + 33 * SAPLING_TREE_DEPTH + 8) + 1 ];
            cWitnessPathHex[2*(1 + 33 * SAPLING_TREE_DEPTH + 8)]=0; //null terminate
            CharArrayToHex(&sWitness.cArray[0], sizeof(myCharArray_s), &cWitnessPathHex[0]);

            libzcash::diversifier_t d     = spend.note.d;
            uint256       pk_d            = spend.note.pk_d;
            uint256       rcm             = spend.note.rcm();
            uint64_t      lValue          = spend.note.value();
            char          cZip212_enabled;
            libzcash::Zip212Enabled zip_212_enabled = spend.note.get_zip_212_enabled();

            //Convert zip_212_enabled to a value that can transmitted in the transaction communication
            if (zip_212_enabled == libzcash::Zip212Enabled::BeforeZip212)
            {
                cZip212_enabled=0;
            }
            else if (zip_212_enabled == libzcash::Zip212Enabled::AfterZip212)
            {
                cZip212_enabled=1;
            }
            else
            {
                sReturn="Internal error: Unknown value for note.zip_212_enabled. Expected BeforeZip212 or AfterZip212";
                return sReturn;
            }

            unsigned char cD[ZC_DIVERSIFIER_SIZE];
            char          cDHex[2*ZC_DIVERSIFIER_SIZE+1];
            std::memcpy(&cD[0], &d, ZC_DIVERSIFIER_SIZE);
            CharArrayToHex(&cD[0], ZC_DIVERSIFIER_SIZE, &cDHex[0]);

            unsigned char cPK_D[32];
            char          cPK_DHex[2*32+1];
            std::memcpy(&cPK_D[0], &pk_d, 32);
            CharArrayToHex(&cPK_D[0], 32, &cPK_DHex[0]);

            unsigned char cR[32];
            char          cRHex[2*32+1];
            std::memcpy(&cR[0], &rcm, 32);
            CharArrayToHex(&cR[0], 32, &cRHex[0]);

            char cHex[ 20+ 2*(1 + 33 * SAPLING_TREE_DEPTH + 8) + 1];
            //20                        14                8            11             9            8              9              2
            //{"witnessposition":number,"witnesspath":hex,"note_d":hex,"note_pkd":hex,"note_r":hex,"value":number,"zip212":number},{...},{...}]'
            snprintf(&cHex[0],sizeof(cHex),"{\"witnessposition\":\"%ld\",",lWitnessPosition);
            sReturn=sReturn+cHex;
            sTmp = strprintf("%u ", lWitnessPosition);
            sChecksumInput+=sTmp;


            snprintf(&cHex[0],sizeof(cHex),"\"witnesspath\":\"%s\",", &cWitnessPathHex[0]);
            sReturn=sReturn+cHex;
            sTmp = strprintf("%s ",&cWitnessPathHex[0]);
            sChecksumInput+=sTmp;


            snprintf(&cHex[0],sizeof(cHex),"\"note_d\":\"%s\",", &cDHex[0]);
            sReturn=sReturn+cHex;
            sTmp = strprintf("%s ",&cDHex[0]);
            sChecksumInput+=sTmp;


            snprintf(&cHex[0],sizeof(cHex),"\"note_pkd\":\"%s\",", &cPK_DHex[0]);
            sReturn=sReturn+cHex;
            sTmp = strprintf("%s ",&cPK_DHex[0]);
            sChecksumInput+=sTmp;


            snprintf(&cHex[0],sizeof(cHex),"\"note_r\":\"%s\",", &cRHex[0]);
            sReturn=sReturn+cHex;
            sTmp = strprintf("%s ",&cRHex[0]);
            sChecksumInput+=sTmp;


            snprintf(&cHex[0],sizeof(cHex),"\"value\":%ld,", lValue);
            sReturn=sReturn+cHex;
            sTmp = strprintf("%ld ",lValue);
            sChecksumInput+=sTmp;


            //Zip212 enabled for the specific note
            snprintf(&cHex[0],sizeof(cHex),"\"zip212\":%u}", cZip212_enabled);
            sReturn=sReturn+cHex;
            if (cZip212_enabled==0)
            {
                sTmp="0 ";
            }
            else
            {
                sTmp="1 ";
            }
            sChecksumInput+=sTmp;


            //Last entry: Must close the array with ]'
            //otherwide start the next entry with a ,
            if (i == spends.size()-1)
            {
              sReturn=sReturn+"]' ";
            }
            else
            {
              sReturn=sReturn+",";
            }
            std::memcpy(&cAnchor[0], &spend.anchor, 32);
        }



        //Parameter [4]: Outputs (recipients)
        //printf("Build_offline_transaction() [4] Outputs: Total=%ld\n\n", outputs.size());
        if (sOutputRecipients.size() != outputs.size())
        {
          //printf("Build_offline_transaction() [4] Internal error: sOutputRecipients.size(%ld) != outputs.size(%ld)\n\n",sOutputRecipients.size(), outputs.size());
          sReturn="Internal error: sOutputRecipients.size() != outputs.size()";
          return sReturn;
        }
        sChecksumInput+="outputs: ";
        sReturn=sReturn+"'[";
        for (size_t i = 0; i < outputs.size(); i++)
        {
            std::string sHexMemo;
            auto output = outputs[i];

            sReturn=sReturn+strprintf("{\"address\":\"%s\",\"amount\":%ld",sOutputRecipients[i].c_str(), output.note.value() );

            //Set length to maximum.
            size_t iStrLen = ZC_MEMO_SIZE;
            if (output.memo.data()[ (ZC_MEMO_SIZE-1) ] == 0)
            {
              //If the string is null terminated, evaluate its length to see
              //if its shorter:
              iStrLen = strlen( reinterpret_cast<char*>(output.memo.data()) );
            }

            if (iStrLen > 0)
            {
              //Note: Memo is ASCII encoded character array of max. ZC_MEMO_SIZE characters.
              //    : The array is not null terminated if all 512 chars are populated!
              //    : Convert to hex encoded string for offline transaction:
              char caHexMemo[ZC_MEMO_SIZE*2+1];
              memset(&caHexMemo[0], 0, sizeof(caHexMemo));
              CharArrayToHex(output.memo.data(), iStrLen, (char *)&caHexMemo[0]);
              sHexMemo = strprintf("%s", &caHexMemo[0]);
              sReturn=sReturn+ ",\"memo\":\""+sHexMemo+"\"}";

              //printf("transaction_builder.cpp process outputs-for(%ld). Adres=%s Amount=%ld Memo=%s\n\n",i, sOutputRecipients[i].c_str(), output.note.value(), sHexMemo.c_str() );
            }
            else
            {
              sReturn=sReturn+strprintf("}");
              //printf("transaction_builder.cpp process outputs-for(%ld). Adres=%s Amount=%ld No memo\n\n",i, sOutputRecipients[i].c_str(), output.note.value() );
            }

            if (i==(outputs.size()-1))
            {
              sReturn=sReturn+"]' ";
            }
            else
            {
              sReturn=sReturn+",";
            }

            //sChecksumInput+=sOutputRecipients[i] +"\n"+ std::to_string(output.note.value() ) +"\n"+ sHexMemo +"\n";
            sTmp = strprintf("%s ",sOutputRecipients[i].c_str() );
            sChecksumInput+=sTmp;
            sTmp = strprintf("%ld ",output.note.value() );
            sChecksumInput+=sTmp;
            sTmp = strprintf("%s ",sHexMemo.c_str() );
            sChecksumInput+=sTmp;
        }


        //Parameter [5]: Minimum confirmations
        //printf("Build_offline_transaction() [5] Minimum confirmations %d\n\n",iMinConf);
        sReturn=sReturn+strprintf("%d ",iMinConf);
        sTmp = strprintf("%d ",iMinConf);
        sChecksumInput+=sTmp;


        //Parameter [6]: Miners fee
        //printf("Build_offline_transaction() [6] Miners fee %ld\n\n",fee);
        sReturn=sReturn+strprintf("%ld ",fee);
        sTmp = strprintf("%ld ",fee);
        sChecksumInput+=sTmp;


        //Parameter [7]: Next block height
        //printf("Build_offline_transaction() [7] Next block height: '%d'\n\n", nHeight);
        sReturn=sReturn+strprintf("%d ",nHeight);
        sTmp = strprintf("%d ",nHeight);
        sChecksumInput+=sTmp;


        //Parameter [8]: BranchId (uint32_t)
        auto BranchId = CurrentEpochBranchId(nHeight, consensusParams);
        //printf("Build_offline_transaction() [8] BranchId: '%u'\n\n", BranchId);
        sReturn=sReturn+strprintf("%u ",BranchId);
        sTmp = strprintf("%u ",BranchId);
        sChecksumInput+=sTmp;


        //Parameter [9]: Anchor
        //printf("Build_offline_transaction() [9] Anchor\n");
        char          cAnchorHex[2*32+1];
        CharArrayToHex(&cAnchor[0], 32, &cAnchorHex[0]);
        sReturn=sReturn+"\""+cAnchorHex+"\" ";
        sTmp = strprintf("%s ",&cAnchorHex[0]);
        sChecksumInput+=sTmp;


        //Parameter [10] : bool fOverwintered
        if (mtx.fOverwintered == true)
        {
          sReturn=sReturn+"1 ";
          sChecksumInput += "1 ";
        }
        else
        {
          sReturn=sReturn+"0 ";
          sChecksumInput += "0 ";
        }


        //Parameter [11] : uint32_t nExpiryHeight
        sReturn=sReturn+strprintf("%u ",mtx.nExpiryHeight);
        sTmp = strprintf("%u ",mtx.nExpiryHeight);
        sChecksumInput+=sTmp;


        //Parameter [12]: uint32_t nVersionGroupId
        sReturn=sReturn+strprintf("%u ",mtx.nVersionGroupId);
        sTmp = strprintf("%u ",mtx.nVersionGroupId);
        sChecksumInput+=sTmp;


        //Parameter [13]: int32_t nVersion);
        sReturn=sReturn+strprintf("%d ",mtx.nVersion);
        sTmp = strprintf("%d ",mtx.nVersion);
        sChecksumInput+=sTmp;

        //Parameter [14]: Zip212 enabled: Used for the outputs
        std::string sZip212_enabled = "0";
        // We use nHeight = chainActive.Height() + 1 since the output will be included in the next block
        if (NetworkUpgradeActive(nHeight + 1, consensusParams, Consensus::UPGRADE_CANOPY)) {
            sZip212_enabled = "1";
        }
        sReturn+= sZip212_enabled+" ";
        sChecksumInput+=sZip212_enabled+" ";

        //Parameter [15]: checksum
        //A simple checksum of the full string, to detect copy/paste errors between the wallets
        //The checksum equals the sum of the ASCII values of all the characters in the string:
        //printf("sChecksumInput:\n%s\n\n",sChecksumInput.c_str() );
        iChecksum=0x01;
        for (iI=0;iI<sChecksumInput.length();iI++)
        {
          unsigned int iVal = (unsigned int)sChecksumInput.at(iI);
          iChecksum = iChecksum + iVal;
        }
        sTmp = strprintf("%u",iChecksum);
        //printf("Calculated checksum: %s\n",sTmp.c_str() );
        //Append the checksum to the protocol data:
        sReturn+=sTmp;

        //printf("\n\nPaste the full contents into the console of your offline wallet to sign the transaction:\n%s\n\n",sReturn.c_str());
        // add op_return if there is one to add
        //AddOpRetLast(); ??
        //printf("Build_offline_transaction() %s\n\n",sReturn.c_str() );
        return sReturn;
    }
    catch (const char* pcError)
    {
        sReturn = "Exception occurred: ";
        sReturn.assign(pcError);
        printf("%s\n\n",sReturn.c_str() );
        return sReturn;
    }
}

TransactionBuilderResult TransactionBuilder::Build()
{
    boost::optional<CTransaction> maybe_tx = CTransaction(mtx);
    auto tx_result = maybe_tx.get();
    auto signedtxn = EncodeHexTx(tx_result);

    //
    // Consistency checks
    //

    // Valid change
    CAmount change = mtx.valueBalance - fee;
    for (auto tIn : tIns) {
        change += tIn.value;
    }
    for (auto tOut : mtx.vout) {
        change -= tOut.nValue;
    }
    if (change < 0) {
        return TransactionBuilderResult("Change cannot be negative");
    }

    //
    // Change output
    //

    if (change > 0) {
        // Send change to the specified change address. If no change address
        // was set, send change to the first Sapling address given as input.
        if (zChangeAddr) {
            AddSaplingOutput(zChangeAddr->first, zChangeAddr->second, change);
        } else if (tChangeAddr) {
            // tChangeAddr has already been validated.
            assert(AddTransparentOutput(tChangeAddr.value(), change));
        } else if (!spends.empty()) {
            auto fvk = spends[0].expsk.full_viewing_key();
            auto note = spends[0].note;
            libzcash::SaplingPaymentAddress changeAddr(note.d, note.pk_d);
            AddSaplingOutput(fvk.ovk, changeAddr, change);
        } else {
            return TransactionBuilderResult("Could not determine change address");
        }
    }

    //
    // Sapling spends and outputs
    //

    auto ctx = librustzcash_sapling_proving_ctx_init();

    // Create Sapling SpendDescriptions
    //for (auto spend : spends)
    for (size_t i = 0; i < spends.size(); i++)
    {
        auto spend = spends[i];
        uint64_t      lWitnessPosition = alWitnessPosition[i];
        myCharArray_s sWitness         = asWitness[i];

        // libzcash::diversifier_t d     = spend.note.d;
        // uint256       pk_d            = spend.note.pk_d;
        // uint256       rcm             = spend.note.rcm;
        // uint64_t      value           = spend.note.value();
        // libzcash::SaplingNote myNote (d, pk_d, value, r);

        //printf("transaction_builder.cpp process spends-for()\n");
        //auto cm = spend.note.cm();
        //auto nf = spend.note.nullifier(spend.expsk.full_viewing_key(), lWitnessPosition);
        auto cmu = spend.note.cmu();
        auto nf = spend.note.nullifier(spend.expsk.full_viewing_key(), lWitnessPosition);
        if (!(cmu && nf))
        {
            //printf("transaction_builder.cpp cm && nf error\n");
            librustzcash_sapling_proving_ctx_free(ctx);
            return TransactionBuilderResult("Spend is invalid");
        }

/*
        //printf("transaction_builder.cpp passed (cm&&nf)\n");
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        //printf("transaction_builder.cpp 1\n");
        ss << spend.witness.path();
        //printf("transaction_builder.cpp 2\n");
        std::vector<unsigned char> witness(ss.begin(), ss.end());
        //printf("transaction_builder.cpp 3\n");
*/
        std::vector<unsigned char> witness ( &sWitness.cArray[0], &sWitness.cArray[0] +  sizeof(myCharArray_s) );

        SpendDescription sdesc;
        uint256 rcm = spend.note.rcm();
        if (!librustzcash_sapling_spend_proof(
                ctx,
                spend.expsk.full_viewing_key().ak.begin(),
                spend.expsk.nsk.begin(),
                spend.note.d.data(),
                rcm.begin(),
                spend.alpha.begin(),
                spend.note.value(),
                spend.anchor.begin(),
                witness.data(),
                sdesc.cv.begin(),
                sdesc.rk.begin(),
                sdesc.zkproof.data())) {
            librustzcash_sapling_proving_ctx_free(ctx);
            return TransactionBuilderResult("Spend proof failed");
        }
        //printf("transaction_builder.cpp librustzcash_sapling_spend_proof() passed!\n");
        sdesc.anchor = spend.anchor;
        sdesc.nullifier = *nf;
        mtx.vShieldedSpend.push_back(sdesc);
    }

    // Create Sapling OutputDescriptions
    for (auto output : outputs) {
        // Check this out here as well to provide better logging.
        if (!output.note.cmu()) {
            librustzcash_sapling_proving_ctx_free(ctx);
            return TransactionBuilderResult("Output is invalid");
        }

        auto odesc = output.Build(ctx);
        if (!odesc) {
            librustzcash_sapling_proving_ctx_free(ctx);
            return TransactionBuilderResult("Failed to create output description");
        }

        mtx.vShieldedOutput.push_back(odesc.get());
    }

    // add op_return if there is one to add
    AddOpRetLast();

    //
    // Signatures
    // Empty output script.
    uint256 dataToBeSigned;
    CScript scriptCode;
    try {
        dataToBeSigned = SignatureHash(scriptCode, mtx, NOT_AN_INPUT, SIGHASH_ALL, 0, consensusBranchId);
    } catch (std::logic_error ex) {
        librustzcash_sapling_proving_ctx_free(ctx);
        return TransactionBuilderResult("Could not construct signature hash: " + std::string(ex.what()));
    }

    // Create Sapling spendAuth and binding signatures
    for (size_t i = 0; i < spends.size(); i++) {
        librustzcash_sapling_spend_sig(
            spends[i].expsk.ask.begin(),
            spends[i].alpha.begin(),
            dataToBeSigned.begin(),
            mtx.vShieldedSpend[i].spendAuthSig.data());
    }
    librustzcash_sapling_binding_sig(
        ctx,
        mtx.valueBalance,
        dataToBeSigned.begin(),
        mtx.bindingSig.data());

    librustzcash_sapling_proving_ctx_free(ctx);

    // Transparent signatures
    CTransaction txNewConst(mtx);
    for (int nIn = 0; nIn < mtx.vin.size(); nIn++) {
        auto tIn = tIns[nIn];
        SignatureData sigdata;
        bool signSuccess = ProduceSignature(
            TransactionSignatureCreator(
                keystore, &txNewConst, nIn, tIn.value, SIGHASH_ALL),
            tIn.scriptPubKey, sigdata, consensusBranchId);

        if (!signSuccess) {
            return TransactionBuilderResult("Failed to sign transaction");
        } else {
            UpdateTransaction(mtx, nIn, sigdata);
        }
    }

    maybe_tx = CTransaction(mtx);
    tx_result = maybe_tx.get();
    signedtxn = EncodeHexTx(tx_result);
    //printf("signed txn: %s\n\n",signedtxn.c_str() );

    //printf("transaction_builder.cpp Done\n");fflush(stdout);
    return CTransaction(mtx);
}
