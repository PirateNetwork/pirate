// Copyright (c) 2019 Cryptoforge
// Copyright (c) 2019 The Zero developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "init.h"
#include "rpc/server.h"
#include "wallet.h"
#include "witness.h"
#include "utilmoneystr.h"
#include "coins.h"

using namespace std;
using namespace libzcash;

bool EnsureWalletIsAvailable(bool avoidException);

UniValue getsaplingwitness(const UniValue& params, bool fHelp,  const CPubKey& mypk) {

  if (!EnsureWalletIsAvailable(fHelp))
      return NullUniValue;

  if (fHelp || params.size() != 2)
      throw runtime_error(
          "getsaplingwitness txid sheildedOutputIndex\n"
          + HelpExampleCli("getsaplingwitness","26d4c79aab980bc39ac0deb1d8224c399249a6f5d6d3b3a6d58e6374750854c1 0")
          + HelpExampleRpc("getsaplingwitness","26d4c79aab980bc39ac0deb1d8224c399249a6f5d6d3b3a6d58e6374750854c1 0")
      );

  LOCK2(cs_main, pwalletMain->cs_wallet);

  //UniValue obj to be returned
  UniValue wit(UniValue::VOBJ);

  //Set Parameters
  uint256 ctxhash;
  ctxhash.SetHex(params[0].get_str());
  int64_t nSheildedOutputIndex = params[1].get_int64();

  //get transaction with the sapling shielded output to be witnessed
  UniValue ret(UniValue::VARR);

  CTransaction ctx;
  uint256 hashBlock;
  int nHeight = 0;


  {
      if (!GetTransaction(ctxhash, ctx, hashBlock, true))
          throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available about transaction");

      BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
      if (mi != mapBlockIndex.end() && (*mi).second) {
          CBlockIndex* pindex = (*mi).second;
          if (chainActive.Contains(pindex)) {
              nHeight = pindex->GetHeight();
          } else {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "transaction not found in valid block");
          }
      }
  }

  //Check that sapling is active
  const bool saplingActive = NetworkUpgradeActive(nHeight, Params().GetConsensus(), Consensus::UPGRADE_SAPLING);
  if (!saplingActive)
      throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Sapling not active");

  //Get the blocckindex of the previous block
  CBlockIndex* pblockindex = chainActive[nHeight];

  //Get the sapling tree as of the previous block
  SaplingMerkleTree saplingTree;
  pcoinsTip->GetSaplingAnchorAt(pblockindex->pprev->hashFinalSaplingRoot, saplingTree);

  //Cycle through block and transactions build sapling tree until the commitment needed is reached
  CBlock pblock;
  ReadBlockFromDisk(pblock, pblockindex, false);
  for (const CTransaction& tx : pblock.vtx) {
      auto hash = tx.GetHash();

      //Check if output exists
      if (hash == ctxhash && (tx.vShieldedOutput.size() == 0 || tx.vShieldedOutput.size() < nSheildedOutputIndex + 1))
          throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Sapling Output does not exist");

      for (int64_t i = 0; i < tx.vShieldedOutput.size(); i++) {
          const uint256& note_commitment = tx.vShieldedOutput[i].cm;
          saplingTree.append(note_commitment);

          if (hash == ctxhash && i == nSheildedOutputIndex) {
            CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
            ss << saplingTree.witness();
            wit.push_back(Pair("height", nHeight));
            wit.push_back(Pair("txhash", hash.ToString()));
            wit.push_back(Pair("sheildedoutputindex", i));
            wit.push_back(Pair("witness", HexStr(ss.begin(), ss.end())));
          }
      }
  }

  return wit;

}


