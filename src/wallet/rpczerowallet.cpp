// Copyright (c) 2019 Cryptoforge
// Copyright (c) 2019 The Zero developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "init.h"
#include "key_io.h"
#include "rpc/server.h"
#include "wallet.h"
#include "rpczerowallet.h"
#include "utilmoneystr.h"

using namespace std;
using namespace libzcash;

bool EnsureWalletIsAvailable(bool avoidException);

void zsTxSpendsToJSON(const CWalletTx& wtx, UniValue& spends, CAmount& totalSpends, CAmount& filteredSpends, const std::string& strAddress, bool filterByAddress) {

  LOCK2(cs_main, pwalletMain->cs_wallet);

  //Used to identify incomplete key sets
  int vinCount = 0;
  int vinSpendCount = 0;
  int shieldedSpendCount = 0;
  int shieldedSpendsSpentCount = 0;

  //Check address
  bool isTAddress = false;
  bool isZsAddress = false;
  bool isZcAddress = false;

  CTxDestination tAddress = DecodeDestination(strAddress);
  auto zAddress = DecodePaymentAddress(strAddress);
  SaplingPaymentAddress zsAddress;
  SproutPaymentAddress zcAddress;

  if (filterByAddress) {

    if (IsValidDestination(tAddress))
      isTAddress = true;

    if (IsValidPaymentAddress(zAddress)) {
      if (boost::get<libzcash::SproutPaymentAddress>(&zAddress) != nullptr) {
          zcAddress = boost::get<libzcash::SproutPaymentAddress>(zAddress);
          isZcAddress = true;
      }
      if (boost::get<libzcash::SaplingPaymentAddress>(&zAddress) != nullptr) {
          zsAddress = boost::get<libzcash::SaplingPaymentAddress>(zAddress);
          isZsAddress = true;
      }
    }
  }


  // Transparent Inputs belonging to the wallet
  UniValue tSpends(UniValue::VARR);
  if (isTAddress || !filterByAddress) {
    for (int i = 0; i < wtx.vin.size(); i++) {
      vinCount++;
      const CTxIn& txin = wtx.vin[i];
      UniValue obj(UniValue::VOBJ);
      CTxDestination address;
      const CWalletTx* parent = pwalletMain->GetWalletTx(txin.prevout.hash);
      if (parent != NULL) {
        const CTxOut& parentOut = parent->vout[txin.prevout.n];
        ExtractDestination(parentOut.scriptPubKey, address);
        if(IsMine(*pwalletMain, address)){
          vinSpendCount++;
          totalSpends += CAmount(-parentOut.nValue);
          obj.push_back(Pair("address",EncodeDestination(address)));
          obj.push_back(Pair("scriptPubKey",HexStr(parentOut.scriptPubKey.begin(), parentOut.scriptPubKey.end())));
          obj.push_back(Pair("amount",ValueFromAmount(-parentOut.nValue)));
          obj.push_back(Pair("spendTxid",parent->GetHash().ToString()));
          obj.push_back(Pair("spendVout",(int)txin.prevout.n));
          CTxDestination filterAddress = DecodeDestination(strAddress);
          if (address == tAddress || !filterByAddress) {
            filteredSpends += CAmount(-parentOut.nValue);
            tSpends.push_back(obj);
          }
        }
      }
    }
  }
  spends.push_back(Pair("transparentSpends",tSpends));

  // Sapling Inputs belonging to the wallet
  UniValue zsSpends(UniValue::VARR);
  if (isZsAddress || !filterByAddress) {
    for (int i = 0; i < wtx.vShieldedSpend.size(); i++) {
      shieldedSpendCount++;
      const SpendDescription& spendDesc = wtx.vShieldedSpend[i];
      UniValue obj(UniValue::VOBJ);

      SaplingOutPoint op = pwalletMain->mapSaplingNullifiersToNotes[spendDesc.nullifier];

      if (pwalletMain->IsSaplingNullifierFromMe(spendDesc.nullifier)) {
        const CWalletTx* parent = pwalletMain->GetWalletTx(pwalletMain->mapSaplingNullifiersToNotes[spendDesc.nullifier].hash);
        const OutputDescription& output = parent->vShieldedOutput[op.n];
        auto nd = pwalletMain->mapWallet[pwalletMain->mapSaplingNullifiersToNotes[spendDesc.nullifier].hash].mapSaplingNoteData[op];
        auto pt = libzcash::SaplingNotePlaintext::decrypt(output.encCiphertext,nd.ivk,output.ephemeralKey,output.cm);

        if (pt) {
          auto note = pt.get();
          auto pa = nd.ivk.address(note.d);
          auto address = pa.get();
          shieldedSpendsSpentCount++;
          totalSpends += CAmount(-note.value());
          obj.push_back(Pair("address",EncodePaymentAddress(address)));
          obj.push_back(Pair("amount", ValueFromAmount(CAmount(-note.value()))));
          obj.push_back(Pair("spendTxid",parent->GetHash().ToString()));
          obj.push_back(Pair("spendSheildedOutputIndex",(int)op.n));
          if (address == zsAddress || !filterByAddress) {
            filteredSpends += CAmount(-note.value());
            zsSpends.push_back(obj);
          }
        }
      }
    }
  }
  spends.push_back(Pair("saplingSpends",zsSpends));

  //Sprout Inputs belonging to the wallet
  UniValue zcSpends(UniValue::VARR);
  if (isZcAddress || !filterByAddress) {
    for (int itr = 0; itr < wtx.vJoinSplit.size(); itr++) {
      const JSDescription& jsdesc = wtx.vJoinSplit[itr];

      for (const uint256 &nullifier : jsdesc.nullifiers) {
        UniValue obj(UniValue::VOBJ);
        JSOutPoint op = pwalletMain->mapSproutNullifiersToNotes[nullifier];

        if (pwalletMain->IsSproutNullifierFromMe(nullifier)) {
          const CWalletTx* parent = pwalletMain->GetWalletTx(pwalletMain->mapSproutNullifiersToNotes[nullifier].hash);

          int i = op.js; // Index into CTransaction.vJoinSplit
          int j = op.n; // Index into JSDescription.ciphertexts

          std::set<libzcash::SproutPaymentAddress> addresses;
          pwalletMain->GetSproutPaymentAddresses(addresses);
          for (auto addr : addresses) {
            try {
              SproutSpendingKey sk;
              pwalletMain->GetSproutSpendingKey(addr, sk);
              ZCNoteDecryption decryptor(sk.receiving_key());

              // determine amount of funds in the note
              auto hSig = parent->vJoinSplit[i].h_sig(*pzcashParams, parent->joinSplitPubKey);

              SproutNotePlaintext pt = SproutNotePlaintext::decrypt(
                      decryptor,parent->vJoinSplit[i].ciphertexts[j],parent->vJoinSplit[i].ephemeralKey,hSig,(unsigned char) j);

              auto decrypted_note = pt.note(addr);
              totalSpends += CAmount(-decrypted_note.value());
              obj.push_back(Pair("address",EncodePaymentAddress(addr)));
              obj.push_back(Pair("amount",ValueFromAmount(CAmount(-decrypted_note.value()))));
              obj.push_back(Pair("spendTxid",parent->GetHash().ToString()));
              obj.push_back(Pair("spendJsIndex", i));
              obj.push_back(Pair("spendJsOutIndex", j));
              if (addr == zcAddress || !filterByAddress) {
                filteredSpends += CAmount(-decrypted_note.value());
                zcSpends.push_back(obj);
              }
            } catch (const note_decryption_failed &err) {
              //do nothing
            }
          }
        }
      }
    }
  }
  spends.push_back(Pair("sproutSpends",zcSpends));
  spends.push_back(Pair("totalSpends",ValueFromAmount(filteredSpends)));

  if (!filterByAddress) {
    if (filteredSpends != 0 && (vinCount != vinSpendCount || shieldedSpendCount != shieldedSpendsSpentCount)) {
      spends.push_back(Pair("missingSpendingKeys", true));
      spends.push_back(Pair("vinCount", vinCount));
      spends.push_back(Pair("vinSpendCount", vinSpendCount));
      spends.push_back(Pair("shieldedSpendCount", shieldedSpendCount));
      spends.push_back(Pair("shieldedSpendsSpentCount", shieldedSpendsSpentCount));
    } else {
        spends.push_back(Pair("missingSpendingKeys", false));
    }
  }
}


