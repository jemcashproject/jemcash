// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_PRIMITIVES_BLOCK_H
#define BITCOIN_PRIMITIVES_BLOCK_H

#include <deque>
#include <type_traits>
#include <boost/foreach.hpp>
#include "primitives/transaction.h"
#include "serialize.h"
#include "uint256.h"
#include "definition.h"
#include "crypto/MerkleTreeProof/mtp.h"
#include "zerocoin_params.h"

unsigned char GetNfactor(int64_t nTimestamp);

/** Nodes collect new transactions into a block, hash them into a hash tree,
 * and scan through nonce values to make the block's hash satisfy proof-of-work
 * requirements.  When they solve the proof-of-work, they broadcast the block
 * to everyone and the block is added to the block chain.  The first transaction
 * in the block is a special one that creates a new coin owned by the creator
 * of the block.
 */

inline int GetZerocoinChainID()
{
    return 0x0001; // We are the first :)
}

// Jemcash - MTP
class CMTPHashData {
public:
    uint8_t hashRootMTP[16]; // 16 is 128 bit of blake2b
    uint64_t nBlockMTP[mtp::MTP_L*2][128]; // 128 is ARGON2_QWORDS_IN_BLOCK
    std::deque<std::vector<uint8_t>> nProofMTP[mtp::MTP_L*3];

    CMTPHashData() {
        memset(nBlockMTP, 0, sizeof(nBlockMTP));
    }

    ADD_SERIALIZE_METHODS;

    /**
     * Custom serialization scheme is in place because of speed reasons
     */

    // Function for write/getting size
    template <typename Stream, typename Operation, typename = typename std::enable_if<!std::is_base_of<CSerActionUnserialize, Operation>::value>::type>
    inline void SerializationOp(Stream &s, Operation ser_action, int nType, int nVersion) {
        READWRITE(hashRootMTP);
        READWRITE(nBlockMTP);
        for (int i = 0; i < mtp::MTP_L*3; i++) {
            assert(nProofMTP[i].size() < 256);
            uint8_t numberOfProofBlocks = (uint8_t)nProofMTP[i].size();
            READWRITE(numberOfProofBlocks);
            for (const std::vector<uint8_t> &mtpData: nProofMTP[i]) {
                // data size should be 16 for each block
                assert(mtpData.size() == 16);
                s.write((const char *)mtpData.data(), 16);
            }
        }
    }

    // Function for reading
    template <typename Stream>
    inline void SerializationOp(Stream &s, CSerActionUnserialize ser_action, int nType, int nVersion) {
        READWRITE(hashRootMTP);
        READWRITE(nBlockMTP);
        for (int i = 0; i < mtp::MTP_L*3; i++) {
            uint8_t numberOfProofBlocks;
            READWRITE(numberOfProofBlocks);
            for (uint8_t j=0; j<numberOfProofBlocks; j++) {
                vector<uint8_t> mtpData(16, 0);
                s.read((char *)mtpData.data(), 16);
                nProofMTP[i].emplace_back(std::move(mtpData));
            }
        }
    }
};

class CBlockHeader
{
public:
    // header
    int32_t nVersion;
    uint256 hashPrevBlock;
    uint256 hashMerkleRoot;
    uint32_t nTime;
    uint32_t nBits;
    uint32_t nNonce;

    // Jemcash - MTP
    int32_t nVersionMTP = 0x1000;
    uint256 mtpHashValue;

    // Reserved fields
    uint256 reserved[2];

    // Store this only when absolutely needed for verification
    std::shared_ptr<CMTPHashData> mtpHashData;

    static const int CURRENT_VERSION = 2;

    // uint32_t lastHeight;
    uint256 powHash;
    int32_t isComputed;

    CBlockHeader()
    {
        SetNull();
    }

    ADD_SERIALIZE_METHODS;

    class CSerializeBlockHeader {};
    class CReadBlockHeader : public CSerActionUnserialize, public CSerializeBlockHeader {};
    class CWriteBlockHeader : public CSerActionSerialize, public CSerializeBlockHeader {};

