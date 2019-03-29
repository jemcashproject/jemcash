#include "util.h"

#include "clientversion.h"
#include "primitives/transaction.h"
#include "random.h"
#include "sync.h"
#include "utilstrencodings.h"
#include "utilmoneystr.h"
#include "test/test_bitcoin.h"

#include <stdint.h>
#include <vector>

#include "chainparams.h"
#include "consensus/consensus.h"
#include "consensus/validation.h"
#include "key.h"
#include "main.h"
#include "miner.h"
#include "pubkey.h"
#include "random.h"
#include "txdb.h"
#include "txmempool.h"
#include "ui_interface.h"
#include "rpc/server.h"
#include "rpc/register.h"
#include "zerocoin.h"

#include "test/testutil.h"
#include "consensus/merkle.h"

#include "wallet/db.h"
#include "wallet/wallet.h"

#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/thread.hpp>

extern CCriticalSection cs_main;
using namespace std;

CScript scriptPubKeyMtpHalving;


struct MtpHalvingTestingSetup : public TestingSetup {
    MtpHalvingTestingSetup() : TestingSetup(CBaseChainParams::REGTEST)
    {
        CPubKey newKey;
        BOOST_CHECK(pwalletMain->GetKeyFromPool(newKey));

        string strAddress = CBitcoinAddress(newKey.GetID()).ToString();
        pwalletMain->SetAddressBook(CBitcoinAddress(strAddress).Get(), "",
                               ( "receive"));

        printf("Balance before %ld\n", pwalletMain->GetBalance());
        scriptPubKeyMtpHalving = CScript() <<  ToByteVector(newKey/*coinbaseKey.GetPubKey()*/) << OP_CHECKSIG;
        bool mtp = false;
        CBlock b;
        for (int i = 0; i < 150; i++)
        {
            std::vector<CMutableTransaction> noTxns;
            b = CreateAndProcessBlock(noTxns, scriptPubKeyMtpHalving, mtp);
            coinbaseTxns.push_back(b.vtx[0]);
            LOCK(cs_main);
            {
                LOCK(pwalletMain->cs_wallet);
                pwalletMain->AddToWalletIfInvolvingMe(b.vtx[0], &b, true);
            }   
        }
        printf("Balance after 150 blocks: %ld\n", pwalletMain->GetBalance());
    }

    CBlock CreateBlock(const std::vector<CMutableTransaction>& txns,
                       const CScript& scriptPubKeyMtpHalving, bool mtp = false) {
        const CChainParams& chainparams = Params();
        CBlockTemplate *pblocktemplate = BlockAssembler(chainparams).CreateNewBlock(scriptPubKeyMtpHalving);
        CBlock& block = pblocktemplate->block;

        // Replace mempool-selected txns with just coinbase plus passed-in txns:
        if(txns.size() > 0) {
            block.vtx.resize(1);
            BOOST_FOREACH(const CMutableTransaction& tx, txns)
                block.vtx.push_back(tx);
        }
        // IncrementExtraNonce creates a valid coinbase and merkleRoot
        unsigned int extraNonce = 0;
        IncrementExtraNonce(&block, chainActive.Tip(), extraNonce);

        while (!CheckProofOfWork(block.GetHash(), block.nBits, chainparams.GetConsensus())){
            ++block.nNonce;
        }
        if(mtp) {
            while (!CheckMerkleTreeProof(block, chainparams.GetConsensus())){
                block.mtpHashValue = mtp::hash(block, Params().GetConsensus().powLimit);
            }
        }
        else {
            while (!CheckProofOfWork(block.GetHash(), block.nBits, chainparams.GetConsensus())){
                ++block.nNonce;
            }
        }

        //delete pblocktemplate;
        return block;
    }

    bool ProcessBlock(CBlock &block) {
        const CChainParams& chainparams = Params();
        CValidationState state;
        return ProcessNewBlock(state, chainparams, NULL, &block, true, NULL, false);
    }

    // Create a new block with just given transactions, coinbase paying to
    // scriptPubKeyMtpHalving, and try to add it to the current chain.
    CBlock CreateAndProcessBlock(const std::vector<CMutableTransaction>& txns,
                                 const CScript& scriptPubKeyMtpHalving, bool mtp = false){

        CBlock block = CreateBlock(txns, scriptPubKeyMtpHalving, mtp);
        BOOST_CHECK_MESSAGE(ProcessBlock(block), "Processing block failed");
        return block;
    }