void zsTxReceivedToJSON(const CWalletTx& wtx, UniValue& received, CAmount& totalReceived, const std::string& strAddress, bool filterByAddress) {

  LOCK2(cs_main, pwalletMain->cs_wallet);

  //Check address
  bool isTAddress = false;
  bool isZsAddress = false;
  bool isZcAddress = false;

  CTxDestination tAddress = DecodeDestination(strAddress);
  auto zAddress = DecodePaymentAddress(strAddress);
  SaplingPaymentAddress zsAddress;
  SproutPaymentAddress zcAddress;

  if (filterByAddress) {

    if (IsValidDestination(tAddress))
      isTAddress = true;

    if (IsValidPaymentAddress(zAddress)) {
      if (boost::get<libzcash::SproutPaymentAddress>(&zAddress) != nullptr) {
          zcAddress = boost::get<libzcash::SproutPaymentAddress>(zAddress);
          isZcAddress = true;
      }
      if (boost::get<libzcash::SaplingPaymentAddress>(&zAddress) != nullptr) {
          zsAddress = boost::get<libzcash::SaplingPaymentAddress>(zAddress);
          isZsAddress = true;
      }
    }
  }


  //Transparent Received txos belonging to the wallet
  UniValue tReceived(UniValue::VARR);
  if (isTAddress || !filterByAddress) {
    for (int i = 0; i < wtx.vout.size(); i++) {
      const CTxOut& txout = wtx.vout[i];
      UniValue obj(UniValue::VOBJ);
      CTxDestination address;
      ExtractDestination(txout.scriptPubKey, address);
      if(IsMine(*pwalletMain, address)){
        obj.push_back(Pair("address",EncodeDestination(address)));
        obj.push_back(Pair("scriptPubKey",HexStr(txout.scriptPubKey.begin(), txout.scriptPubKey.end())));
        obj.push_back(Pair("amount",ValueFromAmount(txout.nValue)));
        obj.push_back(Pair("vout", i));
        if (address == tAddress || !filterByAddress) {
          totalReceived += CAmount(txout.nValue);
          tReceived.push_back(obj);
        }
      }
    }
  }
  received.push_back(Pair("transparentReceived",tReceived));


  //Sapling Sends belonging to the wallet
  UniValue zsReceived(UniValue::VARR);
  if (isZsAddress || !filterByAddress) {
    for (int i = 0; i < wtx.vShieldedOutput.size(); i++) {
      const OutputDescription& outputDesc = wtx.vShieldedOutput[i];
      UniValue obj(UniValue::VOBJ);
      bool changeTx = false;
      //Decrypt sapling incoming commitments using IVK
      std::set<libzcash::SaplingPaymentAddress> addresses;
      pwalletMain->GetSaplingPaymentAddresses(addresses);
      for (auto addr : addresses) {
        libzcash::SaplingExtendedSpendingKey extsk;
        if (pwalletMain->GetSaplingExtendedSpendingKey(addr, extsk)) {
          auto pt = libzcash::SaplingNotePlaintext::decrypt(
                  outputDesc.encCiphertext, extsk.expsk.full_viewing_key().in_viewing_key(), outputDesc.ephemeralKey, outputDesc.cm);

          if (pt) {
            auto note = pt.get();
            obj.push_back(Pair("address",EncodePaymentAddress(addr)));
            obj.push_back(Pair("amount", ValueFromAmount(CAmount(note.value()))));
            obj.push_back(Pair("sheildedOutputIndex",i));

            //Check Change Status
            if (wtx.vShieldedSpend.size()!=0) {
              std::set<std::pair<PaymentAddress, uint256>> nullifierSet;
              nullifierSet = pwalletMain->GetNullifiersForAddresses({addr});
              BOOST_FOREACH(const SpendDescription& spendDesc, wtx.vShieldedSpend) {
                if (nullifierSet.count(std::make_pair(addr, spendDesc.nullifier))) {
                    changeTx = true;
                }
              }
            }
            obj.push_back(Pair("change",changeTx));
            if (addr == zsAddress || !filterByAddress) {
              totalReceived += CAmount(note.value());
              zsReceived.push_back(obj);
            }
          }
        }
      }
    }
  }
  received.push_back(Pair("saplingReceived",zsReceived));

  //Sprout Received belonging to the wallet
  UniValue zcReceived(UniValue::VARR);
  if (isZcAddress || !filterByAddress) {
    for (int i = 0; i < wtx.vJoinSplit.size(); i++) {
      const JSDescription& jsdesc = wtx.vJoinSplit[i];

      for (int j = 0; j < jsdesc.ciphertexts.size(); j++) {
        UniValue obj(UniValue::VOBJ);
        bool changeTx = false;
        std::set<libzcash::SproutPaymentAddress> addresses;
        pwalletMain->GetSproutPaymentAddresses(addresses);
        for (auto addr : addresses) {
          try {
            SproutSpendingKey sk;
            pwalletMain->GetSproutSpendingKey(addr, sk);
            ZCNoteDecryption decryptor(sk.receiving_key());

            // determine amount of funds in the note
            auto hSig = wtx.vJoinSplit[i].h_sig(*pzcashParams, wtx.joinSplitPubKey);

            SproutNotePlaintext pt = SproutNotePlaintext::decrypt(
                    decryptor,wtx.vJoinSplit[i].ciphertexts[j],wtx.vJoinSplit[i].ephemeralKey,hSig,(unsigned char) j);

            auto decrypted_note = pt.note(addr);
            obj.push_back(Pair("address",EncodePaymentAddress(addr)));
            obj.push_back(Pair("amount",ValueFromAmount(CAmount(decrypted_note.value()))));
            obj.push_back(Pair("jsindex", i));
            obj.push_back(Pair("jsoutindex", j));

            //Check Change Status
            if (wtx.vJoinSplit.size()!=0) {
              std::set<std::pair<PaymentAddress, uint256>> nullifierSet;
              nullifierSet = pwalletMain->GetNullifiersForAddresses({addr});
              BOOST_FOREACH(const JSDescription& jDesc, wtx.vJoinSplit) {
                for (const uint256 &nullifier : jDesc.nullifiers) {
                  if (nullifierSet.count(std::make_pair(addr, nullifier))) {
                      changeTx = true;
                  }
                }
              }
            }
            obj.push_back(Pair("change",changeTx));
            if (addr == zcAddress || !filterByAddress) {
              totalReceived += CAmount(decrypted_note.value());
              zcReceived.push_back(obj);
            }
          } catch (const note_decryption_failed &err) {
            //do nothing
          }
        }
      }
    }
  }
  received.push_back(Pair("sproutReceived",zcReceived));


  received.push_back(Pair("totalReceived",ValueFromAmount(totalReceived)));
}


void zsTxSendsToJSON(const CWalletTx& wtx, UniValue& sends, CAmount& totalSends, const std::string& strAddress, bool filterByAddress) {

  LOCK2(cs_main, pwalletMain->cs_wallet);

  //Used to identify incomplete key sets
  int shieldedOutputCount = 0;
  int shieldedOutputDecryptedCount = 0;

  //Check address
  bool isTAddress = false;
  bool isZsAddress = false;
  bool isZcAddress = false;

  CTxDestination tAddress = DecodeDestination(strAddress);
  auto zAddress = DecodePaymentAddress(strAddress);
  SaplingPaymentAddress zsAddress;
  SproutPaymentAddress zcAddress;

  if (filterByAddress) {

    if (IsValidDestination(tAddress))
      isTAddress = true;

    if (IsValidPaymentAddress(zAddress)) {
      if (boost::get<libzcash::SproutPaymentAddress>(&zAddress) != nullptr) {
          zcAddress = boost::get<libzcash::SproutPaymentAddress>(zAddress);
          isZcAddress = true;
      }
      if (boost::get<libzcash::SaplingPaymentAddress>(&zAddress) != nullptr) {
          zsAddress = boost::get<libzcash::SaplingPaymentAddress>(zAddress);
          isZsAddress = true;
      }
    }
  }

  //All Transparent Sends in the transaction

  UniValue tSends(UniValue::VARR);
  if (isTAddress || !filterByAddress) {
    for (int i = 0; i < wtx.vout.size(); i++) {
      const CTxOut& txout = wtx.vout[i];
      UniValue obj(UniValue::VOBJ);
      CTxDestination address;
      ExtractDestination(txout.scriptPubKey, address);
      obj.push_back(Pair("address",EncodeDestination(address)));
      obj.push_back(Pair("scriptPubKey",HexStr(txout.scriptPubKey.begin(), txout.scriptPubKey.end())));
      obj.push_back(Pair("amount",ValueFromAmount(-txout.nValue)));
      obj.push_back(Pair("vout", i));
      if (address == tAddress || !filterByAddress) {
        totalSends += CAmount(-txout.nValue);
        tSends.push_back(obj);
      }
    }
  }
  sends.push_back(Pair("transparentSends",tSends));

  //All Shielded Sends in the transaction
  UniValue zsSends(UniValue::VARR);
  if (isZsAddress || !filterByAddress) {
    for (int i = 0; i < wtx.vShieldedOutput.size(); i++) {
      const OutputDescription& outputDesc = wtx.vShieldedOutput[i];
      shieldedOutputCount++;
      UniValue obj(UniValue::VOBJ);
      bool changeTx = false;

      //Decrypt sapling outgoing t to z transaction using HDseed
      if (wtx.vShieldedSpend.size()==0) {
        HDSeed seed;
        if (pwalletMain->GetHDSeed(seed)) {
          auto opt = libzcash::SaplingOutgoingPlaintext::decrypt(
                  outputDesc.outCiphertext,ovkForShieldingFromTaddr(seed),outputDesc.cv,outputDesc.cm,outputDesc.ephemeralKey);

          if (opt) {
            auto opt_unwrapped = opt.get();
            auto pt = libzcash::SaplingNotePlaintext::decrypt(
                    outputDesc.encCiphertext,outputDesc.ephemeralKey,opt_unwrapped.esk,opt_unwrapped.pk_d,outputDesc.cm);

            if (pt) {
              shieldedOutputDecryptedCount++;
              auto pt_unwrapped = pt.get();
              libzcash::SaplingPaymentAddress sentAddr(pt_unwrapped.d, opt_unwrapped.pk_d);
              obj.push_back(Pair("address",EncodePaymentAddress(sentAddr)));
              obj.push_back(Pair("amount", ValueFromAmount(CAmount(-pt_unwrapped.value()))));
              obj.push_back(Pair("sheildedOutputIndex",i));
              obj.push_back(Pair("change",false));
              if (sentAddr == zsAddress || !filterByAddress) {
                totalSends += CAmount(-pt_unwrapped.value());
                zsSends.push_back(obj);
              }
            }
          }
        }

      //attempt Decryption of Outgoing Sapling using wallet extended spending keys
      } else {
        std::set<libzcash::SaplingPaymentAddress> addresses;
        pwalletMain->GetSaplingPaymentAddresses(addresses);
        for (auto addr : addresses) {
          libzcash::SaplingExtendedSpendingKey extsk;
          if (pwalletMain->GetSaplingExtendedSpendingKey(addr, extsk)) {
            auto opt = libzcash::SaplingOutgoingPlaintext::decrypt(
                    outputDesc.outCiphertext,extsk.expsk.full_viewing_key().ovk,outputDesc.cv,outputDesc.cm,outputDesc.ephemeralKey);

            if (opt) {
              auto opt_unwrapped = opt.get();
              auto pt = libzcash::SaplingNotePlaintext::decrypt(
                      outputDesc.encCiphertext,outputDesc.ephemeralKey,opt_unwrapped.esk,opt_unwrapped.pk_d,outputDesc.cm);

              if (pt) {
                auto pt_unwrapped = pt.get();
                shieldedOutputDecryptedCount++;
                libzcash::SaplingPaymentAddress sentAddr(pt_unwrapped.d, opt_unwrapped.pk_d);
                obj.push_back(Pair("address",EncodePaymentAddress(sentAddr)));
                obj.push_back(Pair("amount", ValueFromAmount(CAmount(-pt_unwrapped.value()))));
                obj.push_back(Pair("sheildedOutputIndex",i));

                //Check Change Status
                if (wtx.vShieldedSpend.size()!=0) {
                  std::set<std::pair<PaymentAddress, uint256>> nullifierSet;
                  nullifierSet = pwalletMain->GetNullifiersForAddresses({sentAddr});
                  BOOST_FOREACH(const SpendDescription& spendDesc, wtx.vShieldedSpend) {
                    if (nullifierSet.count(std::make_pair(sentAddr, spendDesc.nullifier))) {
                        changeTx = true;
                    }
                  }
                }
                obj.push_back(Pair("change",changeTx));
                if (sentAddr == zsAddress || !filterByAddress) {
                  totalSends += CAmount(-pt_unwrapped.value());
                  zsSends.push_back(obj);
                }
              }
            }
          }
        }
      }
    }
  }
  sends.push_back(Pair("saplingSends",zsSends));

  if (shieldedOutputCount != shieldedOutputDecryptedCount) {
    sends.push_back(Pair("missingSaplingOVK", true));
  } else {
    sends.push_back(Pair("missingSaplingOVK", false));
  }


  //Does the transaction contain sprout sends
  if (!filterByAddress) {
    UniValue zcSends(UniValue::VARR);
    BOOST_FOREACH(const JSDescription& jsDesc, wtx.vJoinSplit) {
      UniValue obj(UniValue::VOBJ);
      obj.push_back(Pair("address",""));
      obj.push_back(Pair("vpub_old",ValueFromAmount(CAmount(jsDesc.vpub_old))));
      obj.push_back(Pair("vpub_new",ValueFromAmount(CAmount(jsDesc.vpub_new))));
      zcSends.push_back(obj);
    }
    sends.push_back(Pair("sproutSends",zcSends));
  }

  sends.push_back(Pair("totalSends",ValueFromAmount(totalSends)));
}


