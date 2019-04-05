// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "consensus/merkle.h"
#include "consensus/consensus.h"
#include "zerocoin_params.h"

#include "tinyformat.h"
#include "util.h"
#include "utilstrencodings.h"
#include "libzerocoin/bitcoin_bignum/bignum.h"

#include <assert.h>

#include <boost/assign/list_of.hpp>

#include "chainparamsseeds.h"
#include "arith_uint256.h"
#include "crypto/scrypt.h"


static CBlock CreateGenesisBlock(const char *pszTimestamp, const CScript &genesisOutputScript, uint32_t nTime, uint32_t nNonce,
                   uint32_t nBits, int32_t nVersion, const CAmount &genesisReward,
                   std::vector<unsigned char> extraNonce) {
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 504365040 << CBigNum(4).getvch() << std::vector < unsigned char >
    ((const unsigned char *) pszTimestamp, (const unsigned char *) pszTimestamp + strlen(pszTimestamp)) << extraNonce;
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime = nTime;
    genesis.nBits = nBits;
    genesis.nNonce = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(txNew);
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount &genesisReward,
                   std::vector<unsigned char> extraNonce) {
    //btzc: jemcash timestamp
    const char *pszTimestamp = "Jemcash is an experimental PoW cryptocurrency project using the MTP algorithm";
    const CScript genesisOutputScript = CScript();
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward,
                              extraNonce);
}

/**
 * Main network
 */
/**
 * What makes a good checkpoint block?
 * + Is surrounded by blocks with reasonable timestamps
 *   (no blocks before with a timestamp after, none after with
 *    timestamp before)
 * + Contains no strange transactions
 */