    std::vector<CTransaction> coinbaseTxns; // For convenience, coinbase transactions
    CKey coinbaseKey; // private/public key needed to spend coinbase transactions
};

BOOST_FIXTURE_TEST_SUITE(mtp_halving_tests, MtpHalvingTestingSetup)

BOOST_AUTO_TEST_CASE(mtp_halving)
{
    bool mtp = false;
    CBlock b;
    //Good check to have
    BOOST_CHECK_MESSAGE(!b.fChecked, "fChecked must be initialized to false");
    const CChainParams& chainparams = Params();

    std::vector<CMutableTransaction> noTxns;
    b = CreateBlock(noTxns, scriptPubKeyMtpHalving, mtp);
    CAmount blockReward = 0;
    for(auto txout : b.vtx[0].vout)
        blockReward += txout.nValue;
    BOOST_CHECK_MESSAGE(blockReward == 50 * COIN, "Block reward not correct in MTP block");
    CBlock oldBlock = b;

    int previousHeight = chainActive.Height();
    ProcessBlock(b);
    BOOST_CHECK_MESSAGE(previousHeight == chainActive.Height() - 1, "Block not connected");

    //Transition to MTP
    mtp = true;
    Params(CBaseChainParams::REGTEST).SetRegTestMtpSwitchTime(GetAdjustedTime());

    b = CreateBlock(noTxns, scriptPubKeyMtpHalving, mtp);

    CBlock bMtp = b;
    blockReward = 0;
    for(int i = 0; i < oldBlock.vtx[0].vout.size(); i++) {
        BOOST_CHECK_MESSAGE(oldBlock.vtx[0].vout[i].nValue == bMtp.vtx[0].vout[i].nValue * 2, "Block reward not halved");
    }
    for(auto txout : bMtp.vtx[0].vout)
        blockReward += txout.nValue;
    BOOST_CHECK_MESSAGE(blockReward == 25 * COIN, "Block reward not correct in MTP block");

    for(int i = 0; i < bMtp.vtx[0].vout.size(); i++)
    {
        CBlock bModified = bMtp;
        bModified.vtx[0].vout[i].nValue += COIN;
        bModified.vtx[0].UpdateHash();
        bool mutated;
        bModified.hashMerkleRoot = BlockMerkleRoot(bModified, &mutated);

        bModified.fChecked = false;
        while (!CheckMerkleTreeProof(bModified, chainparams.GetConsensus())){
            bModified.mtpHashValue = mtp::hash(bModified, Params().GetConsensus().powLimit);
        }
        int previousHeight = chainActive.Height();
        ProcessBlock(bModified);
        BOOST_CHECK_MESSAGE(previousHeight == chainActive.Height(), "Invalid Block connected");
    }


    for(int i = 1; i < bMtp.vtx[0].vout.size() - 1; i++)
    {
        CBlock bModified = bMtp;
        CPubKey modifiedKey;
        BOOST_CHECK(pwalletMain->GetKeyFromPool(modifiedKey));
        CScript modifiedScript = CScript() <<  ToByteVector(modifiedKey) << OP_CHECKSIG;
        bModified.vtx[0].vout[i].scriptPubKey = modifiedScript;
        bModified.vtx[0].UpdateHash();
        bool mutated;
        bModified.hashMerkleRoot = BlockMerkleRoot(bModified, &mutated);

        bModified.fChecked = false;
        while (!CheckMerkleTreeProof(bModified, chainparams.GetConsensus())){
            bModified.mtpHashValue = mtp::hash(bModified, Params().GetConsensus().powLimit);
        }
        int previousHeight = chainActive.Height();
        ProcessBlock(bModified);
        BOOST_CHECK_MESSAGE(previousHeight == chainActive.Height(), "Invalid Block connected");
    }

    bMtp = CreateBlock(noTxns, scriptPubKeyMtpHalving, mtp);
    previousHeight = chainActive.Height();
    ProcessBlock(bMtp);
    BOOST_CHECK_MESSAGE(previousHeight == chainActive.Height() - 1, "Block not connected");


    coinbaseTxns.push_back(b.vtx[0]);
    LOCK(cs_main);
    {
        LOCK(pwalletMain->cs_wallet);
        pwalletMain->AddToWalletIfInvolvingMe(b.vtx[0], &b, true);
    }
    Params(CBaseChainParams::REGTEST).SetRegTestMtpSwitchTime(INT_MAX);

}

BOOST_AUTO_TEST_SUITE_END()
