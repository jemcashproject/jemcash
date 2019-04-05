// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "primitives/block.h"
#include "consensus/consensus.h"
#include "main.h"
#include "zerocoin.h"
#include "hash.h"
#include "tinyformat.h"
#include "utilstrencodings.h"
#include "crypto/common.h"
#include "chainparams.h"
#include "crypto/scrypt.h"
#include "crypto/MerkleTreeProof/mtp.h"
#include "util.h"
#include <iostream>
#include <chrono>
#include <fstream>
#include <algorithm>
#include <string>
#include "precomputed_hash.h"



unsigned char GetNfactor(int64_t nTimestamp) {
    int l = 0;
    if (nTimestamp <= Params(CBaseChainParams::MAIN).GetConsensus().nChainStartTime)
        return Params(CBaseChainParams::MAIN).GetConsensus().nMinNFactor;

    int64_t s = nTimestamp - Params(CBaseChainParams::MAIN).GetConsensus().nChainStartTime;
    while ((s >> 1) > 3) {
        l += 1;
        s >>= 1;
    }
    s &= 3;
    int n = (l * 158 + s * 28 - 2670) / 100;
    if (n < 0) n = 0;
    if (n > 255)
        LogPrintf("GetNfactor(%d) - something wrong(n == %d)\n", nTimestamp, n);

    unsigned char N = (unsigned char) n;

    return std::min(std::max(N, Params(CBaseChainParams::MAIN).GetConsensus().nMinNFactor), Params(CBaseChainParams::MAIN).GetConsensus().nMaxNFactor);
}  

uint256 CBlockHeader::GetHash() const {
    return SerializeHash(*this);
}

bool CBlockHeader::IsMTP() const {
    // In case if nTime == JC_GENESIS_BLOCK_TIME we're being called from CChainParams() constructor and
    // it is not possible to get Params()
    return nTime > JC_GENESIS_BLOCK_TIME && nTime >= Params().GetConsensus().nMTPSwitchTime;
}

uint256 CBlockHeader::GetPoWHash(int nHeight, bool forceCalc) const {
//    int64_t start = std::chrono::duration_cast<std::chrono::milliseconds>(
//            std::chrono::system_clock::now().time_since_epoch()).count();
    bool fTestNet = (Params().NetworkIDString() == CBaseChainParams::TESTNET);
    if (!fTestNet) {
        if (!forceCalc && mapPoWHash.count(nHeight)) {
//        std::cout << "GetPowHash nHeight=" << nHeight << ", hash= " << mapPoWHash[nHeight].ToString() << std::endl;
            return mapPoWHash[nHeight];
        }
    }
    uint256 powHash;
    // Jemcash - MTP
    try {
    	if (IsMTP()) {
            powHash = mtpHashValue;
		}else {
            scrypt_N_1_1_256(BEGIN(nVersion), BEGIN(powHash), GetNfactor(nTime));
        } 
    } catch (std::exception &e) {
        LogPrintf("exception: %s", e.what());
    }
//    int64_t end = std::chrono::duration_cast<std::chrono::milliseconds>(
//            std::chrono::system_clock::now().time_since_epoch()).count();
//    std::cout << "GetPowHash nHeight=" << nHeight << ", hash= " << powHash.ToString() << " done in= " << (end - start) << " miliseconds" << std::endl;
    mapPoWHash.insert(make_pair(nHeight, powHash));
//    SetPoWHash(thash);
    return powHash;
}

void CBlockHeader::InvalidateCachedPoWHash(int nHeight) const {
	if (mapPoWHash.count(nHeight) > 0)
        mapPoWHash.erase(nHeight);
}

std::string CBlock::ToString() const {
    std::stringstream s;
    s << strprintf(
            "CBlock(hash=%s, ver=0x%08x, hashPrevBlock=%s, hashMerkleRoot=%s, nTime=%u, nBits=%08x, nNonce=%u, vtx=%u)\n",
            GetHash().ToString(),
            nVersion,
            hashPrevBlock.ToString(),
            hashMerkleRoot.ToString(),
            nTime, nBits, nNonce,
            vtx.size());
    for (unsigned int i = 0; i < vtx.size(); i++) {
        s << "  " << vtx[i].ToString() << "\n";
    }
    return s.str();
}
int64_t GetBlockWeight(const CBlock& block)
{
//     This implements the weight = (stripped_size * 4) + witness_size formula,
//     using only serialization with and without witness data. As witness_size
//     is equal to total_size - stripped_size, this formula is identical to:
//     weight = (stripped_size * 3) + total_size.
//    return ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS) * (WITNESS_SCALE_FACTOR - 1) + ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION);
    return ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION);
}

void CBlock::ZerocoinClean() const {
    //if (zerocoinTxInfo != NULL)
        //delete zerocoinTxInfo; No need since now it is a shared ptr
    zerocoinTxInfo = NULL;
}