UniValue getsaplingwitnessatheight(const UniValue& params, bool fHelp,  const CPubKey& mypk) {

  if (!EnsureWalletIsAvailable(fHelp))
      return NullUniValue;

  if (fHelp || params.size() != 3)
      throw runtime_error(
          "getsaplingwitnessatheight txid sheildedOutputIndex\n"
          + HelpExampleCli("getsaplingwitnessatheight","26d4c79aab980bc39ac0deb1d8224c399249a6f5d6d3b3a6d58e6374750854c1 0")
          + HelpExampleRpc("getsaplingwitnessatheight","26d4c79aab980bc39ac0deb1d8224c399249a6f5d6d3b3a6d58e6374750854c1 0")
      );

  LOCK2(cs_main, pwalletMain->cs_wallet);

  //UniValue obj to be returned
  UniValue wit(UniValue::VOBJ);

  //Set Parameters
  uint256 ctxhash;
  ctxhash.SetHex(params[0].get_str());
  int64_t nSheildedOutputIndex = params[1].get_int64();
  int64_t toHeight = params[2].get_int64();

  //get transaction with the sapling shielded output to be witnessed
  UniValue ret(UniValue::VARR);

  CTransaction ctx;
  uint256 hashBlock;
  int nHeight = 0;

  {
      if (!GetTransaction(ctxhash, ctx, hashBlock, true))
          throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available about transaction");

      BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
      if (mi != mapBlockIndex.end() && (*mi).second) {
          CBlockIndex* pindex = (*mi).second;
          if (chainActive.Contains(pindex)) {
              nHeight = pindex->GetHeight();
          } else {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "transaction not found in valid block");
          }
      }
  }

  //Check that sapling is active
  const bool saplingActive = NetworkUpgradeActive(nHeight, Params().GetConsensus(), Consensus::UPGRADE_SAPLING);
  if (!saplingActive)
      throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Sapling not active");

  //Get the blocckindex of the previous block
  CBlockIndex* pblockindex = chainActive[nHeight];

  //Get the sapling tree as of the previous block
  SaplingMerkleTree saplingTree;
  auto witness = saplingTree.witness();
  pcoinsTip->GetSaplingAnchorAt(pblockindex->pprev->hashFinalSaplingRoot, saplingTree);

  //Cycle through block and transactions build sapling tree until the commitment needed is reached
  CBlock pblock;
  ReadBlockFromDisk(pblock, pblockindex, false);
  for (const CTransaction& tx : pblock.vtx) {
      auto hash = tx.GetHash();

      //Check if output exists
      if (hash == ctxhash && (tx.vShieldedOutput.size() == 0 || tx.vShieldedOutput.size() < nSheildedOutputIndex + 1))
          throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Sapling Output does not exist");

      for (int64_t i = 0; i < tx.vShieldedOutput.size(); i++) {
          const uint256& note_commitment = tx.vShieldedOutput[i].cm;
          saplingTree.append(note_commitment);
          if (hash == ctxhash && i == nSheildedOutputIndex) {
            witness = saplingTree.witness();
          }
          if (hash == ctxhash && i > nSheildedOutputIndex) {
            witness.append(note_commitment);
          }
      }
  }


  for (int64_t j = nHeight + 1; j <= toHeight; j++) {
    UniValue jsonblock(UniValue::VOBJ);
    std::string strHash = chainActive[j]->GetBlockHash().GetHex();

    uint256 hash(uint256S(strHash));

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hash];

    if (fHavePruned && !(pblockindex->nStatus & BLOCK_HAVE_DATA) && pblockindex->nTx > 0)
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Block not available (pruned data)");

    if(!ReadBlockFromDisk(block, pblockindex, false))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");


    // UniValue jsontxs(UniValue::VARR);
    for (const CTransaction& tx : block.vtx) {
      for (int64_t i = 0; i < tx.vShieldedOutput.size(); i++) {
        const uint256& note_commitment = tx.vShieldedOutput[i].cm;
        saplingTree.append(note_commitment);
        witness.append(note_commitment);
      }
    }

    if (j == toHeight) {
      wit.push_back(Pair("hash", block.GetHash().GetHex()));
      wit.push_back(Pair("height", j));
      wit.push_back(Pair("finalsaplingroot", block.hashFinalSaplingRoot.GetHex()));
    }
  }

  CDataStream iss(SER_NETWORK, PROTOCOL_VERSION);
  iss << witness;
  wit.push_back(Pair("incrementedwitness", HexStr(iss.begin(), iss.end())));
  wit.push_back(Pair("incrementedroot", witness.root().GetHex()));

  return wit;
}