void zsWalletTxJSON(const CWalletTx& wtx, UniValue& ret, const std::string strAddress, bool fBool, const int returnType) {

  LOCK2(cs_main, pwalletMain->cs_wallet);

  //Track total wallet spend and received
  CAmount totalSpends = 0;
  CAmount filteredSpends = 0;
  CAmount totalReceived = 0;
  CAmount totalSends = 0;

  //Various Univalue to be added to the final transaction
  UniValue spends(UniValue::VOBJ);
  UniValue received(UniValue::VOBJ);
  UniValue sends(UniValue::VOBJ);
  UniValue tx(UniValue::VOBJ);

  //Begin Compiling the Decrypted Transaction
  tx.push_back(Pair("txid", wtx.GetHash().ToString()));
  if (wtx.IsCoinBase())
  {
      tx.push_back(Pair("coinbase", true));
      if (wtx.GetDepthInMainChain() < 1)
          tx.push_back(Pair("category", "orphan"));
      else if (wtx.GetBlocksToMaturity() > 0)
          tx.push_back(Pair("category", "immature"));
      else
          tx.push_back(Pair("category", "generate"));
  } else {
    tx.push_back(Pair("coinbase", false));
    tx.push_back(Pair("category", "standard"));
  }

  tx.push_back(Pair("blockhash", wtx.hashBlock.GetHex()));
  tx.push_back(Pair("blockindex", wtx.nIndex));
  int confirms = wtx.GetDepthInMainChain();
  if(confirms > 0)
  {
      tx.push_back(Pair("blocktime", mapBlockIndex[wtx.hashBlock]->GetBlockTime()));
  } else {
      tx.push_back(Pair("blocktime", 0));
  }
  tx.push_back(Pair("expiryheight", (int64_t)wtx.nExpiryHeight));
  tx.push_back(Pair("confirmations", confirms));
  tx.push_back(Pair("time", wtx.GetTxTime()));
  tx.push_back(Pair("size", static_cast<uint64_t>(GetSerializeSize(static_cast<CTransaction>(wtx), SER_NETWORK, PROTOCOL_VERSION))));

  //Wallet Conflicts
  UniValue conflicts(UniValue::VARR);
  BOOST_FOREACH(const uint256& conflict, wtx.GetConflicts())
      conflicts.push_back(conflict.GetHex());
  tx.push_back(Pair("walletconflicts", conflicts));

  // Return Type used to determine what is included in the transaction
  // 0 Spends, Received and spends
  // 1 Sends
  // 2 Received
  // 3 Spends
  // TODO - Setup Enum, maybe...

  // Add Spends retrieved earlier
  if (returnType != 2) {
    zsTxSpendsToJSON(wtx, spends, totalSpends, filteredSpends, strAddress, fBool);
    if ((!fBool || filteredSpends != 0) && (returnType == 0 || returnType == 1)) {
      tx.push_back(Pair("spends",spends));
    }
  }
  // Get Received
  if (returnType == 0 || returnType == 2) {
    zsTxReceivedToJSON(wtx, received, totalReceived, strAddress, fBool);
    if (!fBool || totalReceived != 0) {
      tx.push_back(Pair("received",received));
    }
  }

  // Get Sends
  if (returnType == 0 || returnType == 3) {
    //Only include sends if there are spends that belong to the wallet.
    if (totalSpends != 0 || fBool) {
      zsTxSendsToJSON(wtx, sends, totalSends, strAddress, fBool);
    }
    if (!fBool || totalSends != 0) {
      tx.push_back(Pair("sends",sends));
    }
  }

  if ((returnType == 0 && (!fBool || filteredSpends != 0 || totalReceived != 0 || totalSends != 0))
   || (returnType == 1 && (!fBool || filteredSpends != 0))
   || (returnType == 2 && (!fBool || totalReceived != 0))
   || (returnType == 3 && (!fBool || totalSends != 0))) {
    ret.push_back(tx);
  }

}


