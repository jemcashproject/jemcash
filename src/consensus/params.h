// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_PARAMS_H
#define BITCOIN_CONSENSUS_PARAMS_H

#include "uint256.h"
#include <map>
#include <string>

namespace Consensus {

enum DeploymentPos
{
    DEPLOYMENT_TESTDUMMY,
    DEPLOYMENT_CSV, // Deployment of BIP68, BIP112, and BIP113.
    DEPLOYMENT_SEGWIT, // Deployment of BIP141, BIP143, and BIP147.

    DEPLOYMENT_MTP, // Deployment of MTP

    // NOTE: Also add new deployments to VersionBitsDeploymentInfo in versionbits.cpp
    MAX_VERSION_BITS_DEPLOYMENTS
};

/**
 * Struct for each individual consensus rule change using BIP9.
 */
struct BIP9Deployment {
    /** Bit position to select the particular bit in nVersion. */
    int bit;
    /** Start MedianTime for version bits miner confirmation. Can be a date in the past */
    int64_t nStartTime;
    /** Timeout/expiry MedianTime for the deployment attempt. */
    int64_t nTimeout;
};

/**
 * Type of chain
 */
enum ChainType {
    chainMain,
    chainTestnet,
    chainRegtest
};

/**
 * Parameters that influence chain consensus.
 */
struct Params {
    ChainType chainType;

    uint256 hashGenesisBlock;
    int nSubsidyHalvingInterval;
    /** Used to check majorities for block version upgrade */
    int nMajorityEnforceBlockUpgrade;
    int nMajorityRejectBlockOutdated;
    int nMajorityWindow;
    /** Block height and hash at which BIP34 becomes active */
   // int BIP34Height;
   // uint256 BIP34Hash;
    /**
     * Minimum blocks including miner confirmation of the total of 2016 blocks in a retargetting period,
     * (nPowTargetTimespan / nPowTargetSpacing) which is also used for BIP9 deployments.
     * Examples: 1916 for 95%, 1512 for testchains.
     */
    uint32_t nRuleChangeActivationThreshold;
    uint32_t nMinerConfirmationWindow;
    BIP9Deployment vDeployments[MAX_VERSION_BITS_DEPLOYMENTS];
    /** Proof of work parameters */
    uint256 powLimit;
    bool fPowAllowMinDifficultyBlocks;
    bool fPowNoRetargeting;
    int64_t nPowTargetSpacing;
    int64_t nPowTargetTimespan;
    int64_t nChainStartTime;
    unsigned char nMinNFactor;
    unsigned char nMaxNFactor;
    int nInstantSendKeepLock; // in blocks
    //int nBudgetPaymentsStartBlock;
    //int nBudgetPaymentsCycleBlocks;
    //int nBudgetPaymentsWindowBlocks;
    int nJnodeMinimumConfirmations;
    int nJnodePaymentsStartBlock;
    //int nJnodePaymentsIncreaseBlock;
    //int nJnodePaymentsIncreasePeriod; // in blocks
    //int nSuperblockStartBlock;

	/** Zerocoin-related block numbers when features are changed */
    int nCheckBugFixedAtBlock;
    int nJnodePaymentsBugFixedAtBlock;
	int nSpendV15StartBlock;
	int nSpendV2ID_1, nSpendV2ID_10, nSpendV2ID_25, nSpendV2ID_50, nSpendV2ID_100;
	
	int nModulusV2StartBlock;
    int nModulusV1MempoolStopBlock;
	int nModulusV1StopBlock;
    
    int nMultipleSpendInputsInOneTxStartBlock;

    int nDontAllowDupTxsStartBlock;

    /** switch to MTP time */
    uint32_t nMTPSwitchTime;
    /** block number to reduce distance between blocks */
    int nMTPFiveMinutesStartBlock;

    /** don't adjust difficulty until some block number */
    int nDifficultyAdjustStartBlock;
    /** fixed diffuculty to use before adjustment takes place */
    int nFixedDifficulty;

    /** pow target spacing after switch to MTP */
    int64_t nPowTargetSpacingMTP;

    /** initial MTP difficulty */
    int nInitialMTPDifficulty;

    /** reduction coefficient for rewards after MTP kicks in */
    int nMTPRewardReduction;
	
    int64_t DifficultyAdjustmentInterval(bool fMTP = false) const { return nPowTargetTimespan / (fMTP ? nPowTargetSpacingMTP : nPowTargetSpacing); }
    uint256 nMinimumChainWork;

    bool IsMain() const { return chainType == chainMain; }
    bool IsTestnet() const { return chainType == chainTestnet; }
    bool IsRegtest() const { return chainType == chainRegtest; }
};
} // namespace Consensus

#endif // BITCOIN_CONSENSUS_PARAMS_H