UniValue getsaplingblocks(const UniValue& params, bool fHelp,  const CPubKey& mypk)
{
    if (fHelp || params.size() != 2)
        throw runtime_error(
            "getsaplingblocks \"startheight blocksqty\" ( verbosity )\n"

            "\nExamples:\n"
            + HelpExampleCli("getsaplingblocks", "12800 1")
            + HelpExampleRpc("getsaplingblocks", "12800 1")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

   int64_t nHeight = params[0].get_int64();
   int64_t nBlocks = params[1].get_int64();


    if (nHeight < 0 || nHeight > chainActive.Height()) {
      nHeight == chainActive.Height();
      nBlocks = -1;
    }

    if (nBlocks < 0) {
      nBlocks = -1;
    }

    if (chainActive.Height() - nHeight < nBlocks && chainActive.Height() != nHeight) {
      nBlocks = chainActive.Height() - nHeight + 1;
    }

    if (chainActive.Height() == nHeight) {
      nBlocks = 1;
    }

    UniValue result(UniValue::VARR);
    for (int64_t i = 0; i < nBlocks; i++) {
      UniValue jsonblock(UniValue::VOBJ);
      std::string strHash = chainActive[nHeight + i]->GetBlockHash().GetHex();

      uint256 hash(uint256S(strHash));

      CBlock block;
      CBlockIndex* pblockindex = mapBlockIndex[hash];

      if (fHavePruned && !(pblockindex->nStatus & BLOCK_HAVE_DATA) && pblockindex->nTx > 0)
          throw JSONRPCError(RPC_INTERNAL_ERROR, "Block not available (pruned data)");

      if(!ReadBlockFromDisk(block, pblockindex, false))
          throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");

      jsonblock.push_back(Pair("hash", block.GetHash().GetHex()));
      jsonblock.push_back(Pair("previousblockhash", pblockindex->pprev->GetBlockHash().GetHex()));
      jsonblock.push_back(Pair("height", pblockindex->GetHeight()));
      jsonblock.push_back(Pair("finalsaplingroot", block.hashFinalSaplingRoot.GetHex()));

      UniValue jsontxs(UniValue::VARR);
      for (const CTransaction& tx : block.vtx) {
        UniValue jsontx(UniValue::VOBJ);
        jsontx.push_back(Pair("txid",tx.GetHash().ToString()));

        UniValue jsonShieldOutputs(UniValue::VARR);
        for (int64_t i = 0; i < tx.vShieldedOutput.size(); i++) {
          UniValue jsonShieldOutput(UniValue::VOBJ);
          jsonShieldOutput.push_back(Pair("sheildedoutputindex", i));
          jsonShieldOutput.push_back(Pair("cv", tx.vShieldedOutput[i].cv.ToString()));
          jsonShieldOutput.push_back(Pair("cm", tx.vShieldedOutput[i].cm.ToString()));
          jsonShieldOutput.push_back(Pair("ephemeralKey", tx.vShieldedOutput[i].ephemeralKey.ToString()));
          auto encCiphertext = tx.vShieldedOutput[i].encCiphertext;
          jsonShieldOutput.push_back(Pair("encCiphertext", HexStr(encCiphertext.begin(),encCiphertext.end())));
          auto outCiphertext = tx.vShieldedOutput[i].outCiphertext;
          jsonShieldOutput.push_back(Pair("outCiphertext", HexStr(outCiphertext.begin(),outCiphertext.end())));
          jsonShieldOutputs.push_back(jsonShieldOutput);
        }

        UniValue jsonShieldSpends(UniValue::VARR);
        for (int64_t i = 0; i < tx.vShieldedSpend.size(); i++) {
          UniValue jsonShieldSpend(UniValue::VOBJ);
          jsonShieldSpend.push_back(Pair("sheildedspendindex", i));
          jsonShieldSpend.push_back(Pair("nullifier", tx.vShieldedSpend[i].nullifier.ToString()));
          jsonShieldSpends.push_back(jsonShieldSpend);
        }

        jsontx.push_back(Pair("vShieldedOutput",jsonShieldOutputs));
        jsontx.push_back(Pair("vShieldedSpend",jsonShieldSpends));

        if (tx.vShieldedOutput.size() > 0 || tx.vShieldedSpend.size() > 0)
          jsontxs.push_back(jsontx);

      }
      jsonblock.push_back(Pair("transactions",jsontxs));
      result.push_back(jsonblock);
    }

    return result;
}



static const CRPCCommand commands[] =
{   //  category              name                            actor (function)              okSafeMode
    //  --------------------- ------------------------        -----------------------       ----------
    {   "zero Experimental",     "getsaplingwitness",         &getsaplingwitness,           true },
    {   "zero Experimental",     "getsaplingwitnessatheight", &getsaplingwitnessatheight,   true },
    {   "zero Experimental",     "getsaplingblocks",          &getsaplingblocks,            true },

};

void RegisterZeroExperimentalRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