UniValue zs_listtransactions(const UniValue& params, bool fHelp)
{
  if (!EnsureWalletIsAvailable(fHelp))
      return NullUniValue;

  if (fHelp || params.size() > 4 || params.size() == 2)
      throw runtime_error(
        "zs_listtransactions\n"
        "\nReturns an array of decrypted Zero transactions.\n"
        "\n"
        "This function only returns information on addresses with full spending keys."
        "\n"
        "\nArguments:\n"
        "1. \"Minimum Confimations:\"   (numeric, optional, default=0) \n"
        "\n"
        "2. \"Filter Type:\"            (numeric, optional, default=0) \n"
        "                               Value of 0: Returns all transactions in the wallet\n"
        "                               Value of 1: Returns the last x days of transactions\n"
        "                               Value of 2: Returns transactions with confimations less than x\n"
        "\n"
        "3. \"Filter:\"                 (numeric, optional, default=9999999) \n"
        "                               Filter Type equal 0: paramater ignored\n"
        "                               Filter Type equal 1: number represents the number of days returned\n"
        "                               Filter Type equal 2: number represents the max confirmations for transaction to be returned\n"
        "\n"
        "4. \"Count:\"                 (numeric, optional, default=9999999) \n"
        "                               Last n number of transactions returned\n"
        "\n"
        "Default Parameters:\n"
        "1. 0 - O confimations required\n"
        "2. 0 - Returns all transactions\n"
        "3. 9999999 - Ignored\n"
        "4. 9999999 - Return the last 9,999,999 transactions.\n"
        "\n"
        "\nResult:\n"
        "[{\n                                     An Array of Transactions\n"
        "   \"txid\":  \"transactionid\",           (string) The transaction id.\n"
        "   \"coinbase\": \"coinbase\",             (string) Coinbase transaction, true or false\n"
        "   \"category\": \"category\",             (string) orphan (coinbase), immature (coinbase), generate (coinbase), regular\n"
        "   \"blockhash\": \"hashvalue\",           (string) The block hash containing the transaction\n"
        "   \"blockindex\": n,                    (numeric) The block index containing the transaction\n"
        "   \"blocktime\": n,                     (numeric) The block time in seconds of the block containing the transaction, 0 for unconfirmed transactions\n"
        "   \"expiryheight\": n,                  (numeric) The expiry height of the transaction\n"
        "   \"confirmations\": n,                 (numeric) The number of confirmations for the transaction\n"
        "   \"time\": xxx,                        (numeric) The transaction time in seconds of the transaction\n"
        "   \"size\": xxx,                        (numeric) The transaction size\n"
        "   \"walletconflicts\": [conflicts],     An array of wallet conflicts\n"
        "   \"spends\": {                         A list of the spends used as inputs in the transaction\n"
        "      \"transparentSpends\": [{              An Array of utxos/notes used for transparent addresses\n"
        "          \"address\": \"zeroaddress\",          (string) Zero transparent address (t-address)\n"
        "          \"scriptPubKey\": \"script\",          (string) Script for the Zero transparent address (t-address)\n"
        "          \"amount\": x.xxxx,                  (numeric) Value of output being spent " + CURRENCY_UNIT + ", negative for spends\n"
        "          \"spendTxid\":  \"transactionid\",     (string) The transaction id of the output being spent\n"
        "          \"spendVout\"vout\": n,              (numeric) the vout value of the output being spent\n"
        "      }],\n"
        "      \"saplingSpends\": [{                  An Array of utxos/notes used for transparent addresses\n"
        "           \"address\": \"zeroaddress\",         (string) Zero sapling address (z-address)\n"
        "           \"amount\": x.xxxx,                 (numeric) Value of output being spent " + CURRENCY_UNIT + ", negative for spends\n"
        "           \"spendTxid\":  \"transactionid\",    (string) The transaction id of the output being spent\n"
        "           \"spendSheildedOutputIndex\": n,    (numeric) The index of the ShieledOutput being spent\n"
        "      }],\n"
        "      \"sproutSpends\": [{                   An Array of utxos/notes used for sprout addresses\n"
        "           \"address\": \"zeroaddress\",         (string) Zero sprout address (z-address)\n"
        "           \"amount\": x.xxxx,                 (numeric) Value of output being spent " + CURRENCY_UNIT + ", negative for spends\n"
        "           \"spendTxid\":  \"transactionid\",    (string) The transaction id of the output being spent\n"
        "           \"spendJsIndex\": n,                (numeric) Joinsplit index of the output being spent\n"
        "           \"spendJsOutIndex\": n,             (numeric) Joinsplit Output index of the output being spent\n"
        "      }],\n"
        "      \"missingSpendingKeys\": true/false    (string) True if the wallet only contains a partial set of\n"
        "                                                      key used to sign the transaction\n"
        "   },\n"
        "   \"recieved\": {                       A list of receives from the transaction\n"
        "       \"transparentReceived\": [{           An Array of txos received for transparent addresses\n"
        "           \"address\": \"zeroaddress\",         (string) Zero transparent address (t-address)\n"
        "           \"scriptPubKey\": \"script\",         (string) Script for the Zero transparent address (t-address)\n"
        "           \"amount\": x.xxxx,                 (numeric) Value of output being received " + CURRENCY_UNIT + ", positive for receives\n"
        "           \"vout\": : n,                      (numeric) the vout value\n"
        "       }],\n"
        "       \"saplingReceived\": [{               An Array of utxos/notes received for sapling addresses\n"
        "           \"address\": \"zeroaddress\",         (string) Zero sapling address (z-address)\n"
        "           \"amount\": x.xxxx,                 (numeric) Value of output being received " + CURRENCY_UNIT + ", positive for receives\n"
        "           \"sheildedOutputIndex\": n,         (numeric) The index of the ShieledOutput\n"
        "           \"change\": true/false              (string) The note is change. This can result from sending funds\n"
        "                                                        to the same address they came from, or incomplete useage\n"
        "                                                        resulting in the remainder of the note used being sent back to the\n"
        "                                                        same z-address.\n"
        "       }],\n"
        "       \"sproutReceived\": [{                An Array of utxos/notes received for sprout addresses\n"
        "           \"address\": \"zeroaddress\",         (string) Zero sprout address (z-address)\n"
        "           \"amount\": x.xxxx,                 (numeric) Value of output being received " + CURRENCY_UNIT + ", positive for receives\n"
        "           \"jsindex\": n,                     (numeric) Joinsplit index\n"
        "           \"jsoutindex\": n,                  (numeric) Joinsplit Output index\n"
        "           \"change\": true/false              (string) The note is change. This can result from sending funds\n"
        "                                                        to the same address they came from, or incomplete useage\n"
        "                                                        resulting in the remainder of the note used being sent back to the\n"
        "                                                        same z-address.\n"
        "       }],\n"
        "   },\n"
        "   \"sends\": {                          A list of outputs of where funds were sent to in the transaction,\n"
        "                                         only available if the transaction has valid sends (inputs) belonging to the wallet\n"
        "       \"transparentSends\": [{              An Array of spends (outputs) for transparent addresses of the receipient\n"
        "           \"address\": \"zeroaddress\",         (string) Zero transparent address (t-address)\n"
        "           \"scriptPubKey\": \"script\",         (string) Script for the Zero transparent address (t-address)\n"
        "           \"amount\": x.xxxx,                 (numeric) Value of output being sent " + CURRENCY_UNIT + ", negative for sends\n"
        "           \"vout\": : n,                      (numeric) the vout value\n"
        "       }],\n"
        "       \"saplingSends\": [{                 An Array of spends (outputs) for sapling addresses\n"
        "           \"address\": \"zeroaddress\",         (string) Zero sapling address (z-address) of the receipient\n"
        "           \"amount\": x.xxxx,                 (numeric) Value of output being sent" + CURRENCY_UNIT + ", negative for sends\n"
        "           \"sheildedOutputIndex\": n,         (numeric) The index of the ShieledOutput\n"
        "           \"change\": true/false              (string) The note is change. This can result from sending funds\n"
        "                                                        to the same address they came from, or incomplete useage\n"
        "                                                        resulting in the remainder of the note used being sent back to the\n"
        "                                                        same z-address.\n"
        "       }],\n"
        "       \"missingSaplingOVK\": true/false    (string) True if the sapling outputs are not decryptable\n"
        "       \"sproutSends\": [{                  An Array of spends (outputs) for sprout addresses\n"
        "           \"address\": \"zeroaddress\",         (string) Zero sprout address (z-address)\n"
        "           \"vpub_old\": x.xx                  (numeric) vpub_old\n"
        "           \"vpub_new\": x.xx                  (numeric) vpub_new\n"
        "       }],\n"
        "   }\n"
        "}]\n"
        "\nExamples:\n"
        + HelpExampleCli("zs_listtransactions", "")
        + HelpExampleCli("zs_listtransactions", "1")
        + HelpExampleCli("zs_listtransactions", "1 1 30 200")
        + HelpExampleRpc("zs_listtransactions", "")
        + HelpExampleRpc("zs_listtransactions", "1")
        + HelpExampleRpc("zs_listtransactions", "1 1 30 200")
    );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    UniValue ret(UniValue::VARR);

    //param values`
    int64_t nMinConfirms = 0;
    int64_t nFilterType = 0;
    int64_t nFilter = 9999999;
    int64_t nCount = 9999999;

    if (params.size() >= 1)
      nMinConfirms = params[0].get_int64();

    if (params.size() >= 3) {
      nFilterType = params[1].get_int64();
      nFilter = params[2].get_int64();
    }

    if (params.size() == 4) {
      nCount = params[3].get_int64();
    }

    if (nMinConfirms < 0)
      throw runtime_error("Minimum confimations must be greater that 0");

    if (nFilterType < 0 || nFilterType > 2)
        throw runtime_error("Filter type must be 0, 1 or 2.");

    if (nFilter < 0)
        throw runtime_error("Filter must be greater that 0.");

    //Created Ordered Transaction Map
    map<int64_t,CWalletTx> orderedTxs;
    for (map<uint256, CWalletTx>::iterator it = pwalletMain->mapWallet.begin(); it != pwalletMain->mapWallet.end(); ++it) {
      const CWalletTx& wtx = (*it).second;
      orderedTxs.insert(std::pair<int64_t,CWalletTx>(wtx.nOrderPos, wtx));
    }


    uint64_t t = GetTime();
    //Reverse Iterate thru transactions
    for (map<int64_t,CWalletTx>::reverse_iterator it = orderedTxs.rbegin(); it != orderedTxs.rend(); ++it)
    {
        const CWalletTx& wtx = (*it).second;

        if (!CheckFinalTx(wtx))
            continue;

        if (wtx.GetDepthInMainChain() < 0)
            continue;

        if (wtx.mapSaplingNoteData.size() == 0 && wtx.mapSproutNoteData.size() == 0 && !wtx.IsTrusted())
            continue;

        //Excude transactions with less confirmations than required
        if (wtx.GetDepthInMainChain() < nMinConfirms)
            continue;

        //Exclude Transactions older that max days old
        if (wtx.GetDepthInMainChain() > 0 && nFilterType == 1 && mapBlockIndex[wtx.hashBlock]->GetBlockTime() < (t - (nFilter * 60 * 60 * 24)))
            continue;

        //Exclude transactions with greater than max confirmations
        if (nFilterType == 2 && wtx.GetDepthInMainChain() > nFilter)
            continue;

        zsWalletTxJSON(wtx, ret, "*", false, 0);

        if (ret.size() >= nCount) break;
    }

    vector<UniValue> arrTmp = ret.getValues();

    std::reverse(arrTmp.begin(), arrTmp.end()); // Return oldest to newest

    ret.clear();
    ret.setArray();
    ret.push_backV(arrTmp);

    return ret;

}

