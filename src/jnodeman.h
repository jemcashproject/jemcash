// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef JNODEMAN_H
#define JNODEMAN_H

#include "jnode.h"
#include "sync.h"

using namespace std;

class CJnodeMan;

extern CJnodeMan mnodeman;

/**
 * Provides a forward and reverse index between MN vin's and integers.
 *
 * This mapping is normally add-only and is expected to be permanent
 * It is only rebuilt if the size of the index exceeds the expected maximum number
 * of MN's and the current number of known MN's.
 *
 * The external interface to this index is provided via delegation by CJnodeMan
 */
class CJnodeIndex
{
public: // Types
    typedef std::map<CTxIn,int> index_m_t;

    typedef index_m_t::iterator index_m_it;

    typedef index_m_t::const_iterator index_m_cit;

    typedef std::map<int,CTxIn> rindex_m_t;

    typedef rindex_m_t::iterator rindex_m_it;

    typedef rindex_m_t::const_iterator rindex_m_cit;

private:
    int                  nSize;

    index_m_t            mapIndex;

    rindex_m_t           mapReverseIndex;

public:
    CJnodeIndex();

    int GetSize() const {
        return nSize;
    }

    /// Retrieve jnode vin by index
    bool Get(int nIndex, CTxIn& vinJnode) const;

    /// Get index of a jnode vin
    int GetJnodeIndex(const CTxIn& vinJnode) const;

    void AddJnodeVIN(const CTxIn& vinJnode);

    void Clear();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(mapIndex);
        if(ser_action.ForRead()) {
            RebuildIndex();
        }
    }

private:
    void RebuildIndex();

};

class CJnodeMan
{
public:
    typedef std::map<CTxIn,int> index_m_t;

    typedef index_m_t::iterator index_m_it;

    typedef index_m_t::const_iterator index_m_cit;

private:
    static const int MAX_EXPECTED_INDEX_SIZE = 30000;

    /// Only allow 1 index rebuild per hour
    static const int64_t MIN_INDEX_REBUILD_TIME = 3600;

    static const std::string SERIALIZATION_VERSION_STRING;

    static const int DSEG_UPDATE_SECONDS        = 3 * 60 * 60;

    static const int LAST_PAID_SCAN_BLOCKS      = 100;

    static const int MIN_POSE_PROTO_VERSION     = 70203;
    static const int MAX_POSE_CONNECTIONS       = 10;
    static const int MAX_POSE_RANK              = 10;
    static const int MAX_POSE_BLOCKS            = 10;

    static const int MNB_RECOVERY_QUORUM_TOTAL      = 10;
    static const int MNB_RECOVERY_QUORUM_REQUIRED   = 6;
    static const int MNB_RECOVERY_MAX_ASK_ENTRIES   = 10;
    static const int MNB_RECOVERY_WAIT_SECONDS      = 60;
    static const int MNB_RECOVERY_RETRY_SECONDS     = 3 * 60 * 60;


    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    // Keep track of current block index
    const CBlockIndex *pCurrentBlockIndex;

    // map to hold all MNs
    std::vector<CJnode> vJnodes;
    // who's asked for the Jnode list and the last time
    std::map<CNetAddr, int64_t> mAskedUsForJnodeList;
    // who we asked for the Jnode list and the last time
    std::map<CNetAddr, int64_t> mWeAskedForJnodeList;
    // which Jnodes we've asked for
    std::map<COutPoint, std::map<CNetAddr, int64_t> > mWeAskedForJnodeListEntry;
    // who we asked for the jnode verification
    std::map<CNetAddr, CJnodeVerification> mWeAskedForVerification;

    // these maps are used for jnode recovery from JNODE_NEW_START_REQUIRED state
    std::map<uint256, std::pair< int64_t, std::set<CNetAddr> > > mMnbRecoveryRequests;
    std::map<uint256, std::vector<CJnodeBroadcast> > mMnbRecoveryGoodReplies;
    std::list< std::pair<CService, uint256> > listScheduledMnbRequestConnections;

    int64_t nLastIndexRebuildTime;

    CJnodeIndex indexJnodes;

    CJnodeIndex indexJnodesOld;

    /// Set when index has been rebuilt, clear when read
    bool fIndexRebuilt;

    /// Set when jnodes are added, cleared when CGovernanceManager is notified
    bool fJnodesAdded;

