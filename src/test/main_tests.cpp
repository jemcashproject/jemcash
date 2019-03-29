// Copyright (c) 2014-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "main.h"

#include "test/test_bitcoin.h"

#include <boost/signals2/signal.hpp>
#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(main_tests, TestingSetup)

static void TestBlockSubsidyHalvings(const Consensus::Params& consensusParams)
{
    int maxHalvings = 64;
    CAmount nInitialSubsidy = 50 * COIN;

    CAmount nPreviousSubsidy = nInitialSubsidy * 2; // for height == 0
    BOOST_CHECK_EQUAL(nPreviousSubsidy, nInitialSubsidy * 2);
    for (int nHalvings = 0; nHalvings < maxHalvings; nHalvings++) {
        int nHeight = nHalvings * consensusParams.nSubsidyHalvingInterval;
        CAmount nSubsidy = GetBlockSubsidy(nHeight, consensusParams, 1475020800);
        BOOST_CHECK(nSubsidy <= nInitialSubsidy);
        if(nHeight > 0)
            BOOST_CHECK_EQUAL(nSubsidy, nPreviousSubsidy / 2);
        nPreviousSubsidy = nPreviousSubsidy / 2;
    }
    BOOST_CHECK_EQUAL(GetBlockSubsidy(maxHalvings * consensusParams.nSubsidyHalvingInterval, consensusParams), 0);
}

static void TestBlockSubsidyHalvings(int nSubsidyHalvingInterval)
{
    Consensus::Params consensusParams;
    consensusParams.nMTPSwitchTime = INT_MAX;
    consensusParams.nSubsidyHalvingInterval = nSubsidyHalvingInterval;
    TestBlockSubsidyHalvings(consensusParams);
}

BOOST_AUTO_TEST_CASE(block_subsidy_test)
{
    TestBlockSubsidyHalvings(Params(CBaseChainParams::MAIN).GetConsensus()); // As in main
    TestBlockSubsidyHalvings(105000); // As in regtest
    //TestBlockSubsidyHalvings(1000); // Just another interval
}

BOOST_AUTO_TEST_CASE(subsidy_limit_test)
{
    Consensus::Params consensusParams = Params(CBaseChainParams::MAIN).GetConsensus();
    CAmount nSum = 0;
    int const mtpReleaseHeight = 110725
        //The MTP switch time is December 10th at 12:00 UTC.
        //The block height of MTP switch cannot be calculated firmly, but can only be approximated.
        //Below is one of such approximations which is used for this test only.
        //This approximation influences the check at the end of the test.
        , mtpActivationHeight = 117560;

    int nHeight = 0;
    int step = 1;

    consensusParams.nSubsidyHalvingInterval = 210000;
    for(; nHeight < mtpReleaseHeight; nHeight += step)
    {
        CAmount nSubsidy = GetBlockSubsidy(nHeight, consensusParams);
        if(nHeight == 0)
            nSubsidy = 50 * COIN;
        BOOST_CHECK(nSubsidy <= 50 * COIN);
        nSum += nSubsidy * step;
        BOOST_CHECK(MoneyRange(nSum));
    }
    BOOST_CHECK_EQUAL(nSum, 553625000000000ULL);
    
    consensusParams.nSubsidyHalvingInterval = 105000;
    for(; nHeight < mtpActivationHeight; nHeight += step)
    {
        CAmount nSubsidy = GetBlockSubsidy(nHeight, consensusParams);
        BOOST_CHECK(nSubsidy <= 50 * COIN);
        nSum += nSubsidy * step;
        BOOST_CHECK(MoneyRange(nSum));
    }
    BOOST_CHECK_EQUAL(nSum, 587800000000000ULL);

    step = 1000;
    for(; nHeight < 14000000; nHeight += step)
    {
        CAmount nSubsidy = GetBlockSubsidy(nHeight, consensusParams, consensusParams.nMTPSwitchTime);
        BOOST_CHECK(nSubsidy <= 50 * COIN);
        nSum += nSubsidy * step;
        BOOST_CHECK(MoneyRange(nSum));
    }
    //The final check value is changed due to the approximation of mtpActivationHeight
    BOOST_CHECK_EQUAL(nSum, 1820299996645000ULL);
}

bool ReturnFalse() { return false; }
bool ReturnTrue() { return true; }

BOOST_AUTO_TEST_CASE(test_combiner_all)
{
    boost::signals2::signal<bool (), CombinerAll> Test;
    BOOST_CHECK(Test());
    Test.connect(&ReturnFalse);
    BOOST_CHECK(!Test());
    Test.connect(&ReturnTrue);
    BOOST_CHECK(!Test());
    Test.disconnect(&ReturnFalse);
    BOOST_CHECK(Test());
    Test.disconnect(&ReturnTrue);
    BOOST_CHECK(Test());
}
BOOST_AUTO_TEST_SUITE_END()