UniValue zs_gettransaction(const UniValue& params, bool fHelp)
{
  if (!EnsureWalletIsAvailable(fHelp))
      return NullUniValue;

  if (fHelp || params.size() != 1)
      throw runtime_error(
        "zs_gettransaction\n"
        "\nReturns a decrypted Zero transaction.\n"
        "\n"
        "This function only returns information on addresses with full spending keys."
        "\n"
        "\nArguments:\n"
        "1. \"txid:\"   (string, required) \n"
        "\n"
        "\nResult:\n"
        "   \"txid\":  \"transactionid\",           (string) The transaction id.\n"
        "   \"coinbase\": \"coinbase\",             (string) Coinbase transaction, true or false\n"
        "   \"category\": \"category\",             (string) orphan (coinbase), immature (coinbase), generate (coinbase), regular\n"
        "   \"blockhash\": \"hashvalue\",           (string) The block hash containing the transaction\n"
        "   \"blockindex\": n,                    (numeric) The block index containing the transaction\n"
        "   \"blocktime\": n,                     (numeric) The block time in seconds of the block containing the transaction, 0 for unconfirmed transactions\n"
        "   \"expiryheight\": n,                  (numeric) The expiry height of the transaction\n"
        "   \"confirmations\": n,                 (numeric) The number of confirmations for the transaction\n"
        "   \"time\": xxx,                        (numeric) The transaction time in seconds of the transaction\n"
        "   \"size\": xxx,                        (numeric) The transaction size\n"
        "   \"walletconflicts\": [conflicts],     An array of wallet conflicts\n"
        "   \"spends\": {                         A list of the spends used as inputs in the transaction\n"
        "      \"transparentSpends\": [{              An Array of utxos/notes used for transparent addresses\n"
        "          \"address\": \"zeroaddress\",          (string) Zero transparent address (t-address)\n"
        "          \"scriptPubKey\": \"script\",          (string) Script for the Zero transparent address (t-address)\n"
        "          \"amount\": x.xxxx,                  (numeric) Value of output being spent " + CURRENCY_UNIT + ", negative for spends\n"
        "          \"spendTxid\":  \"transactionid\",     (string) The transaction id of the output being spent\n"
        "          \"spendVout\"vout\": n,              (numeric) the vout value of the output being spent\n"
        "      }],\n"
        "      \"saplingSpends\": [{                  An Array of utxos/notes used for transparent addresses\n"
        "           \"address\": \"zeroaddress\",         (string) Zero sapling address (z-address)\n"
        "           \"amount\": x.xxxx,                 (numeric) Value of output being spent " + CURRENCY_UNIT + ", negative for spends\n"
        "           \"spendTxid\":  \"transactionid\",    (string) The transaction id of the output being spent\n"
        "           \"spendSheildedOutputIndex\": n,    (numeric) The index of the ShieledOutput being spent\n"
        "      }],\n"
        "      \"sproutSpends\": [{                   An Array of utxos/notes used for sprout addresses\n"
        "           \"address\": \"zeroaddress\",         (string) Zero sprout address (z-address)\n"
        "           \"amount\": x.xxxx,                 (numeric) Value of output being spent " + CURRENCY_UNIT + ", negative for spends\n"
        "           \"spendTxid\":  \"transactionid\",    (string) The transaction id of the output being spent\n"
        "           \"spendJsIndex\": n,                (numeric) Joinsplit index of the output being spent\n"
        "           \"spendJsOutIndex\": n,             (numeric) Joinsplit Output index of the output being spent\n"
        "      }],\n"
        "      \"missingSpendingKeys\": true/false    (string) True if the wallet only contains a partial set of\n"
        "                                                      key used to sign the transaction\n"
        "   },\n"
        "   \"recieved\": {                       A list of receives from the transaction\n"
        "       \"transparentReceived\": [{           An Array of txos received for transparent addresses\n"
        "           \"address\": \"zeroaddress\",         (string) Zero transparent address (t-address)\n"
        "           \"scriptPubKey\": \"script\",         (string) Script for the Zero transparent address (t-address)\n"
        "           \"amount\": x.xxxx,                 (numeric) Value of output being received " + CURRENCY_UNIT + ", positive for receives\n"
        "           \"vout\": : n,                      (numeric) the vout value\n"
        "       }],\n"
        "       \"saplingReceived\": [{               An Array of utxos/notes received for sapling addresses\n"
        "           \"address\": \"zeroaddress\",         (string) Zero sapling address (z-address)\n"
        "           \"amount\": x.xxxx,                 (numeric) Value of output being received " + CURRENCY_UNIT + ", positive for receives\n"
        "           \"sheildedOutputIndex\": n,         (numeric) The index of the ShieledOutput\n"
        "           \"change\": true/false              (string) The note is change. This can result from sending funds\n"
        "                                                        to the same address they came from, or incomplete useage\n"
        "                                                        resulting in the remainder of the note used being sent back to the\n"
        "                                                        same z-address.\n"
        "       }],\n"
        "       \"sproutReceived\": [{                An Array of utxos/notes received for sprout addresses\n"
        "           \"address\": \"zeroaddress\",         (string) Zero sprout address (z-address)\n"
        "           \"amount\": x.xxxx,                 (numeric) Value of output being received " + CURRENCY_UNIT + ", positive for receives\n"
        "           \"jsindex\": n,                     (numeric) Joinsplit index\n"
        "           \"jsoutindex\": n,                  (numeric) Joinsplit Output index\n"
        "           \"change\": true/false              (string) The note is change. This can result from sending funds\n"
        "                                                        to the same address they came from, or incomplete useage\n"
        "                                                        resulting in the remainder of the note used being sent back to the\n"
        "                                                        same z-address.\n"
        "       }],\n"
        "   },\n"
        "   \"sends\": {                          A list of outputs of where funds were sent to in the transaction,\n"
        "                                         only available if the transaction has valid sends (inputs) belonging to the wallet\n"
        "       \"transparentSends\": [{              An Array of spends (outputs) for transparent addresses of the receipient\n"
        "           \"address\": \"zeroaddress\",         (string) Zero transparent address (t-address)\n"
        "           \"scriptPubKey\": \"script\",         (string) Script for the Zero transparent address (t-address)\n"
        "           \"amount\": x.xxxx,                 (numeric) Value of output being sent " + CURRENCY_UNIT + ", negative for sends\n"
        "           \"vout\": : n,                      (numeric) the vout value\n"
        "       }],\n"
        "       \"saplingSends\": [{                 An Array of spends (outputs) for sapling addresses\n"
        "           \"address\": \"zeroaddress\",         (string) Zero sapling address (z-address) of the receipient\n"
        "           \"amount\": x.xxxx,                 (numeric) Value of output being sent" + CURRENCY_UNIT + ", negative for sends\n"
        "           \"sheildedOutputIndex\": n,         (numeric) The index of the ShieledOutput\n"
        "           \"change\": true/false              (string) The note is change. This can result from sending funds\n"
        "                                                        to the same address they came from, or incomplete useage\n"
        "                                                        resulting in the remainder of the note used being sent back to the\n"
        "                                                        same z-address.\n"
        "       }],\n"
        "       \"missingSaplingOVK\": true/false    (string) True if the sapling outputs are not decryptable\n"
        "       \"sproutSends\": [{                  An Array of spends (outputs) for sprout addresses\n"
        "           \"address\": \"zeroaddress\",         (string) Zero sprout address (z-address)\n"
        "           \"vpub_old\": x.xx                  (numeric) vpub_old\n"
        "           \"vpub_new\": x.xx                  (numeric) vpub_new\n"
        "       }],\n"
        "   }\n"
        "\nExamples:\n"
        + HelpExampleCli("zs_gettransaction", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\"")
        + HelpExampleRpc("zs_gettransaction", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\"")
    );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    uint256 hash;
    hash.SetHex(params[0].get_str());

    UniValue ret(UniValue::VARR);
    if (!pwalletMain->mapWallet.count(hash))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid or non-wallet transaction id");

    const CWalletTx& wtx = pwalletMain->mapWallet[hash];
    zsWalletTxJSON(wtx, ret, "*", false, 0);

    return ret;
}

UniValue zs_listspentbyaddress(const UniValue& params, bool fHelp) {
  if (!EnsureWalletIsAvailable(fHelp))
      return NullUniValue;

  if (fHelp || params.size() > 5 || params.size() == 3)
      throw runtime_error(
        "zs_listspentbyaddress\n"
        "\nReturns decrypted Zero spent inputs for a single address.\n"
        "\n"
        "This function only returns information on addresses with full spending keys."
        "\n"
        "\nArguments:\n"
        "1. \"zeroaddress:\"            (string, required) \n"
        "\n"
        "2. \"Minimum Confimations:\"   (numeric, optional, default=0) \n"
        "\n"
        "3. \"Filter Type:\"            (numeric, optional, default=0) \n"
        "                               Value of 0: Returns all transactions in the wallet\n"
        "                               Value of 1: Returns the last x days of transactions\n"
        "                               Value of 2: Returns transactions with confimations less than x\n"
        "\n"
        "4. \"Filter:\"                 (numeric, optional, default=999999) \n"
        "                               Filter Type equal 0: paramater ignored\n"
        "                               Filter Type equal 1: number represents the number of days returned\n"
        "                               Filter Type equal 2: number represents the max confirmations for transaction to be returned\n"
        "\n"
        "5. \"Count:\"                 (numeric, optional, default=9999999) \n"
        "                               Last n number of transactions returned\n"
        "\n"
        "Default Parameters:\n"
        "1. Zero Address\n"
        "2. 0 - O confimations required\n"
        "3. 0 - Returns all transactions\n"
        "4. 9999999 - Ignored\n"
        "5. 9999999 - Return the last 9,999,999 transactions.\n"
        "\n"
        "\nResult:\n"
        "   \"txid\":  \"transactionid\",           (string) The transaction id.\n"
        "   \"coinbase\": \"coinbase\",             (string) Coinbase transaction, true or false\n"
        "   \"category\": \"category\",             (string) orphan (coinbase), immature (coinbase), generate (coinbase), regular\n"
        "   \"blockhash\": \"hashvalue\",           (string) The block hash containing the transaction\n"
        "   \"blockindex\": n,                    (numeric) The block index containing the transaction\n"
        "   \"blocktime\": n,                     (numeric) The block time in seconds of the block containing the transaction, 0 for unconfirmed transactions\n"
        "   \"expiryheight\": n,                  (numeric) The expiry height of the transaction\n"
        "   \"confirmations\": n,                 (numeric) The number of confirmations for the transaction\n"
        "   \"time\": xxx,                        (numeric) The transaction time in seconds of the transaction\n"
        "   \"size\": xxx,                        (numeric) The transaction size\n"
        "   \"walletconflicts\": [conflicts],     An array of wallet conflicts\n"
        "   \"spends\": {                         A list of the spends used as inputs in the transaction\n"
        "      \"transparentSpends\": [{              An Array of utxos/notes used for transparent addresses\n"
        "          \"address\": \"zeroaddress\",          (string) Zero transparent address (t-address)\n"
        "          \"scriptPubKey\": \"script\",          (string) Script for the Zero transparent address (t-address)\n"
        "          \"amount\": x.xxxx,                  (numeric) Value of output being spent " + CURRENCY_UNIT + ", negative for spends\n"
        "          \"spendTxid\":  \"transactionid\",     (string) The transaction id of the output being spent\n"
        "          \"spendVout\"vout\": n,              (numeric) the vout value of the output being spent\n"
        "      }],\n"
        "      \"saplingSpends\": [{                  An Array of utxos/notes used for transparent addresses\n"
        "           \"address\": \"zeroaddress\",         (string) Zero sapling address (z-address)\n"
        "           \"amount\": x.xxxx,                 (numeric) Value of output being spent " + CURRENCY_UNIT + ", negative for spends\n"
        "           \"spendTxid\":  \"transactionid\",    (string) The transaction id of the output being spent\n"
        "           \"spendSheildedOutputIndex\": n,    (numeric) The index of the ShieledOutput being spent\n"
        "      }],\n"
        "      \"sproutSpends\": [{                   An Array of utxos/notes used for sprout addresses\n"
        "           \"address\": \"zeroaddress\",         (string) Zero sprout address (z-address)\n"
        "           \"amount\": x.xxxx,                 (numeric) Value of output being spent " + CURRENCY_UNIT + ", negative for spends\n"
        "           \"spendTxid\":  \"transactionid\",    (string) The transaction id of the output being spent\n"
        "           \"spendJsIndex\": n,                (numeric) Joinsplit index of the output being spent\n"
        "           \"spendJsOutIndex\": n,             (numeric) Joinsplit Output index of the output being spent\n"
        "      }],\n"
        "      \"missingSpendingKeys\": true/false    (string) True if the wallet only contains a partial set of\n"
        "                                                      key used to sign the transaction\n"
        "   },\n"
        "\nExamples:\n"
        + HelpExampleCli("zs_listspentbyaddress", "t1KzZ5n2TPEGYXTZ3WYGL1AYEumEQaRoHaL")
        + HelpExampleRpc("zs_listspentbyaddress", "t1KzZ5n2TPEGYXTZ3WYGL1AYEumEQaRoHaL")
    );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    UniValue ret(UniValue::VARR);

    //param values`
    int64_t nMinConfirms = 0;
    int64_t nFilterType = 0;
    int64_t nFilter = 9999999;
    int64_t nCount = 9999999;

    if (params.size() >= 2)
      nMinConfirms = params[1].get_int64();

    if (params.size() >= 4) {
      nFilterType = params[2].get_int64();
      nFilter = params[3].get_int64();
    }

    if (params.size() == 5) {
      nCount = params[4].get_int64();
    }

    if (nMinConfirms < 0)
      throw runtime_error("Minimum confimations must be greater that 0");

    if (nFilterType < 0 || nFilterType > 2)
        throw runtime_error("Filter type must be 0, 1 or 2.");

    if (nFilter < 0)
        throw runtime_error("Filter must be greater that 0.");

      //Created Ordered Transaction Map
      map<int64_t,CWalletTx> orderedTxs;
      for (map<uint256, CWalletTx>::iterator it = pwalletMain->mapWallet.begin(); it != pwalletMain->mapWallet.end(); ++it) {
        const CWalletTx& wtx = (*it).second;
        orderedTxs.insert(std::pair<int64_t,CWalletTx>(wtx.nOrderPos, wtx));
      }


      uint64_t t = GetTime();
      //Reverse Iterate thru transactions
      for (map<int64_t,CWalletTx>::reverse_iterator it = orderedTxs.rbegin(); it != orderedTxs.rend(); ++it) {
        const CWalletTx& wtx = (*it).second;

        if (!CheckFinalTx(wtx))
            continue;

        if (wtx.GetDepthInMainChain() < 0)
            continue;

        if (wtx.mapSaplingNoteData.size() == 0 && wtx.mapSproutNoteData.size() == 0 && !wtx.IsTrusted())
            continue;

        //Excude transactions with less confirmations than required
        if (wtx.GetDepthInMainChain() < nMinConfirms)
            continue;

        //Exclude Transactions older that max days old
        if (wtx.GetDepthInMainChain() > 0 && nFilterType == 1 && mapBlockIndex[wtx.hashBlock]->GetBlockTime() < (t - (nFilter * 60 * 60 * 24)))
            continue;

        //Exclude transactions with greater than max confirmations
        if (nFilterType == 2 && wtx.GetDepthInMainChain() > nFilter)
            continue;

        zsWalletTxJSON(wtx, ret, params[0].get_str() , true, 1);

        if (ret.size() >= nCount) break;
    }

    vector<UniValue> arrTmp = ret.getValues();

    std::reverse(arrTmp.begin(), arrTmp.end()); // Return oldest to newest

    ret.clear();
    ret.setArray();
    ret.push_backV(arrTmp);

    return ret;
}