    template <typename Stream, typename Operation, typename = typename std::enable_if<!std::is_base_of<CSerializeBlockHeader,Operation>::value>::type>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(this->nVersion);
        READWRITE(hashPrevBlock);
        READWRITE(hashMerkleRoot);
        READWRITE(nTime);
        READWRITE(nBits);
        READWRITE(nNonce);
        // Jemcash - MTP
        // On read: allocate and read. On write: write only if already allocated
        if (IsMTP()) {
            READWRITE(nVersionMTP);
            READWRITE(mtpHashValue);
            READWRITE(reserved[0]);
            READWRITE(reserved[1]);
            if (ser_action.ForRead()) {
                mtpHashData = make_shared<CMTPHashData>();
                READWRITE(*mtpHashData);
            }
            else {
                if (mtpHashData && !(nType & SER_GETHASH))
                    READWRITE(*mtpHashData);
            }
        }
    }

    template <typename Stream>
    inline void SerializationOp(Stream &s, CReadBlockHeader ser_action, int nType, int) {
        READWRITE(this->nVersion);
        READWRITE(hashPrevBlock);
        READWRITE(hashMerkleRoot);
        READWRITE(nTime);
        READWRITE(nBits);
        READWRITE(nNonce);
        if (IsMTP()) {
            READWRITE(nVersionMTP);
            READWRITE(mtpHashValue);
            READWRITE(reserved[0]);
            READWRITE(reserved[1]);
        }
    }

    void SetNull()
    {
        nVersion = CBlockHeader::CURRENT_VERSION | (GetZerocoinChainID() * BLOCK_VERSION_CHAIN_START);
        hashPrevBlock.SetNull();
        hashMerkleRoot.SetNull();
        nTime = 0;
        nBits = 0;
        nNonce = 0;
        isComputed = -1;
        powHash.SetNull();

        // Jemcash - MTP
        mtpHashData.reset();
        mtpHashValue.SetNull();
        reserved[0].SetNull();
        reserved[1].SetNull();
    }

    int GetChainID() const
    {
        return nVersion / BLOCK_VERSION_CHAIN_START;
    }

    bool IsNull() const
    {
        return (nBits == 0);
    }

    bool IsComputed() const
    {
        return (isComputed <= 0);
    }

    void SetPoWHash(uint256 hash) const
    {
//        isComputed = 1;
//        powHash = hash;
    }

    uint256 GetPoWHash(int nHeight, bool forceCalc = false) const;

    uint256 GetHash() const;

    int64_t GetBlockTime() const
    {
        return (int64_t)nTime;
    }

    void InvalidateCachedPoWHash(int nHeight) const;

    bool IsMTP() const;
};

class CZerocoinTxInfo;

class CBlock : public CBlockHeader
{
public:
    // network and disk
    std::vector<CTransaction> vtx;

    // memory only
    mutable CTxOut txoutJnode; // jnode payment
    mutable std::vector<CTxOut> voutSuperblock; // superblock payment
    mutable bool fChecked;

    // memory only, zerocoin tx info
    mutable std::shared_ptr<CZerocoinTxInfo> zerocoinTxInfo;

    CBlock()
    {
        zerocoinTxInfo = NULL;
        SetNull();
    }

    CBlock(const CBlockHeader &header)
    {
        zerocoinTxInfo = NULL;
        SetNull();
        *((CBlockHeader*)this) = header;
    }

    ~CBlock() {
        ZerocoinClean();
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(*(CBlockHeader*)this);
        READWRITE(vtx);
    }

    template <typename Stream>
    inline void SerializationOp(Stream &s, CReadBlockHeader ser_action, int nType, int nVersion) {
        CBlockHeader::SerializationOp(s, ser_action, nType, nVersion);
    }

    void SetNull()
    {
        ZerocoinClean();
        CBlockHeader::SetNull();
        vtx.clear();
        txoutJnode = CTxOut();
        voutSuperblock.clear();
        fChecked = false;
    }

    CBlockHeader GetBlockHeader() const
    {
        CBlockHeader block;
        block.nVersion       = nVersion;
        block.hashPrevBlock  = hashPrevBlock;
        block.hashMerkleRoot = hashMerkleRoot;
        block.nTime          = nTime;
        block.nBits          = nBits;
        block.nNonce         = nNonce;
        if (block.IsMTP()) {
            block.nVersionMTP = nVersionMTP;
            block.mtpHashData = mtpHashData;
            block.reserved[0] = reserved[0];
            block.reserved[1] = reserved[1];
        }
        return block;
    }

    std::string ToString() const;

    void ZerocoinClean() const;
};

/** Describes a place in the block chain to another node such that if the
 * other node doesn't have the same branch, it can find a recent common trunk.
 * The further back it is, the further before the fork it may be.
 */
struct CBlockLocator
{
    std::vector<uint256> vHave;

    CBlockLocator() {}

    CBlockLocator(const std::vector<uint256>& vHaveIn)
    {
        vHave = vHaveIn;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        if (!(nType & SER_GETHASH))
            READWRITE(nVersion);
        READWRITE(vHave);
    }

    void SetNull()
    {
        vHave.clear();
    }

    bool IsNull() const
    {
        return vHave.empty();
    }
};

/** Compute the consensus-critical block weight (see BIP 141). */
int64_t GetBlockWeight(const CBlock& tx);

#endif // BITCOIN_PRIMITIVES_BLOCK_H
