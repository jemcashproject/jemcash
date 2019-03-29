#include "exodus/exodus.h"
#include "exodus/rules.h"

#include "base58.h"
#include "chainparams.h"
#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

#include <limits>

using namespace exodus;

BOOST_FIXTURE_TEST_SUITE(exodus_exodus_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(exodus_address_mainnet)
{
    BOOST_CHECK(CBitcoinAddress("ZzzcQkPmXomcTcSVGsDHsGBCvxg67joaj5") ==
                ExodusAddress());
}

BOOST_AUTO_TEST_CASE(exodus_address_testnet)
{
    SelectParams(CBaseChainParams::TESTNET);

    BOOST_CHECK(CBitcoinAddress("TTFL4sPFHP22Dzqbw9mPQJEjdG7Wf1ajjZ") ==
                ExodusAddress());

    SelectParams(CBaseChainParams::MAIN);
}

BOOST_AUTO_TEST_CASE(exodus_address_regtest)
{
    SelectParams(CBaseChainParams::REGTEST);

    BOOST_CHECK(CBitcoinAddress("TTFL4sPFHP22Dzqbw9mPQJEjdG7Wf1ajjZ") ==
                ExodusAddress());

    SelectParams(CBaseChainParams::MAIN);
}


BOOST_AUTO_TEST_SUITE_END()