UniValue zs_listreceivedbyaddress(const UniValue& params, bool fHelp) {
  if (!EnsureWalletIsAvailable(fHelp))
      return NullUniValue;

  if (fHelp || params.size() > 5 || params.size() == 3)
      throw runtime_error(
        "zs_listreceivedbyaddress\n"
        "\nReturns decrypted Zero received outputs for a single address.\n"
        "\n"
        "This function only returns information on addresses with full spending keys."
        "\n"
        "\nArguments:\n"
        "1. \"zeroaddress:\"            (string, required) \n"
        "\n"
        "2. \"Minimum Confimations:\"   (numeric, optional, default=0) \n"
        "\n"
        "3. \"Filter Type:\"            (numeric, optional, default=0) \n"
        "                               Value of 0: Returns all transactions in the wallet\n"
        "                               Value of 1: Returns the last x days of transactions\n"
        "                               Value of 2: Returns transactions with confimations less than x\n"
        "\n"
        "4. \"Filter:\"                 (numeric, optional, default=999999) \n"
        "                               Filter Type equal 0: paramater ignored\n"
        "                               Filter Type equal 1: number represents the number of days returned\n"
        "                               Filter Type equal 2: number represents the max confirmations for transaction to be returned\n"
        "\n"
        "5. \"Count:\"                 (numeric, optional, default=9999999) \n"
        "                               Last n number of transactions returned\n"
        "\n"
        "Default Parameters:\n"
        "2. 0 - O confimations required\n"
        "3. 0 - Returns all transactions\n"
        "4. 9999999 - Ignored\n"
        "5. 9999999 - Return the last 9,999,999 transactions.\n"
        "\n"
        "\nResult:\n"
        "   \"txid\":  \"transactionid\",           (string) The transaction id.\n"
        "   \"coinbase\": \"coinbase\",             (string) Coinbase transaction, true or false\n"
        "   \"category\": \"category\",             (string) orphan (coinbase), immature (coinbase), generate (coinbase), regular\n"
        "   \"blockhash\": \"hashvalue\",           (string) The block hash containing the transaction\n"
        "   \"blockindex\": n,                    (numeric) The block index containing the transaction\n"
        "   \"blocktime\": n,                     (numeric) The block time in seconds of the block containing the transaction, 0 for unconfirmed transactions\n"
        "   \"expiryheight\": n,                  (numeric) The expiry height of the transaction\n"
        "   \"confirmations\": n,                 (numeric) The number of confirmations for the transaction\n"
        "   \"time\": xxx,                        (numeric) The transaction time in seconds of the transaction\n"
        "   \"size\": xxx,                        (numeric) The transaction size\n"
        "   \"walletconflicts\": [conflicts],     An array of wallet conflicts\n"
        "   \"recieved\": {                       A list of receives from the transaction\n"
        "       \"transparentReceived\": [{           An Array of txos received for transparent addresses\n"
        "           \"address\": \"zeroaddress\",         (string) Zero transparent address (t-address)\n"
        "           \"scriptPubKey\": \"script\",         (string) Script for the Zero transparent address (t-address)\n"
        "           \"amount\": x.xxxx,                 (numeric) Value of output being received " + CURRENCY_UNIT + ", positive for receives\n"
        "           \"vout\": : n,                      (numeric) the vout value\n"
        "       }],\n"
        "       \"saplingReceived\": [{               An Array of utxos/notes received for sapling addresses\n"
        "           \"address\": \"zeroaddress\",         (string) Zero sapling address (z-address)\n"
        "           \"amount\": x.xxxx,                 (numeric) Value of output being received " + CURRENCY_UNIT + ", positive for receives\n"
        "           \"sheildedOutputIndex\": n,         (numeric) The index of the ShieledOutput\n"
        "           \"change\": true/false              (string) The note is change. This can result from sending funds\n"
        "                                                        to the same address they came from, or incomplete useage\n"
        "                                                        resulting in the remainder of the note used being sent back to the\n"
        "                                                        same z-address.\n"
        "       }],\n"
        "       \"sproutReceived\": [{                An Array of utxos/notes received for sprout addresses\n"
        "           \"address\": \"zeroaddress\",         (string) Zero sprout address (z-address)\n"
        "           \"amount\": x.xxxx,                 (numeric) Value of output being received " + CURRENCY_UNIT + ", positive for receives\n"
        "           \"jsindex\": n,                     (numeric) Joinsplit index\n"
        "           \"jsoutindex\": n,                  (numeric) Joinsplit Output index\n"
        "           \"change\": true/false              (string) The note is change. This can result from sending funds\n"
        "                                                        to the same address they came from, or incomplete useage\n"
        "                                                        resulting in the remainder of the note used being sent back to the\n"
        "                                                        same z-address.\n"
        "       }],\n"
        "   },\n"
        "\nExamples:\n"
        + HelpExampleCli("zs_listreceivedbyaddress", "t1KzZ5n2TPEGYXTZ3WYGL1AYEumEQaRoHaL")
        + HelpExampleRpc("zs_listreceivedbyaddress", "t1KzZ5n2TPEGYXTZ3WYGL1AYEumEQaRoHaL")
    );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    UniValue ret(UniValue::VARR);

    //param values`
    int64_t nMinConfirms = 0;
    int64_t nFilterType = 0;
    int64_t nFilter = 9999999;
    int64_t nCount = 9999999;

    if (params.size() >= 2)
      nMinConfirms = params[1].get_int64();

    if (params.size() >= 4) {
      nFilterType = params[2].get_int64();
      nFilter = params[3].get_int64();
    }

    if (params.size() == 5) {
      nCount = params[4].get_int64();
    }

    if (nMinConfirms < 0)
      throw runtime_error("Minimum confimations must be greater that 0");

    if (nFilterType < 0 || nFilterType > 2)
        throw runtime_error("Filter type must be 0, 1 or 2.");

    if (nFilter < 0)
        throw runtime_error("Filter must be greater that 0.");

    //Created Ordered Transaction Map
    map<int64_t,CWalletTx> orderedTxs;
    for (map<uint256, CWalletTx>::iterator it = pwalletMain->mapWallet.begin(); it != pwalletMain->mapWallet.end(); ++it) {
      const CWalletTx& wtx = (*it).second;
      orderedTxs.insert(std::pair<int64_t,CWalletTx>(wtx.nOrderPos, wtx));
    }


    uint64_t t = GetTime();
    //Reverse Iterate thru transactions
    for (map<int64_t,CWalletTx>::reverse_iterator it = orderedTxs.rbegin(); it != orderedTxs.rend(); ++it) {
      const CWalletTx& wtx = (*it).second;

      if (!CheckFinalTx(wtx))
          continue;

      if (wtx.GetDepthInMainChain() < 0)
          continue;

      if (wtx.mapSaplingNoteData.size() == 0 && wtx.mapSproutNoteData.size() == 0 && !wtx.IsTrusted())
          continue;

      //Excude transactions with less confirmations than required
      if (wtx.GetDepthInMainChain() < nMinConfirms)
          continue;

      //Exclude Transactions older that max days old
      if (wtx.GetDepthInMainChain() > 0 && nFilterType == 1 && mapBlockIndex[wtx.hashBlock]->GetBlockTime() < (t - (nFilter * 60 * 60 * 24)))
          continue;

      //Exclude transactions with greater than max confirmations
      if (nFilterType == 2 && wtx.GetDepthInMainChain() > nFilter)
          continue;

        zsWalletTxJSON(wtx, ret, params[0].get_str() , true, 2);

        if (ret.size() >= nCount) break;
    }

    vector<UniValue> arrTmp = ret.getValues();

    std::reverse(arrTmp.begin(), arrTmp.end()); // Return oldest to newest

    ret.clear();
    ret.setArray();
    ret.push_backV(arrTmp);

    return ret;
}