    /// Set when jnodes are removed, cleared when CGovernanceManager is notified
    bool fJnodesRemoved;

    std::vector<uint256> vecDirtyGovernanceObjectHashes;

    int64_t nLastWatchdogVoteTime;

    friend class CJnodeSync;

public:
    // Keep track of all broadcasts I've seen
    std::map<uint256, std::pair<int64_t, CJnodeBroadcast> > mapSeenJnodeBroadcast;
    // Keep track of all pings I've seen
    std::map<uint256, CJnodePing> mapSeenJnodePing;
    // Keep track of all verifications I've seen
    std::map<uint256, CJnodeVerification> mapSeenJnodeVerification;
    // keep track of dsq count to prevent jnodes from gaming darksend queue
    int64_t nDsqCount;


    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        LOCK(cs);
        std::string strVersion;
        if(ser_action.ForRead()) {
            READWRITE(strVersion);
        }
        else {
            strVersion = SERIALIZATION_VERSION_STRING; 
            READWRITE(strVersion);
        }

        READWRITE(vJnodes);
        READWRITE(mAskedUsForJnodeList);
        READWRITE(mWeAskedForJnodeList);
        READWRITE(mWeAskedForJnodeListEntry);
        READWRITE(mMnbRecoveryRequests);
        READWRITE(mMnbRecoveryGoodReplies);
        READWRITE(nLastWatchdogVoteTime);
        READWRITE(nDsqCount);