class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = "main";

        consensus.chainType = Consensus::chainMain;
        consensus.nSubsidyHalvingInterval = 416666;
        consensus.nMajorityEnforceBlockUpgrade = 750;
        consensus.nMajorityRejectBlockOutdated = 950;
        consensus.nMajorityWindow = 1000;
        consensus.nMinNFactor = 10;
        consensus.nMaxNFactor = 30;
        //nVertcoinStartTime
        consensus.nChainStartTime = 1551398400;
        consensus.powLimit = uint256S("00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        //static const int64 nInterval = nTargetTimespan / nTargetSpacing;
        consensus.nPowTargetTimespan = 60 * 60; // 60 minutes between retargets
        consensus.nPowTargetSpacing = 5 * 60; // 5 minute blocks
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1916; // 95% of 2016
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1475020800; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1462060800; // May 1st, 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1493596800; // May 1st, 2017

        // Deployment of SegWit (BIP141, BIP143, and BIP147)
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = 1479168000; // November 15th, 2016.
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = 1510704000; // November 15th, 2017.

        // Deployment of MTP
        consensus.vDeployments[Consensus::DEPLOYMENT_MTP].bit = 12;
        consensus.vDeployments[Consensus::DEPLOYMENT_MTP].nStartTime = SWITCH_TO_MTP_BLOCK_HEADER; //- 2*60; // 2 hours leeway
        consensus.vDeployments[Consensus::DEPLOYMENT_MTP].nTimeout = SWITCH_TO_MTP_BLOCK_HEADER + consensus.nMinerConfirmationWindow*2 * 5*60;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x0000000000000000000000000000000000000000000000000708f98bf623f02e");

        consensus.nCheckBugFixedAtBlock = JC_CHECK_BUG_FIXED_AT_BLOCK;
        consensus.nJnodePaymentsBugFixedAtBlock = JC_JNODE_PAYMENT_BUG_FIXED_AT_BLOCK;
	    consensus.nSpendV15StartBlock = JC_V1_5_STARTING_BLOCK;
	    consensus.nSpendV2ID_1 = JC_V2_SWITCH_ID_1;
	    consensus.nSpendV2ID_10 = JC_V2_SWITCH_ID_10;
	    consensus.nSpendV2ID_25 = JC_V2_SWITCH_ID_25;
	    consensus.nSpendV2ID_50 = JC_V2_SWITCH_ID_50;
	    consensus.nSpendV2ID_100 = JC_V2_SWITCH_ID_100;
	    consensus.nModulusV2StartBlock = JC_MODULUS_V2_START_BLOCK;
        consensus.nModulusV1MempoolStopBlock = JC_MODULUS_V1_MEMPOOL_STOP_BLOCK;
	    consensus.nModulusV1StopBlock = JC_MODULUS_V1_STOP_BLOCK;
        consensus.nMultipleSpendInputsInOneTxStartBlock = JC_MULTIPLE_SPEND_INPUT_STARTING_BLOCK;
        consensus.nDontAllowDupTxsStartBlock = 1;

        // jnode params
        consensus.nJnodePaymentsStartBlock = HF_JNODE_PAYMENT_START; // not true, but it's ok as long as it's less then nJnodePaymentsIncreaseBlock
        // consensus.nJnodePaymentsIncreaseBlock = 680000; // actual historical value // not used for now, probably later
        // consensus.nJnodePaymentsIncreasePeriod = 576*30; // 17280 - actual historical value // not used for now, probably later
        // consensus.nSuperblockStartBlock = 614820;
        // consensus.nBudgetPaymentsStartBlock = 328008; // actual historical value
        // consensus.nBudgetPaymentsCycleBlocks = 16616; // ~(60*24*30)/2.6, actual number of blocks per month is 200700 / 12 = 16725
        // consensus.nBudgetPaymentsWindowBlocks = 100;

        consensus.nMTPSwitchTime = SWITCH_TO_MTP_BLOCK_HEADER;
        consensus.nMTPFiveMinutesStartBlock = SWITCH_TO_MTP_5MIN_BLOCK;
        consensus.nDifficultyAdjustStartBlock = 0;
        consensus.nFixedDifficulty = 0x2000ffff;
        consensus.nPowTargetSpacingMTP = 5*60;
        consensus.nInitialMTPDifficulty = 0x1e0ffe57;
        consensus.nMTPRewardReduction = 1;

        nMaxTipAge = 6 * 60 * 60; // ~144 blocks behind -> 2 x fork detection time, was 24 * 60 * 60 in bitcoin

        nPoolMaxTransactions = 3;
        nFulfilledRequestExpireTime = 60*60; // fulfilled requests expire in 1 hour
        strSporkPubKey = "04f14125319598559d6e7dcf4086bd049f528ab58b74e73c36a6c92fecc034c8d2da7f61a2d2ecf81020d4a85fa3ab4539ab1f274d72f2577945938846afe4d939";
        strJnodePaymentsPubKey = "04f14125319598559d6e7dcf4086bd049f528ab58b74e73c36a6c92fecc034c8d2da7f61a2d2ecf81020d4a85fa3ab4539ab1f274d72f2577945938846afe4d939";

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
       `  * a large 32-bit integer with any alignment.
         */
        //btzc: update jemcash pchMessage
        pchMessageStart[0] = 0x23;
        pchMessageStart[1] = 0xc1;
        pchMessageStart[2] = 0x2b;
        pchMessageStart[3] = 0xe4;
        nDefaultPort = 2810;
        nPruneAfterHeight = 100000;
        /**
         * btzc: jemcash init genesis block
         * nBits = 0x1e0ffff0
         * nTime = 1554465600
         * nNonce = 654968
         * genesisReward = 0 * COIN
         * nVersion = 2
         * extraNonce
         */
        std::vector<unsigned char> extraNonce(4);
        extraNonce[0] = 0x82;
        extraNonce[1] = 0x3f;
        extraNonce[2] = 0x00;
        extraNonce[3] = 0x00;
        genesis = CreateGenesisBlock(JC_GENESIS_BLOCK_TIME, 654968, 0x1e0ffff0, 2, 0 * COIN, extraNonce);
		
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0xc5dc88c19045d7be42094d903b10a0b2ec1ff6550104bcc5644cba7988029863"));
        assert(genesis.hashMerkleRoot == uint256S("0x649284cf542e672728254da68356972182878ece4607dda1f73d0c696de2427a"));
		
        vSeeds.push_back(CDNSSeedData("seed1.jemcash.info", "seed1.jemcash.info", false));
        vSeeds.push_back(CDNSSeedData("seed2.jemcash.info", "seed2.jemcash.info", false));
		vSeeds.push_back(CDNSSeedData("seed3.jemcash.info", "seed3.jemcash.info", false));
		vSeeds.push_back(CDNSSeedData("seed4.jemcash.info", "seed4.jemcash.info", false));
		vSeeds.push_back(CDNSSeedData("seed5.jemcash.info", "seed5.jemcash.info", false));

        // Note that of those with the service bits flag, most only support a subset of possible options
        base58Prefixes[PUBKEY_ADDRESS] = std::vector < unsigned char > (1, 105);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector < unsigned char > (1, 5);
        base58Prefixes[SECRET_KEY] = std::vector < unsigned char > (1, 138);
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x88)(0xB2)(0x1E).convert_to_container < std::vector < unsigned char > > ();
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x88)(0xAD)(0xE4).convert_to_container < std::vector < unsigned char > > ();

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;
        fTestnetToBeDeprecatedFieldRPC = false;

        checkpointData = (CCheckpointData) {
                boost::assign::map_list_of
                        (0, uint256S("0xc5dc88c19045d7be42094d903b10a0b2ec1ff6550104bcc5644cba7988029863")),
                1554465600, // * UNIX timestamp of last checkpoint block
                0,    // * total number of transactions between genesis and last checkpoint
                //   (the tx=... number in the SetBestChain debug.log lines)
                1200.0     // * estimated number of transactions per day after checkpoint
        };
    }
};

static CMainParams mainParams;

/**
 * Testnet (v3)
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = "test";

        consensus.chainType = Consensus::chainTestnet;
        consensus.nSubsidyHalvingInterval = 400;
        consensus.nMajorityEnforceBlockUpgrade = 51;
        consensus.nMajorityRejectBlockOutdated = 75;
        consensus.nMajorityWindow = 100;
        consensus.nMinNFactor = 10;
        consensus.nMaxNFactor = 30;
        consensus.nChainStartTime = 1551398400;
        consensus.powLimit = uint256S("00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 60 * 60; // 60 minutes between retargets
        consensus.nPowTargetSpacing = 150; // 2.5 minute blocks
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1512; // 75% for testchains
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1456790400; // March 1st, 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1493596800; // May 1st, 2017

        // Deployment of SegWit (BIP141, BIP143, and BIP147)
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = 1462060800; // May 1st 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = 1493596800; // May 1st 2017

        // Deployment of MTP
        consensus.vDeployments[Consensus::DEPLOYMENT_MTP].bit = 12;
        consensus.vDeployments[Consensus::DEPLOYMENT_MTP].nStartTime = 1539172800 - 2*60;
        consensus.vDeployments[Consensus::DEPLOYMENT_MTP].nTimeout = 1539172800 + consensus.nMinerConfirmationWindow*2 * 5*60;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x0000000000000000000000000000000000000000000000000708f98bf623f02e");

	    consensus.nSpendV15StartBlock = 5000;
        consensus.nCheckBugFixedAtBlock = 1;
        consensus.nJnodePaymentsBugFixedAtBlock = 1;

	    consensus.nSpendV2ID_1 = JC_V2_TESTNET_SWITCH_ID_1;
	    consensus.nSpendV2ID_10 = JC_V2_TESTNET_SWITCH_ID_10;
	    consensus.nSpendV2ID_25 = JC_V2_TESTNET_SWITCH_ID_25;
	    consensus.nSpendV2ID_50 = JC_V2_TESTNET_SWITCH_ID_50;
	    consensus.nSpendV2ID_100 = JC_V2_TESTNET_SWITCH_ID_100;
	    consensus.nModulusV2StartBlock = JC_MODULUS_V2_TESTNET_START_BLOCK;
        consensus.nModulusV1MempoolStopBlock = JC_MODULUS_V1_TESTNET_MEMPOOL_STOP_BLOCK;
	    consensus.nModulusV1StopBlock = JC_MODULUS_V1_TESTNET_STOP_BLOCK;
        consensus.nMultipleSpendInputsInOneTxStartBlock = 1;
        consensus.nDontAllowDupTxsStartBlock = 1;

        // Jnode params testnet
        consensus.nJnodePaymentsStartBlock = 250;
        //consensus.nJnodePaymentsIncreaseBlock = 360; // not used for now, probably later
        //consensus.nJnodePaymentsIncreasePeriod = 650; // not used for now, probably later
        //consensus.nSuperblockStartBlock = 61000;
        //consensus.nBudgetPaymentsStartBlock = 60000;
        //consensus.nBudgetPaymentsCycleBlocks = 50;
        //consensus.nBudgetPaymentsWindowBlocks = 10;
        nMaxTipAge = 0x7fffffff; // allow mining on top of old blocks for testnet

        consensus.nMTPSwitchTime = 1552345200;
        consensus.nMTPFiveMinutesStartBlock = 0;
        consensus.nDifficultyAdjustStartBlock = 100;
        consensus.nFixedDifficulty = 0x2000ffff;
        consensus.nPowTargetSpacingMTP = 5*60;
        consensus.nInitialMTPDifficulty = 0x2000ffff;  // !!!! change it to the real value
        consensus.nMTPRewardReduction = 1;

        nPoolMaxTransactions = 3;
        nFulfilledRequestExpireTime = 5*60; // fulfilled requests expire in 5 minutes
        strSporkPubKey = "040282f89de76e2d1c09325a2327890a9dd401d02bd4a79adcde3d247a4125a8beba2b7d59a65495f84aeb36149af6e12164cdcaf76a751f88e2a09ac20616f78f";
        strJnodePaymentsPubKey = "040282f89de76e2d1c09325a2327890a9dd401d02bd4a79adcde3d247a4125a8beba2b7d59a65495f84aeb36149af6e12164cdcaf76a751f88e2a09ac20616f78f";

        pchMessageStart[0] = 0xd2;
        pchMessageStart[1] = 0x12;
        pchMessageStart[2] = 0x4b;
        pchMessageStart[3] = 0xc1;
        nDefaultPort = 2811;
        nPruneAfterHeight = 1000;
        /**
          * btzc: testnet params
          * nTime: 1552341600
          * nNonce: 1728165
          */
        std::vector<unsigned char> extraNonce(4);
        extraNonce[0] = 0x08;
        extraNonce[1] = 0x00;
        extraNonce[2] = 0x00;
        extraNonce[3] = 0x00;
        genesis = CreateGenesisBlock(1552341600, 1728165, 0x1e0ffff0, 2, 0 * COIN, extraNonce);
		
        consensus.hashGenesisBlock = genesis.GetHash();
        //btzc: update testnet jemcash hashGenesisBlock and hashMerkleRoot
        assert(consensus.hashGenesisBlock ==
               uint256S("0xf2233abb04c383dc2e237ac27a048fa95a602f1fcc73e87309af1a82c2331c2c"));
        assert(genesis.hashMerkleRoot ==
               uint256S("0x708e5052502ad3aa4d50af455ec8f8e4f8d23694d20fb28ea559c246c6e65f62"));
        vFixedSeeds.clear();
        vSeeds.clear();
        // nodes with support for servicebits filtering should be at the top
        // jemcash test seeds
        vSeeds.push_back(CDNSSeedData("testseed1.jemcash.info", "testseed1.jemcash.info", false));
        vSeeds.push_back(CDNSSeedData("testseed2.jemcash.info", "testseed2.jemcash.info", false));

        base58Prefixes[PUBKEY_ADDRESS] = std::vector < unsigned char > (1, 65);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector < unsigned char > (1, 178);
        base58Prefixes[SECRET_KEY] = std::vector < unsigned char > (1, 185);
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x35)(0x87)(0xCF).convert_to_container < std::vector < unsigned char > > ();
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x35)(0x83)(0x94).convert_to_container < std::vector < unsigned char > > ();
        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_test, pnSeed6_test + ARRAYLEN(pnSeed6_test));

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fMineBlocksOnDemand = false;
        fTestnetToBeDeprecatedFieldRPC = true;

        checkpointData = (CCheckpointData) {
                boost::assign::map_list_of
                        (0, uint256S("0xf2233abb04c383dc2e237ac27a048fa95a602f1fcc73e87309af1a82c2331c2c")),
                        1552341600,
                        0,
                        100.0
        };
    }
};

static CTestNetParams testNetParams;

/**
 * Regression test
 */
class CRegTestParams : public CChainParams {
public:
    CRegTestParams() {
        strNetworkID = "regtest";

        consensus.chainType = Consensus::chainRegtest;
        consensus.nSubsidyHalvingInterval = 288;
        consensus.nMajorityEnforceBlockUpgrade = 750;
        consensus.nMajorityRejectBlockOutdated = 950;
        consensus.nMajorityWindow = 1000;
        //consensus.BIP34Height = -1; // BIP34 has not necessarily activated on regtest
       // consensus.BIP34Hash = uint256();
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 60 * 60 * 1000; // 60 minutes between retargets
        consensus.nPowTargetSpacing = 1; // 10 minute blocks
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true;
        consensus.nJnodePaymentsStartBlock = 120;
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144; // Faster than normal for regtest (144 instead of 2016)
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_MTP].bit = 12;
        consensus.vDeployments[Consensus::DEPLOYMENT_MTP].nStartTime = INT_MAX;
        consensus.vDeployments[Consensus::DEPLOYMENT_MTP].nTimeout = 999999999999ULL;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");
        // Jnode code
        nFulfilledRequestExpireTime = 5*60; // fulfilled requests expire in 5 minutes
        nMaxTipAge = 6 * 60 * 60; // ~144 blocks behind -> 2 x fork detection time, was 24 * 60 * 60 in bitcoin

        consensus.nCheckBugFixedAtBlock = 1;
        consensus.nJnodePaymentsBugFixedAtBlock = 200;
        consensus.nSpendV15StartBlock = 1;
        consensus.nSpendV2ID_1 = 2;
        consensus.nSpendV2ID_10 = 3;
        consensus.nSpendV2ID_25 = 3;
        consensus.nSpendV2ID_50 = 3;
        consensus.nSpendV2ID_100 = 3;
        consensus.nModulusV2StartBlock = 130;
        consensus.nModulusV1MempoolStopBlock = 135;
        consensus.nModulusV1StopBlock = 140;
        consensus.nMultipleSpendInputsInOneTxStartBlock = 1;
        consensus.nDontAllowDupTxsStartBlock = 1;

        consensus.nMTPSwitchTime = INT_MAX;
        consensus.nMTPFiveMinutesStartBlock = 0;
        consensus.nDifficultyAdjustStartBlock = 5000;
        consensus.nFixedDifficulty = 0x2000ffff;
        consensus.nPowTargetSpacingMTP = 5*60;
        consensus.nInitialMTPDifficulty = 0x2070ffff;  // !!!! change it to the real value
        consensus.nMTPRewardReduction = 1;

        pchMessageStart[0] = 0xcb;
        pchMessageStart[1] = 0xae;
        pchMessageStart[2] = 0xc2;
        pchMessageStart[3] = 0x12;
        nDefaultPort = 2821;
        nPruneAfterHeight = 1000;

        /**
          * btzc: testnet params
          * nTime: 1414776313
          * nNonce: 1620571
          */
        std::vector<unsigned char> extraNonce(4);
        extraNonce[0] = 0x08;
        extraNonce[1] = 0x00;
        extraNonce[2] = 0x00;
        extraNonce[3] = 0x00;
        genesis = CreateGenesisBlock(JC_GENESIS_BLOCK_TIME, 414098459, 0x207fffff, 1, 0 * COIN, extraNonce);
        consensus.hashGenesisBlock = genesis.GetHash();
        //btzc: update regtest jemcash hashGenesisBlock and hashMerkleRoot
//        std::cout << "jemcash regtest genesisBlock hash: " << consensus.hashGenesisBlock.ToString() << std::endl;
//        std::cout << "jemcash regtest hashMerkleRoot hash: " << genesis.hashMerkleRoot.ToString() << std::endl;
        //btzc: update testnet jemcash hashGenesisBlock and hashMerkleRoot
        //assert(consensus.hashGenesisBlock ==
        //       uint256S("0x0080c7bf30bb2579ed9c93213475bf8fafc1f53807da908cde19cf405b9eb55b"));
        //assert(genesis.hashMerkleRoot ==
        //       uint256S("0x25b361d60bc7a66b311e72389bf5d9add911c735102bcb6425f63aceeff5b7b8"));

        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();      //!< Regtest mode doesn't have any DNS seeds.

        fMiningRequiresPeers = false;
        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = true;
        fTestnetToBeDeprecatedFieldRPC = false;

        checkpointData = (CCheckpointData) {
                boost::assign::map_list_of
                        (0, uint256S("0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206")),
                0,
                0,
                0
        };
        base58Prefixes[PUBKEY_ADDRESS] = std::vector < unsigned char > (1, 65);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector < unsigned char > (1, 178);
        base58Prefixes[SECRET_KEY] = std::vector < unsigned char > (1, 239);
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x35)(0x87)(0xCF).convert_to_container < std::vector < unsigned char > > ();
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x35)(0x83)(0x94).convert_to_container < std::vector < unsigned char > > ();
    }

    void UpdateBIP9Parameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout) {
        consensus.vDeployments[d].nStartTime = nStartTime;
        consensus.vDeployments[d].nTimeout = nTimeout;
    }
};

static CRegTestParams regTestParams;

static CChainParams *pCurrentParams = 0;

const CChainParams &Params() {
    assert(pCurrentParams);
    return *pCurrentParams;
}

CChainParams &Params(const std::string &chain) {
    if (chain == CBaseChainParams::MAIN)
        return mainParams;
    else if (chain == CBaseChainParams::TESTNET)
        return testNetParams;
    else if (chain == CBaseChainParams::REGTEST)
        return regTestParams;
    else
        throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string &network) {
    SelectBaseParams(network);
    pCurrentParams = &Params(network);
}

void UpdateRegtestBIP9Parameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout) {
    regTestParams.UpdateBIP9Parameters(d, nStartTime, nTimeout);
}
 