UniValue zs_listsentbyaddress(const UniValue& params, bool fHelp) {
  if (!EnsureWalletIsAvailable(fHelp))
      return NullUniValue;

  if (fHelp || params.size() > 5 || params.size() == 3)
      throw runtime_error(
        "zs_listsentbyaddress\n"
        "\nReturns decrypted Zero outputs sent to a single address.\n"
        "\n"
        "This function only returns information on addresses sent from wallet addresses with full spending keys."
        "\n"
        "\nArguments:\n"
        "1. \"zeroaddress:\"            (string, required) \n"
        "\n"
        "2. \"Minimum Confimations:\"   (numeric, optional, default=0) \n"
        "\n"
        "3. \"Filter Type:\"            (numeric, optional, default=0) \n"
        "                               Value of 0: Returns all transactions in the wallet\n"
        "                               Value of 1: Returns the last x days of transactions\n"
        "                               Value of 2: Returns transactions with confimations less than x\n"
        "\n"
        "4. \"Filter:\"                 (numeric, optional, default=999999) \n"
        "                               Filter Type equal 0: paramater ignored\n"
        "                               Filter Type equal 1: number represents the number of days returned\n"
        "                               Filter Type equal 2: number represents the max confirmations for transaction to be returned\n"
        "\n"
        "5. \"Count:\"                 (numeric, optional, default=9999999) \n"
        "                               Last n number of transactions returned\n"
        "\n"
        "Default Parameters:\n"
        "2. 0 - O confimations required\n"
        "3. 0 - Returns all transactions\n"
        "4. 9999999 - Ignored\n"
        "5. 9999999 - Return the last 9,999,999 transactions.\n"
        "\n"
        "\nResult:\n"
        "   \"txid\":  \"transactionid\",           (string) The transaction id.\n"
        "   \"coinbase\": \"coinbase\",             (string) Coinbase transaction, true or false\n"
        "   \"category\": \"category\",             (string) orphan (coinbase), immature (coinbase), generate (coinbase), regular\n"
        "   \"blockhash\": \"hashvalue\",           (string) The block hash containing the transaction\n"
        "   \"blockindex\": n,                    (numeric) The block index containing the transaction\n"
        "   \"blocktime\": n,                     (numeric) The block time in seconds of the block containing the transaction, 0 for unconfirmed transactions\n"
        "   \"expiryheight\": n,                  (numeric) The expiry height of the transaction\n"
        "   \"confirmations\": n,                 (numeric) The number of confirmations for the transaction\n"
        "   \"time\": xxx,                        (numeric) The transaction time in seconds of the transaction\n"
        "   \"size\": xxx,                        (numeric) The transaction size\n"
        "   \"walletconflicts\": [conflicts],     An array of wallet conflicts\n"
        "   \"sends\": {                          A list of outputs of where funds were sent to in the transaction,\n"
        "                                         only available if the transaction has valid sends (inputs) belonging to the wallet\n"
        "       \"transparentSends\": [{              An Array of spends (outputs) for transparent addresses of the receipient\n"
        "           \"address\": \"zeroaddress\",         (string) Zero transparent address (t-address)\n"
        "           \"scriptPubKey\": \"script\",         (string) Script for the Zero transparent address (t-address)\n"
        "           \"amount\": x.xxxx,                 (numeric) Value of output being sent " + CURRENCY_UNIT + ", negative for sends\n"
        "           \"vout\": : n,                      (numeric) the vout value\n"
        "       }],\n"
        "       \"saplingSends\": [{                 An Array of spends (outputs) for sapling addresses\n"
        "           \"address\": \"zeroaddress\",         (string) Zero sapling address (z-address) of the receipient\n"
        "           \"amount\": x.xxxx,                 (numeric) Value of output being sent" + CURRENCY_UNIT + ", negative for sends\n"
        "           \"sheildedOutputIndex\": n,         (numeric) The index of the ShieledOutput\n"
        "           \"change\": true/false              (string) The note is change. This can result from sending funds\n"
        "                                                        to the same address they came from, or incomplete useage\n"
        "                                                        resulting in the remainder of the note used being sent back to the\n"
        "                                                        same z-address.\n"
        "       }],\n"
        "       \"missingSaplingOVK\": true/false    (string) True if the sapling outputs are not decryptable\n"
        "       \"sproutSends\": [{                  An Array of spends (outputs) for sprout addresses\n"
        "           \"address\": \"zeroaddress\",         (string) Zero sprout address (z-address)\n"
        "           \"vpub_old\": x.xx                  (numeric) vpub_old\n"
        "           \"vpub_new\": x.xx                  (numeric) vpub_new\n"
        "       }],\n"
        "   }\n"
        "\nExamples:\n"
        + HelpExampleCli("zs_listsentbyaddress", "t1KzZ5n2TPEGYXTZ3WYGL1AYEumEQaRoHaL")
        + HelpExampleRpc("zs_listsentbyaddress", "t1KzZ5n2TPEGYXTZ3WYGL1AYEumEQaRoHaL")
    );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    UniValue ret(UniValue::VARR);

    //param values`
    int64_t nMinConfirms = 0;
    int64_t nFilterType = 0;
    int64_t nFilter = 9999999;
    int64_t nCount = 9999999;

    if (params.size() >= 2)
      nMinConfirms = params[1].get_int64();

    if (params.size() >= 4) {
      nFilterType = params[2].get_int64();
      nFilter = params[3].get_int64();
    }

    if (params.size() == 5) {
      nCount = params[4].get_int64();
    }

    if (nMinConfirms < 0)
      throw runtime_error("Minimum confimations must be greater that 0");

    if (nFilterType < 0 || nFilterType > 2)
        throw runtime_error("Filter type must be 0, 1 or 2.");

    if (nFilter < 0)
        throw runtime_error("Filter must be greater that 0.");

    //Created Ordered Transaction Map
    map<int64_t,CWalletTx> orderedTxs;
    for (map<uint256, CWalletTx>::iterator it = pwalletMain->mapWallet.begin(); it != pwalletMain->mapWallet.end(); ++it) {
      const CWalletTx& wtx = (*it).second;
      orderedTxs.insert(std::pair<int64_t,CWalletTx>(wtx.nOrderPos, wtx));
    }


    uint64_t t = GetTime();
    //Reverse Iterate thru transactions
    for (map<int64_t,CWalletTx>::reverse_iterator it = orderedTxs.rbegin(); it != orderedTxs.rend(); ++it) {
      const CWalletTx& wtx = (*it).second;

      if (!CheckFinalTx(wtx))
          continue;

      if (wtx.GetDepthInMainChain() < 0)
          continue;

      if (wtx.mapSaplingNoteData.size() == 0 && wtx.mapSproutNoteData.size() == 0 && !wtx.IsTrusted())
          continue;

      //Excude transactions with less confirmations than required
      if (wtx.GetDepthInMainChain() < nMinConfirms)
          continue;

      //Exclude Transactions older that max days old
      if (wtx.GetDepthInMainChain() > 0 && nFilterType == 1 && mapBlockIndex[wtx.hashBlock]->GetBlockTime() < (t - (nFilter * 60 * 60 * 24)))
          continue;

      //Exclude transactions with greater than max confirmations
      if (nFilterType == 2 && wtx.GetDepthInMainChain() > nFilter)
          continue;

      zsWalletTxJSON(wtx, ret, params[0].get_str() , true, 3);


      if (ret.size() >= nCount) break;
    }

    vector<UniValue> arrTmp = ret.getValues();

    std::reverse(arrTmp.begin(), arrTmp.end()); // Return oldest to newest

    ret.clear();
    ret.setArray();
    ret.push_backV(arrTmp);

    return ret;
}