        READWRITE(mapSeenJnodeBroadcast);
        READWRITE(mapSeenJnodePing);
        READWRITE(indexJnodes);
        if(ser_action.ForRead() && (strVersion != SERIALIZATION_VERSION_STRING)) {
            Clear();
        }
    }

    CJnodeMan();

    /// Add an entry
    bool Add(CJnode &mn);

    /// Ask (source) node for mnb
    void AskForMN(CNode *pnode, const CTxIn &vin);
    void AskForMnb(CNode *pnode, const uint256 &hash);

    /// Check all Jnodes
    void Check();

    /// Check all Jnodes and remove inactive
    void CheckAndRemove();

    /// Clear Jnode vector
    void Clear();

    /// Count Jnodes filtered by nProtocolVersion.
    /// Jnode nProtocolVersion should match or be above the one specified in param here.
    int CountJnodes(int nProtocolVersion = -1);
    /// Count enabled Jnodes filtered by nProtocolVersion.
    /// Jnode nProtocolVersion should match or be above the one specified in param here.
    int CountEnabled(int nProtocolVersion = -1);

    /// Count Jnodes by network type - NET_IPV4, NET_IPV6, NET_TOR
    // int CountByIP(int nNetworkType);

    void DsegUpdate(CNode* pnode);

    /// Find an entry
    CJnode* Find(const CScript &payee);
    CJnode* Find(const CTxIn& vin);
    CJnode* Find(const CPubKey& pubKeyJnode);

    /// Versions of Find that are safe to use from outside the class
    bool Get(const CPubKey& pubKeyJnode, CJnode& jnode);
    bool Get(const CTxIn& vin, CJnode& jnode);

    /// Retrieve jnode vin by index
    bool Get(int nIndex, CTxIn& vinJnode, bool& fIndexRebuiltOut) {
        LOCK(cs);
        fIndexRebuiltOut = fIndexRebuilt;
        return indexJnodes.Get(nIndex, vinJnode);
    }

    bool GetIndexRebuiltFlag() {
        LOCK(cs);
        return fIndexRebuilt;
    }

    /// Get index of a jnode vin
    int GetJnodeIndex(const CTxIn& vinJnode) {
        LOCK(cs);
        return indexJnodes.GetJnodeIndex(vinJnode);
    }

    /// Get old index of a jnode vin
    int GetJnodeIndexOld(const CTxIn& vinJnode) {
        LOCK(cs);
        return indexJnodesOld.GetJnodeIndex(vinJnode);
    }

    /// Get jnode VIN for an old index value
    bool GetJnodeVinForIndexOld(int nJnodeIndex, CTxIn& vinJnodeOut) {
        LOCK(cs);
        return indexJnodesOld.Get(nJnodeIndex, vinJnodeOut);
    }

    /// Get index of a jnode vin, returning rebuild flag
    int GetJnodeIndex(const CTxIn& vinJnode, bool& fIndexRebuiltOut) {
        LOCK(cs);
        fIndexRebuiltOut = fIndexRebuilt;
        return indexJnodes.GetJnodeIndex(vinJnode);
    }

    void ClearOldJnodeIndex() {
        LOCK(cs);
        indexJnodesOld.Clear();
        fIndexRebuilt = false;
    }

    bool Has(const CTxIn& vin);

    jnode_info_t GetJnodeInfo(const CTxIn& vin);

    jnode_info_t GetJnodeInfo(const CPubKey& pubKeyJnode);

    char* GetNotQualifyReason(CJnode& mn, int nBlockHeight, bool fFilterSigTime, int nMnCount);

    /// Find an entry in the jnode list that is next to be paid
    CJnode* GetNextJnodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount);
    /// Same as above but use current block height
    CJnode* GetNextJnodeInQueueForPayment(bool fFilterSigTime, int& nCount);

    /// Find a random entry
    CJnode* FindRandomNotInVec(const std::vector<CTxIn> &vecToExclude, int nProtocolVersion = -1);

    std::vector<CJnode> GetFullJnodeVector() { return vJnodes; }

    std::vector<std::pair<int, CJnode> > GetJnodeRanks(int nBlockHeight = -1, int nMinProtocol=0);
    int GetJnodeRank(const CTxIn &vin, int nBlockHeight, int nMinProtocol=0, bool fOnlyActive=true);
    CJnode* GetJnodeByRank(int nRank, int nBlockHeight, int nMinProtocol=0, bool fOnlyActive=true);

    void ProcessJnodeConnections();
    std::pair<CService, std::set<uint256> > PopScheduledMnbRequestConnection();

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);

    void DoFullVerificationStep();
    void CheckSameAddr();
    bool SendVerifyRequest(const CAddress& addr, const std::vector<CJnode*>& vSortedByAddr);
    void SendVerifyReply(CNode* pnode, CJnodeVerification& mnv);
    void ProcessVerifyReply(CNode* pnode, CJnodeVerification& mnv);
    void ProcessVerifyBroadcast(CNode* pnode, const CJnodeVerification& mnv);

    /// Return the number of (unique) Jnodes
    int size() { return vJnodes.size(); }

    std::string ToString() const;

    /// Update jnode list and maps using provided CJnodeBroadcast
    void UpdateJnodeList(CJnodeBroadcast mnb);
    /// Perform complete check and only then update list and maps
    bool CheckMnbAndUpdateJnodeList(CNode* pfrom, CJnodeBroadcast mnb, int& nDos);
    bool IsMnbRecoveryRequested(const uint256& hash) { return mMnbRecoveryRequests.count(hash); }

    void UpdateLastPaid();

    void CheckAndRebuildJnodeIndex();

    void AddDirtyGovernanceObjectHash(const uint256& nHash)
    {
        LOCK(cs);
        vecDirtyGovernanceObjectHashes.push_back(nHash);
    }

    std::vector<uint256> GetAndClearDirtyGovernanceObjectHashes()
    {
        LOCK(cs);
        std::vector<uint256> vecTmp = vecDirtyGovernanceObjectHashes;
        vecDirtyGovernanceObjectHashes.clear();
        return vecTmp;;
    }

    bool IsWatchdogActive();
    void UpdateWatchdogVoteTime(const CTxIn& vin);
    bool AddGovernanceVote(const CTxIn& vin, uint256 nGovernanceObjectHash);
    void RemoveGovernanceObject(uint256 nGovernanceObjectHash);

    void CheckJnode(const CTxIn& vin, bool fForce = false);
    void CheckJnode(const CPubKey& pubKeyJnode, bool fForce = false);

    int GetJnodeState(const CTxIn& vin);
    int GetJnodeState(const CPubKey& pubKeyJnode);

    bool IsJnodePingedWithin(const CTxIn& vin, int nSeconds, int64_t nTimeToCheckAt = -1);
    void SetJnodeLastPing(const CTxIn& vin, const CJnodePing& mnp);

    void UpdatedBlockTip(const CBlockIndex *pindex);

    /**
     * Called to notify CGovernanceManager that the jnode index has been updated.
     * Must be called while not holding the CJnodeMan::cs mutex
     */
    void NotifyJnodeUpdates();

};

#endif