/**
 *Return current blockchain status, wallet balance, address balance and the last 200 transactions
**/
UniValue getalldata(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 3)
        throw runtime_error(
            "getalldata \"datatype transactiontype \"\n"
            "\n"
            "This function only returns information on wallet addresses with full spending keys."
            "\n"
            "\nArguments:\n"
            "1. \"datatype\"     (integer, required) \n"
            "                    Value of 0: Return address, balance, transactions and blockchain info\n"
            "                    Value of 1: Return address, balance, blockchain info\n"
            "                    Value of 2: Return transactions and blockchain info\n"
            "2. \"transactiontype\"     (integer, optional) \n"
            "                    Value of 1: Return all transactions in the last 24 hours\n"
            "                    Value of 2: Return all transactions in the last 7 days\n"
            "                    Value of 3: Return all transactions in the last 30 days\n"
            "                    Other number: Return all transactions in the last 24 hours\n"
            "3. \"transactioncount\"     (integer, optional) \n"
            "\nResult:\n"
            "\nExamples:\n"
            + HelpExampleCli("getalldata", "0")
            + HelpExampleRpc("getalldata", "0")
        );

    LOCK(cs_main);

    UniValue returnObj(UniValue::VOBJ);
    int connectionCount = 0;
    {
        LOCK2(cs_main, cs_vNodes);
        connectionCount = (int)vNodes.size();
    }

    LOCK2(cs_main, pwalletMain->cs_wallet);
    int nMinDepth = 1;

    CAmount confirmed = 0;
    CAmount unconfirmed = 0;
    CAmount locked = 0;
    CAmount immature = 0;

    CAmount privateConfirmed = 0;
    CAmount privateUnconfirmed = 0;
    CAmount privateLocked = 0;
    CAmount privateImmature = 0;

    balancestruct txAmounts;
    txAmounts.confirmed = 0;
    txAmounts.unconfirmed = 0;
    txAmounts.locked = 0;
    txAmounts.immature = 0;


    //Create map of addresses
    //Add all Transaparent addresses to list
    map<string, balancestruct> addressBalances;
    BOOST_FOREACH(const PAIRTYPE(CTxDestination, CAddressBookData)& item, pwalletMain->mapAddressBook)
    {
        string addressString = EncodeDestination(item.first);
        if (addressBalances.count(addressString) == 0)
          addressBalances.insert(make_pair(addressString,txAmounts));
    }

    //Add all Sapling addresses to map
    std::set<libzcash::SaplingPaymentAddress> zs_addresses;
    pwalletMain->GetSaplingPaymentAddresses(zs_addresses);
    for (auto addr : zs_addresses) {
      string addressString = EncodePaymentAddress(addr);
      if (addressBalances.count(addressString) == 0)
        addressBalances.insert(make_pair(addressString,txAmounts));
    }

    //Add all Sprout addresses to map
    std::set<libzcash::SproutPaymentAddress> zc_addresses;
    pwalletMain->GetSproutPaymentAddresses(zc_addresses);
    for (auto addr : zc_addresses) {
      string addressString = EncodePaymentAddress(addr);
      if (addressBalances.count(addressString) == 0)
        addressBalances.insert(make_pair(addressString,txAmounts));
    }


    //Create Ordered List
    map<int64_t,CWalletTx> orderedTxs;
    for (map<uint256, CWalletTx>::iterator it = pwalletMain->mapWallet.begin(); it != pwalletMain->mapWallet.end(); ++it) {
      const uint256& wtxid = it->first;
      const CWalletTx& wtx = (*it).second;
      orderedTxs.insert(std::pair<int64_t,CWalletTx>(wtx.nOrderPos, wtx));

      unsigned int txType = 0;
      // 0 Unassigend
      // 1 Immature
      // 2 Unconfirmed
      // 3 Locked

      if (!CheckFinalTx(wtx))
          continue;

      if (wtx.GetDepthInMainChain() < 0)
          continue;

      if (wtx.mapSaplingNoteData.size() == 0 && wtx.mapSproutNoteData.size() == 0 && !wtx.IsTrusted())
          continue;

      //Assign Immature
      if (txType == 0 && wtx.IsCoinBase() && wtx.GetBlocksToMaturity() > 0)
        txType = 1;

      //Assign Unconfirmed
      if (txType == 0 && wtx.GetDepthInMainChain() == 0)
        txType = 2;

      for (unsigned int i = 0; i < wtx.vout.size(); i++) {

        CTxDestination address;
        if (!ExtractDestination(wtx.vout[i].scriptPubKey, address))
          continue;

        //excluded coins that we dont have the spending keys for
        isminetype mine = IsMine(*pwalletMain,address);
        if (mine != ISMINE_SPENDABLE)
          continue;

        //Exclude spent coins
        if (pwalletMain->IsSpent(wtxid, i))
          continue;

        //Assign locked
        if (txType == 0 && pwalletMain->IsLockedCoin((*it).first, i))
          txType = 3;

        //Assign Locked to 10000 Zer inputs for Zeronodes
        if (txType == 0 && fZeroNode && wtx.vout[i].nValue == 10000 * COIN)
          txType = 3;

        string addressString = EncodeDestination(address);
        if (addressBalances.count(addressString) == 0)
          addressBalances.insert(make_pair(addressString,txAmounts));

        if (txType == 0) {
          addressBalances.at(addressString).confirmed += wtx.vout[i].nValue;
          confirmed += wtx.vout[i].nValue;
        } else if (txType == 1) {
          addressBalances.at(addressString).immature+= wtx.vout[i].nValue;
          immature += wtx.vout[i].nValue;
        } else if (txType == 2) {
          addressBalances.at(addressString).unconfirmed += wtx.vout[i].nValue;
          unconfirmed += wtx.vout[i].nValue;
        } else if (txType == 3) {
          addressBalances.at(addressString).locked += wtx.vout[i].nValue;
          locked += wtx.vout[i].nValue;
        }
      }

      for (auto & pair : wtx.mapSaplingNoteData) {
        SaplingOutPoint op = pair.first;
        SaplingNoteData nd = pair.second;

        //Skip Spent
        if (nd.nullifier && pwalletMain->IsSaplingSpent(*nd.nullifier))
            continue;

        //Decrypt sapling incoming commitments using IVK
        for (auto addr : zs_addresses) {
          libzcash::SaplingExtendedSpendingKey extsk;
          if (pwalletMain->GetSaplingExtendedSpendingKey(addr, extsk)) {
            auto pt = libzcash::SaplingNotePlaintext::decrypt(
              wtx.vShieldedOutput[op.n].encCiphertext,extsk.expsk.full_viewing_key().in_viewing_key(),wtx.vShieldedOutput[op.n].ephemeralKey,wtx.vShieldedOutput[op.n].cm);

            if (txType == 0 && pwalletMain->IsLockedNote(op))
                txType == 3;

            if (pt) {
              auto note = pt.get();
              string addressString = EncodePaymentAddress(addr);
              if (addressBalances.count(addressString) == 0)
                addressBalances.insert(make_pair(addressString,txAmounts));

              if (txType == 0) {
                addressBalances.at(addressString).confirmed += note.value();
                privateConfirmed += note.value();
              } else if (txType == 1) {
                addressBalances.at(addressString).immature += note.value();
                privateImmature += note.value();
              } else if (txType == 2) {
                addressBalances.at(addressString).unconfirmed += note.value();
                privateUnconfirmed += note.value();
              } else if (txType == 3) {
                addressBalances.at(addressString).locked += note.value();
                privateLocked += note.value();
              }

              continue;

            }
          }
        }
      }

      for (auto & pair : wtx.mapSproutNoteData) {
        JSOutPoint jsop = pair.first;
        SproutNoteData nd = pair.second;
        libzcash::SproutPaymentAddress pa = nd.address;

        int i = jsop.js; // Index into CTransaction.vJoinSplit
        int j = jsop.n; // Index into JSDescription.ciphertexts

        //Skip Spent
        if (nd.nullifier && pwalletMain->IsSproutSpent(*nd.nullifier))
            continue;

        for (auto addr : zc_addresses) {
          try {
            libzcash::SproutSpendingKey sk;
            pwalletMain->GetSproutSpendingKey(addr, sk);
            ZCNoteDecryption decryptor(sk.receiving_key());

            // determine amount of funds in the note
            auto hSig = wtx.vJoinSplit[i].h_sig(*pzcashParams, wtx.joinSplitPubKey);

            SproutNotePlaintext pt = libzcash::SproutNotePlaintext::decrypt(decryptor,wtx.vJoinSplit[i].ciphertexts[j],wtx.vJoinSplit[i].ephemeralKey,hSig,(unsigned char) j);

            auto note = pt.note(addr);
            string addressString = EncodePaymentAddress(addr);
            if (addressBalances.count(addressString) == 0)
              addressBalances.insert(make_pair(addressString,txAmounts));

            if (txType == 0) {
              addressBalances.at(addressString).confirmed += note.value();
              privateConfirmed += note.value();
            } else if (txType == 1) {
              addressBalances.at(addressString).immature+= note.value();
              privateImmature += note.value();
            } else if (txType == 2) {
              addressBalances.at(addressString).unconfirmed += note.value();
              privateUnconfirmed += note.value();
            } else if (txType == 3) {
              addressBalances.at(addressString).locked += note.value();
              privateLocked += note.value();
            }

            continue;

          } catch (const note_decryption_failed &err) {
            //do nothing
          }
        }
      }
    }


    CAmount nBalance = 0;
    CAmount nBalanceUnconfirmed = 0;
    CAmount nBalanceTotal = 0;
    CAmount totalBalance= confirmed + privateConfirmed;
    CAmount totalUnconfirmed = unconfirmed + privateUnconfirmed;


    returnObj.push_back(Pair("connectionCount", connectionCount));
    returnObj.push_back(Pair("besttime", chainActive.Tip()->GetBlockTime()));
    returnObj.push_back(Pair("bestblockhash", chainActive.Tip()->GetBlockHash().GetHex()));
    returnObj.push_back(Pair("transparentbalance", FormatMoney(confirmed)));
    returnObj.push_back(Pair("transparentbalanceunconfirmed", FormatMoney(unconfirmed)));
    returnObj.push_back(Pair("privatebalance", FormatMoney(privateConfirmed)));
    returnObj.push_back(Pair("privatebalanceunconfirmed", FormatMoney(privateUnconfirmed)));
    returnObj.push_back(Pair("totalbalance", FormatMoney(totalBalance)));
    returnObj.push_back(Pair("totalunconfirmed", FormatMoney(totalUnconfirmed)));
    returnObj.push_back(Pair("lockedbalance", FormatMoney(locked)));
    returnObj.push_back(Pair("immaturebalance", FormatMoney(immature)));

    //get all t address
    UniValue addressbalance(UniValue::VARR);
    UniValue addrlist(UniValue::VOBJ);

    if (params.size() > 0 && (params[0].get_int() == 1 || params[0].get_int() == 0))
    {
      for (map<string, balancestruct>::iterator it = addressBalances.begin(); it != addressBalances.end(); ++it) {
        UniValue addr(UniValue::VOBJ);
        addr.push_back(Pair("amount", ValueFromAmount(it->second.confirmed)));
        addr.push_back(Pair("unconfirmed", ValueFromAmount(it->second.unconfirmed)));
        addr.push_back(Pair("locked", ValueFromAmount(it->second.locked)));
        addr.push_back(Pair("immature", ValueFromAmount(it->second.immature)));
        addr.push_back(Pair("ismine", true));
        addrlist.push_back(Pair(it->first, addr));
      }
    }

    addressbalance.push_back(addrlist);
    returnObj.push_back(Pair("addressbalance", addressbalance));


    //get transactions
    int nCount = 200;
    UniValue trans(UniValue::VARR);
    UniValue transTime(UniValue::VARR);

    if (params.size() == 3)
    {
      nCount = params[2].get_int();
    }

    if (params.size() > 0 && (params[0].get_int() == 2 || params[0].get_int() == 0))
    {
        int day = 365 * 30; //30 Years
        if(params.size() > 1)
        {
            if(params[1].get_int() == 1)
            {
                day = 1;
            }
            else if(params[1].get_int() == 2)
            {
                day = 7;
            }
            else if(params[1].get_int() == 3)
            {
                day = 30;
            }
            else if(params[1].get_int() == 4)
            {
                day = 90;
            }
            else if(params[1].get_int() == 5)
            {
                day = 365;
            }
        }


        uint64_t t = GetTime();
        for (map<int64_t,CWalletTx>::reverse_iterator it = orderedTxs.rbegin(); it != orderedTxs.rend(); ++it)
        {
            const CWalletTx& wtx = (*it).second;

            if (!CheckFinalTx(wtx))
                continue;

            if (wtx.mapSaplingNoteData.size() == 0 && wtx.mapSproutNoteData.size() == 0 && !wtx.IsTrusted())
                continue;

            //Excude transactions with less confirmations than required
            if (wtx.GetDepthInMainChain() < 0 )
              continue;

            //Exclude Transactions older that max days old
            if (wtx.GetDepthInMainChain() > 0 && mapBlockIndex[wtx.hashBlock]->GetBlockTime() < (t - (day * 60 * 60 * 24))) {
              continue;
            }

            zsWalletTxJSON(wtx, trans, "*", false, 0);
            if (trans.size() >= nCount) break;

        }

        vector<UniValue> arrTmp = trans.getValues();

        std::reverse(arrTmp.begin(), arrTmp.end()); // Return oldest to newest

        trans.clear();
        trans.setArray();
        trans.push_backV(arrTmp);
    }

    returnObj.push_back(Pair("listtransactions", trans));
    return returnObj;
}


static const CRPCCommand commands[] =
{   //  category              name                          actor (function)              okSafeMode
    //  --------------------- ------------------------      -----------------------       ----------
    {   "zero Exclusive",     "zs_listtransactions",       &zs_listtransactions,       true },
    {   "zero Exclusive",     "zs_gettransaction",         &zs_gettransaction,         true },
    {   "zero Exclusive",     "zs_listspentbyaddress",     &zs_listspentbyaddress,     true },
    {   "zero Exclusive",     "zs_listreceivedbyaddress",  &zs_listreceivedbyaddress,  true },
    {   "zero Exclusive",     "zs_listsentbyaddress",      &zs_listsentbyaddress,      true },
    {   "zero Exclusive",     "getalldata",                &getalldata,                true },
};

void RegisterZeroExclusiveRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
