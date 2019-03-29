// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activejnode.h"
#include "addrman.h"
#include "darksend.h"
//#include "governance.h"
#include "jnode-payments.h"
#include "jnode-sync.h"
#include "jnodeman.h"
#include "netfulfilledman.h"
#include "util.h"

/** Jnode manager */
CJnodeMan mnodeman;

const std::string CJnodeMan::SERIALIZATION_VERSION_STRING = "CJnodeMan-Version-4";

struct CompareLastPaidBlock
{
    bool operator()(const std::pair<int, CJnode*>& t1,
                    const std::pair<int, CJnode*>& t2) const
    {
        return (t1.first != t2.first) ? (t1.first < t2.first) : (t1.second->vin < t2.second->vin);
    }
};

struct CompareScoreMN
{
    bool operator()(const std::pair<int64_t, CJnode*>& t1,
                    const std::pair<int64_t, CJnode*>& t2) const
    {
        return (t1.first != t2.first) ? (t1.first < t2.first) : (t1.second->vin < t2.second->vin);
    }
};

CJnodeIndex::CJnodeIndex()
    : nSize(0),
      mapIndex(),
      mapReverseIndex()
{}

bool CJnodeIndex::Get(int nIndex, CTxIn& vinJnode) const
{
    rindex_m_cit it = mapReverseIndex.find(nIndex);
    if(it == mapReverseIndex.end()) {
        return false;
    }
    vinJnode = it->second;
    return true;
}

int CJnodeIndex::GetJnodeIndex(const CTxIn& vinJnode) const
{
    index_m_cit it = mapIndex.find(vinJnode);
    if(it == mapIndex.end()) {
        return -1;
    }
    return it->second;
}

void CJnodeIndex::AddJnodeVIN(const CTxIn& vinJnode)
{
    index_m_it it = mapIndex.find(vinJnode);
    if(it != mapIndex.end()) {
        return;
    }
    int nNextIndex = nSize;
    mapIndex[vinJnode] = nNextIndex;
    mapReverseIndex[nNextIndex] = vinJnode;
    ++nSize;
}

void CJnodeIndex::Clear()
{
    mapIndex.clear();
    mapReverseIndex.clear();
    nSize = 0;
}
struct CompareByAddr

{
    bool operator()(const CJnode* t1,
                    const CJnode* t2) const
    {
        return t1->addr < t2->addr;
    }
};

void CJnodeIndex::RebuildIndex()
{
    nSize = mapIndex.size();
    for(index_m_it it = mapIndex.begin(); it != mapIndex.end(); ++it) {
        mapReverseIndex[it->second] = it->first;
    }
}

CJnodeMan::CJnodeMan() : cs(),
  vJnodes(),
  mAskedUsForJnodeList(),
  mWeAskedForJnodeList(),
  mWeAskedForJnodeListEntry(),
  mWeAskedForVerification(),
  mMnbRecoveryRequests(),
  mMnbRecoveryGoodReplies(),
  listScheduledMnbRequestConnections(),
  nLastIndexRebuildTime(0),
  indexJnodes(),
  indexJnodesOld(),
  fIndexRebuilt(false),
  fJnodesAdded(false),
  fJnodesRemoved(false),
//  vecDirtyGovernanceObjectHashes(),
  nLastWatchdogVoteTime(0),
  mapSeenJnodeBroadcast(),
  mapSeenJnodePing(),
  nDsqCount(0)
{}

bool CJnodeMan::Add(CJnode &mn)
{
    LOCK(cs);

    CJnode *pmn = Find(mn.vin);
    if (pmn == NULL) {
        LogPrint("jnode", "CJnodeMan::Add -- Adding new Jnode: addr=%s, %i now\n", mn.addr.ToString(), size() + 1);
        vJnodes.push_back(mn);
        indexJnodes.AddJnodeVIN(mn.vin);
        fJnodesAdded = true;
        return true;
    }

    return false;
}

void CJnodeMan::AskForMN(CNode* pnode, const CTxIn &vin)
{
    if(!pnode) return;

    LOCK(cs);

    std::map<COutPoint, std::map<CNetAddr, int64_t> >::iterator it1 = mWeAskedForJnodeListEntry.find(vin.prevout);
    if (it1 != mWeAskedForJnodeListEntry.end()) {
        std::map<CNetAddr, int64_t>::iterator it2 = it1->second.find(pnode->addr);
        if (it2 != it1->second.end()) {
            if (GetTime() < it2->second) {
                // we've asked recently, should not repeat too often or we could get banned
                return;
            }
            // we asked this node for this outpoint but it's ok to ask again already
            LogPrintf("CJnodeMan::AskForMN -- Asking same peer %s for missing jnode entry again: %s\n", pnode->addr.ToString(), vin.prevout.ToStringShort());
        } else {
            // we already asked for this outpoint but not this node
            LogPrintf("CJnodeMan::AskForMN -- Asking new peer %s for missing jnode entry: %s\n", pnode->addr.ToString(), vin.prevout.ToStringShort());
        }
    } else {
        // we never asked any node for this outpoint
        LogPrintf("CJnodeMan::AskForMN -- Asking peer %s for missing jnode entry for the first time: %s\n", pnode->addr.ToString(), vin.prevout.ToStringShort());
    }
    mWeAskedForJnodeListEntry[vin.prevout][pnode->addr] = GetTime() + DSEG_UPDATE_SECONDS;

    pnode->PushMessage(NetMsgType::DSEG, vin);
}

void CJnodeMan::Check()
{
    LOCK(cs);

//    LogPrint("jnode", "CJnodeMan::Check -- nLastWatchdogVoteTime=%d, IsWatchdogActive()=%d\n", nLastWatchdogVoteTime, IsWatchdogActive());

    BOOST_FOREACH(CJnode& mn, vJnodes) {
        mn.Check();
    }
}

void CJnodeMan::CheckAndRemove()
{
    if(!jnodeSync.IsJnodeListSynced()) return;

    LogPrintf("CJnodeMan::CheckAndRemove\n");

    {
        // Need LOCK2 here to ensure consistent locking order because code below locks cs_main
        // in CheckMnbAndUpdateJnodeList()
        LOCK2(cs_main, cs);

        Check();

        // Remove spent jnodes, prepare structures and make requests to reasure the state of inactive ones
        std::vector<CJnode>::iterator it = vJnodes.begin();
        std::vector<std::pair<int, CJnode> > vecJnodeRanks;
        // ask for up to MNB_RECOVERY_MAX_ASK_ENTRIES jnode entries at a time
        int nAskForMnbRecovery = MNB_RECOVERY_MAX_ASK_ENTRIES;
        while(it != vJnodes.end()) {
            CJnodeBroadcast mnb = CJnodeBroadcast(*it);
            uint256 hash = mnb.GetHash();
            // If collateral was spent ...
            if ((*it).IsOutpointSpent()) {
                LogPrint("jnode", "CJnodeMan::CheckAndRemove -- Removing Jnode: %s  addr=%s  %i now\n", (*it).GetStateString(), (*it).addr.ToString(), size() - 1);

                // erase all of the broadcasts we've seen from this txin, ...
                mapSeenJnodeBroadcast.erase(hash);
                mWeAskedForJnodeListEntry.erase((*it).vin.prevout);

                // and finally remove it from the list
//                it->FlagGovernanceItemsAsDirty();
                it = vJnodes.erase(it);
                fJnodesRemoved = true;
            } else {
                bool fAsk = pCurrentBlockIndex &&
                            (nAskForMnbRecovery > 0) &&
                            jnodeSync.IsSynced() &&
                            it->IsNewStartRequired() &&
                            !IsMnbRecoveryRequested(hash);
                if(fAsk) {
                    // this mn is in a non-recoverable state and we haven't asked other nodes yet
                    std::set<CNetAddr> setRequested;
                    // calulate only once and only when it's needed
                    if(vecJnodeRanks.empty()) {
                        int nRandomBlockHeight = GetRandInt(pCurrentBlockIndex->nHeight);
                        vecJnodeRanks = GetJnodeRanks(nRandomBlockHeight);
                    }
                    bool fAskedForMnbRecovery = false;
                    // ask first MNB_RECOVERY_QUORUM_TOTAL jnodes we can connect to and we haven't asked recently
                    for(int i = 0; setRequested.size() < MNB_RECOVERY_QUORUM_TOTAL && i < (int)vecJnodeRanks.size(); i++) {
                        // avoid banning
                        if(mWeAskedForJnodeListEntry.count(it->vin.prevout) && mWeAskedForJnodeListEntry[it->vin.prevout].count(vecJnodeRanks[i].second.addr)) continue;
                        // didn't ask recently, ok to ask now
                        CService addr = vecJnodeRanks[i].second.addr;
                        setRequested.insert(addr);
                        listScheduledMnbRequestConnections.push_back(std::make_pair(addr, hash));
                        fAskedForMnbRecovery = true;
                    }
                    if(fAskedForMnbRecovery) {
                        LogPrint("jnode", "CJnodeMan::CheckAndRemove -- Recovery initiated, jnode=%s\n", it->vin.prevout.ToStringShort());
                        nAskForMnbRecovery--;
                    }
                    // wait for mnb recovery replies for MNB_RECOVERY_WAIT_SECONDS seconds
                    mMnbRecoveryRequests[hash] = std::make_pair(GetTime() + MNB_RECOVERY_WAIT_SECONDS, setRequested);
                }
                ++it;
            }
        }

        // proces replies for JNODE_NEW_START_REQUIRED jnodes
        LogPrint("jnode", "CJnodeMan::CheckAndRemove -- mMnbRecoveryGoodReplies size=%d\n", (int)mMnbRecoveryGoodReplies.size());
        std::map<uint256, std::vector<CJnodeBroadcast> >::iterator itMnbReplies = mMnbRecoveryGoodReplies.begin();
        while(itMnbReplies != mMnbRecoveryGoodReplies.end()){
            if(mMnbRecoveryRequests[itMnbReplies->first].first < GetTime()) {
                // all nodes we asked should have replied now
                if(itMnbReplies->second.size() >= MNB_RECOVERY_QUORUM_REQUIRED) {
                    // majority of nodes we asked agrees that this mn doesn't require new mnb, reprocess one of new mnbs
                    LogPrint("jnode", "CJnodeMan::CheckAndRemove -- reprocessing mnb, jnode=%s\n", itMnbReplies->second[0].vin.prevout.ToStringShort());
                    // mapSeenJnodeBroadcast.erase(itMnbReplies->first);
                    int nDos;
                    itMnbReplies->second[0].fRecovery = true;
                    CheckMnbAndUpdateJnodeList(NULL, itMnbReplies->second[0], nDos);
                }
                LogPrint("jnode", "CJnodeMan::CheckAndRemove -- removing mnb recovery reply, jnode=%s, size=%d\n", itMnbReplies->second[0].vin.prevout.ToStringShort(), (int)itMnbReplies->second.size());
                mMnbRecoveryGoodReplies.erase(itMnbReplies++);
            } else {
                ++itMnbReplies;
            }
        }
    }
    {
        // no need for cm_main below
        LOCK(cs);

        std::map<uint256, std::pair< int64_t, std::set<CNetAddr> > >::iterator itMnbRequest = mMnbRecoveryRequests.begin();
        while(itMnbRequest != mMnbRecoveryRequests.end()){
            // Allow this mnb to be re-verified again after MNB_RECOVERY_RETRY_SECONDS seconds
            // if mn is still in JNODE_NEW_START_REQUIRED state.
            if(GetTime() - itMnbRequest->second.first > MNB_RECOVERY_RETRY_SECONDS) {
                mMnbRecoveryRequests.erase(itMnbRequest++);
            } else {
                ++itMnbRequest;
            }
        }

        // check who's asked for the Jnode list
        std::map<CNetAddr, int64_t>::iterator it1 = mAskedUsForJnodeList.begin();
        while(it1 != mAskedUsForJnodeList.end()){
            if((*it1).second < GetTime()) {
                mAskedUsForJnodeList.erase(it1++);
            } else {
                ++it1;
            }
        }

        // check who we asked for the Jnode list
        it1 = mWeAskedForJnodeList.begin();
        while(it1 != mWeAskedForJnodeList.end()){
            if((*it1).second < GetTime()){
                mWeAskedForJnodeList.erase(it1++);
            } else {
                ++it1;
            }
        }

        // check which Jnodes we've asked for
        std::map<COutPoint, std::map<CNetAddr, int64_t> >::iterator it2 = mWeAskedForJnodeListEntry.begin();
        while(it2 != mWeAskedForJnodeListEntry.end()){
            std::map<CNetAddr, int64_t>::iterator it3 = it2->second.begin();
            while(it3 != it2->second.end()){
                if(it3->second < GetTime()){
                    it2->second.erase(it3++);
                } else {
                    ++it3;
                }
            }
            if(it2->second.empty()) {
                mWeAskedForJnodeListEntry.erase(it2++);
            } else {
                ++it2;
            }
        }

        std::map<CNetAddr, CJnodeVerification>::iterator it3 = mWeAskedForVerification.begin();
        while(it3 != mWeAskedForVerification.end()){
            if(it3->second.nBlockHeight < pCurrentBlockIndex->nHeight - MAX_POSE_BLOCKS) {
                mWeAskedForVerification.erase(it3++);
            } else {
                ++it3;
            }
        }

        // NOTE: do not expire mapSeenJnodeBroadcast entries here, clean them on mnb updates!

        // remove expired mapSeenJnodePing
        std::map<uint256, CJnodePing>::iterator it4 = mapSeenJnodePing.begin();
        while(it4 != mapSeenJnodePing.end()){
            if((*it4).second.IsExpired()) {
                LogPrint("jnode", "CJnodeMan::CheckAndRemove -- Removing expired Jnode ping: hash=%s\n", (*it4).second.GetHash().ToString());
                mapSeenJnodePing.erase(it4++);
            } else {
                ++it4;
            }
        }

        // remove expired mapSeenJnodeVerification
        std::map<uint256, CJnodeVerification>::iterator itv2 = mapSeenJnodeVerification.begin();
        while(itv2 != mapSeenJnodeVerification.end()){
            if((*itv2).second.nBlockHeight < pCurrentBlockIndex->nHeight - MAX_POSE_BLOCKS){
                LogPrint("jnode", "CJnodeMan::CheckAndRemove -- Removing expired Jnode verification: hash=%s\n", (*itv2).first.ToString());
                mapSeenJnodeVerification.erase(itv2++);
            } else {
                ++itv2;
            }
        }

        LogPrintf("CJnodeMan::CheckAndRemove -- %s\n", ToString());

        if(fJnodesRemoved) {
            CheckAndRebuildJnodeIndex();
        }
    }

    if(fJnodesRemoved) {
        NotifyJnodeUpdates();
    }
}

void CJnodeMan::Clear()
{
    LOCK(cs);
    vJnodes.clear();
    mAskedUsForJnodeList.clear();
    mWeAskedForJnodeList.clear();
    mWeAskedForJnodeListEntry.clear();
    mapSeenJnodeBroadcast.clear();
    mapSeenJnodePing.clear();
    nDsqCount = 0;
    nLastWatchdogVoteTime = 0;
    indexJnodes.Clear();
    indexJnodesOld.Clear();
}

int CJnodeMan::CountJnodes(int nProtocolVersion)
{
    LOCK(cs);
    int nCount = 0;
    nProtocolVersion = nProtocolVersion == -1 ? mnpayments.GetMinJnodePaymentsProto() : nProtocolVersion;

    BOOST_FOREACH(CJnode& mn, vJnodes) {
        if(mn.nProtocolVersion < nProtocolVersion) continue;
        nCount++;
    }

    return nCount;
}

int CJnodeMan::CountEnabled(int nProtocolVersion)
{
    LOCK(cs);
    int nCount = 0;
    nProtocolVersion = nProtocolVersion == -1 ? mnpayments.GetMinJnodePaymentsProto() : nProtocolVersion;

    BOOST_FOREACH(CJnode& mn, vJnodes) {
        if(mn.nProtocolVersion < nProtocolVersion || !mn.IsEnabled()) continue;
        nCount++;
    }

    return nCount;
}

/* Only IPv4 jnodes are allowed in 12.1, saving this for later
int CJnodeMan::CountByIP(int nNetworkType)
{
    LOCK(cs);
    int nNodeCount = 0;

    BOOST_FOREACH(CJnode& mn, vJnodes)
        if ((nNetworkType == NET_IPV4 && mn.addr.IsIPv4()) ||
            (nNetworkType == NET_TOR  && mn.addr.IsTor())  ||
            (nNetworkType == NET_IPV6 && mn.addr.IsIPv6())) {
                nNodeCount++;
        }

    return nNodeCount;
}
*/

void CJnodeMan::DsegUpdate(CNode* pnode)
{
    LOCK(cs);

    if(Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if(!(pnode->addr.IsRFC1918() || pnode->addr.IsLocal())) {
            std::map<CNetAddr, int64_t>::iterator it = mWeAskedForJnodeList.find(pnode->addr);
            if(it != mWeAskedForJnodeList.end() && GetTime() < (*it).second) {
                LogPrintf("CJnodeMan::DsegUpdate -- we already asked %s for the list; skipping...\n", pnode->addr.ToString());
                return;
            }
        }
    }
    
    pnode->PushMessage(NetMsgType::DSEG, CTxIn());
    int64_t askAgain = GetTime() + DSEG_UPDATE_SECONDS;
    mWeAskedForJnodeList[pnode->addr] = askAgain;

    LogPrint("jnode", "CJnodeMan::DsegUpdate -- asked %s for the list\n", pnode->addr.ToString());
}

CJnode* CJnodeMan::Find(const CScript &payee)
{
    LOCK(cs);

    BOOST_FOREACH(CJnode& mn, vJnodes)
    {
        if(GetScriptForDestination(mn.pubKeyCollateralAddress.GetID()) == payee)
            return &mn;
    }
    return NULL;
}

CJnode* CJnodeMan::Find(const CTxIn &vin)
{
    LOCK(cs);

    BOOST_FOREACH(CJnode& mn, vJnodes)
    {
        if(mn.vin.prevout == vin.prevout)
            return &mn;
    }
    return NULL;
}

CJnode* CJnodeMan::Find(const CPubKey &pubKeyJnode)
{
    LOCK(cs);

    BOOST_FOREACH(CJnode& mn, vJnodes)
    {
        if(mn.pubKeyJnode == pubKeyJnode)
            return &mn;
    }
    return NULL;
}

bool CJnodeMan::Get(const CPubKey& pubKeyJnode, CJnode& jnode)
{
    // Theses mutexes are recursive so double locking by the same thread is safe.
    LOCK(cs);
    CJnode* pMN = Find(pubKeyJnode);
    if(!pMN)  {
        return false;
    }
    jnode = *pMN;
    return true;
}

bool CJnodeMan::Get(const CTxIn& vin, CJnode& jnode)
{
    // Theses mutexes are recursive so double locking by the same thread is safe.
    LOCK(cs);
    CJnode* pMN = Find(vin);
    if(!pMN)  {
        return false;
    }
    jnode = *pMN;
    return true;
}

jnode_info_t CJnodeMan::GetJnodeInfo(const CTxIn& vin)
{
    jnode_info_t info;
    LOCK(cs);
    CJnode* pMN = Find(vin);
    if(!pMN)  {
        return info;
    }
    info = pMN->GetInfo();
    return info;
}

jnode_info_t CJnodeMan::GetJnodeInfo(const CPubKey& pubKeyJnode)
{
    jnode_info_t info;
    LOCK(cs);
    CJnode* pMN = Find(pubKeyJnode);
    if(!pMN)  {
        return info;
    }
    info = pMN->GetInfo();
    return info;
}

bool CJnodeMan::Has(const CTxIn& vin)
{
    LOCK(cs);
    CJnode* pMN = Find(vin);
    return (pMN != NULL);
}

char* CJnodeMan::GetNotQualifyReason(CJnode& mn, int nBlockHeight, bool fFilterSigTime, int nMnCount)
{
    if (!mn.IsValidForPayment()) {
        char* reasonStr = new char[256];
        sprintf(reasonStr, "false: 'not valid for payment'");
        return reasonStr;
    }
    // //check protocol version
    if (mn.nProtocolVersion < mnpayments.GetMinJnodePaymentsProto()) {
        // LogPrintf("Invalid nProtocolVersion!\n");
        // LogPrintf("mn.nProtocolVersion=%s!\n", mn.nProtocolVersion);
        // LogPrintf("mnpayments.GetMinJnodePaymentsProto=%s!\n", mnpayments.GetMinJnodePaymentsProto());
        char* reasonStr = new char[256];
        sprintf(reasonStr, "false: 'Invalid nProtocolVersion', nProtocolVersion=%d", mn.nProtocolVersion);
        return reasonStr;
    }
    //it's in the list (up to 8 entries ahead of current block to allow propagation) -- so let's skip it
    if (mnpayments.IsScheduled(mn, nBlockHeight)) {
        // LogPrintf("mnpayments.IsScheduled!\n");
        char* reasonStr = new char[256];
        sprintf(reasonStr, "false: 'is scheduled'");
        return reasonStr;
    }
    //it's too new, wait for a cycle
    if (fFilterSigTime && mn.sigTime + (nMnCount * 2.6 * 60) > GetAdjustedTime()) {
        // LogPrintf("it's too new, wait for a cycle!\n");
        char* reasonStr = new char[256];
        sprintf(reasonStr, "false: 'too new', sigTime=%s, will be qualifed after=%s",
                DateTimeStrFormat("%Y-%m-%d %H:%M UTC", mn.sigTime).c_str(), DateTimeStrFormat("%Y-%m-%d %H:%M UTC", mn.sigTime + (nMnCount * 2.6 * 60)).c_str());
        return reasonStr;
    }
    //make sure it has at least as many confirmations as there are jnodes
    if (mn.GetCollateralAge() < nMnCount) {
        // LogPrintf("mn.GetCollateralAge()=%s!\n", mn.GetCollateralAge());
        // LogPrintf("nMnCount=%s!\n", nMnCount);
        char* reasonStr = new char[256];
        sprintf(reasonStr, "false: 'collateralAge < jnCount', collateralAge=%d, jnCount=%d", mn.GetCollateralAge(), nMnCount);
        return reasonStr;
    }
    return NULL;
}

//
// Deterministically select the oldest/best jnode to pay on the network
//
CJnode* CJnodeMan::GetNextJnodeInQueueForPayment(bool fFilterSigTime, int& nCount)
{
    if(!pCurrentBlockIndex) {
        nCount = 0;
        return NULL;
    }
    return GetNextJnodeInQueueForPayment(pCurrentBlockIndex->nHeight, fFilterSigTime, nCount);
}

CJnode* CJnodeMan::GetNextJnodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount)
{
    // Need LOCK2 here to ensure consistent locking order because the GetBlockHash call below locks cs_main
    LOCK2(cs_main,cs);

    CJnode *pBestJnode = NULL;
    std::vector<std::pair<int, CJnode*> > vecJnodeLastPaid;

    /*
        Make a vector with all of the last paid times
    */
    int nMnCount = CountEnabled();
    int index = 0;
    BOOST_FOREACH(CJnode &mn, vJnodes)
    {
        index += 1;
        // LogPrintf("index=%s, mn=%s\n", index, mn.ToString());
        /*if (!mn.IsValidForPayment()) {
            LogPrint("jnodeman", "Jnode, %s, addr(%s), not-qualified: 'not valid for payment'\n",
                     mn.vin.prevout.ToStringShort(), CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString());
            continue;
        }
        // //check protocol version
        if (mn.nProtocolVersion < mnpayments.GetMinJnodePaymentsProto()) {
            // LogPrintf("Invalid nProtocolVersion!\n");
            // LogPrintf("mn.nProtocolVersion=%s!\n", mn.nProtocolVersion);
            // LogPrintf("mnpayments.GetMinJnodePaymentsProto=%s!\n", mnpayments.GetMinJnodePaymentsProto());
            LogPrint("jnodeman", "Jnode, %s, addr(%s), not-qualified: 'invalid nProtocolVersion'\n",
                     mn.vin.prevout.ToStringShort(), CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString());
            continue;
        }
        //it's in the list (up to 8 entries ahead of current block to allow propagation) -- so let's skip it
        if (mnpayments.IsScheduled(mn, nBlockHeight)) {
            // LogPrintf("mnpayments.IsScheduled!\n");
            LogPrint("jnodeman", "Jnode, %s, addr(%s), not-qualified: 'IsScheduled'\n",
                     mn.vin.prevout.ToStringShort(), CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString());
            continue;
        }
        //it's too new, wait for a cycle
        if (fFilterSigTime && mn.sigTime + (nMnCount * 2.6 * 60) > GetAdjustedTime()) {
            // LogPrintf("it's too new, wait for a cycle!\n");
            LogPrint("jnodeman", "Jnode, %s, addr(%s), not-qualified: 'it's too new, wait for a cycle!', sigTime=%s, will be qualifed after=%s\n",
                     mn.vin.prevout.ToStringShort(), CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString(), DateTimeStrFormat("%Y-%m-%d %H:%M UTC", mn.sigTime).c_str(), DateTimeStrFormat("%Y-%m-%d %H:%M UTC", mn.sigTime + (nMnCount * 2.6 * 60)).c_str());
            continue;
        }
        //make sure it has at least as many confirmations as there are jnodes
        if (mn.GetCollateralAge() < nMnCount) {
            // LogPrintf("mn.GetCollateralAge()=%s!\n", mn.GetCollateralAge());
            // LogPrintf("nMnCount=%s!\n", nMnCount);
            LogPrint("jnodeman", "Jnode, %s, addr(%s), not-qualified: 'mn.GetCollateralAge() < nMnCount', CollateralAge=%d, nMnCount=%d\n",
                     mn.vin.prevout.ToStringShort(), CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString(), mn.GetCollateralAge(), nMnCount);
            continue;
        }*/
        char* reasonStr = GetNotQualifyReason(mn, nBlockHeight, fFilterSigTime, nMnCount);
        if (reasonStr != NULL) {
            LogPrint("jnodeman", "Jnode, %s, addr(%s), qualify %s\n",
                     mn.vin.prevout.ToStringShort(), CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString(), reasonStr);
            delete [] reasonStr;
            continue;
        }
        vecJnodeLastPaid.push_back(std::make_pair(mn.GetLastPaidBlock(), &mn));
    }
    nCount = (int)vecJnodeLastPaid.size();

    //when the network is in the process of upgrading, don't penalize nodes that recently restarted
    if(fFilterSigTime && nCount < nMnCount / 3) {
        // LogPrintf("Need Return, nCount=%s, nMnCount/3=%s\n", nCount, nMnCount/3);
        return GetNextJnodeInQueueForPayment(nBlockHeight, false, nCount);
    }

    // Sort them low to high
    sort(vecJnodeLastPaid.begin(), vecJnodeLastPaid.end(), CompareLastPaidBlock());

    uint256 blockHash;
    if(!GetBlockHash(blockHash, nBlockHeight - 101)) {
        LogPrintf("CJnode::GetNextJnodeInQueueForPayment -- ERROR: GetBlockHash() failed at nBlockHeight %d\n", nBlockHeight - 101);
        return NULL;
    }
    // Look at 1/10 of the oldest nodes (by last payment), calculate their scores and pay the best one
    //  -- This doesn't look at who is being paid in the +8-10 blocks, allowing for double payments very rarely
    //  -- 1/100 payments should be a double payment on mainnet - (1/(3000/10))*2
    //  -- (chance per block * chances before IsScheduled will fire)
    int nTenthNetwork = nMnCount/10;
    int nCountTenth = 0;
    arith_uint256 nHighest = 0;
    BOOST_FOREACH (PAIRTYPE(int, CJnode*)& s, vecJnodeLastPaid){
        arith_uint256 nScore = s.second->CalculateScore(blockHash);
        if(nScore > nHighest){
            nHighest = nScore;
            pBestJnode = s.second;
        }
        nCountTenth++;
        if(nCountTenth >= nTenthNetwork) break;
    }
    return pBestJnode;
}

CJnode* CJnodeMan::FindRandomNotInVec(const std::vector<CTxIn> &vecToExclude, int nProtocolVersion)
{
    LOCK(cs);

    nProtocolVersion = nProtocolVersion == -1 ? mnpayments.GetMinJnodePaymentsProto() : nProtocolVersion;

    int nCountEnabled = CountEnabled(nProtocolVersion);
    int nCountNotExcluded = nCountEnabled - vecToExclude.size();

    LogPrintf("CJnodeMan::FindRandomNotInVec -- %d enabled jnodes, %d jnodes to choose from\n", nCountEnabled, nCountNotExcluded);
    if(nCountNotExcluded < 1) return NULL;

    // fill a vector of pointers
    std::vector<CJnode*> vpJnodesShuffled;
    BOOST_FOREACH(CJnode &mn, vJnodes) {
        vpJnodesShuffled.push_back(&mn);
    }

    InsecureRand insecureRand;
    // shuffle pointers
    std::random_shuffle(vpJnodesShuffled.begin(), vpJnodesShuffled.end(), insecureRand);
    bool fExclude;

    // loop through
    BOOST_FOREACH(CJnode* pmn, vpJnodesShuffled) {
        if(pmn->nProtocolVersion < nProtocolVersion || !pmn->IsEnabled()) continue;
        fExclude = false;
        BOOST_FOREACH(const CTxIn &txinToExclude, vecToExclude) {
            if(pmn->vin.prevout == txinToExclude.prevout) {
                fExclude = true;
                break;
            }
        }
        if(fExclude) continue;
        // found the one not in vecToExclude
        LogPrint("jnode", "CJnodeMan::FindRandomNotInVec -- found, jnode=%s\n", pmn->vin.prevout.ToStringShort());
        return pmn;
    }

    LogPrint("jnode", "CJnodeMan::FindRandomNotInVec -- failed\n");
    return NULL;
}

int CJnodeMan::GetJnodeRank(const CTxIn& vin, int nBlockHeight, int nMinProtocol, bool fOnlyActive)
{
    std::vector<std::pair<int64_t, CJnode*> > vecJnodeScores;

    //make sure we know about this block
    uint256 blockHash = uint256();
    if(!GetBlockHash(blockHash, nBlockHeight)) return -1;

    LOCK(cs);

    // scan for winner
    BOOST_FOREACH(CJnode& mn, vJnodes) {
        if(mn.nProtocolVersion < nMinProtocol) continue;
        if(fOnlyActive) {
            if(!mn.IsEnabled()) continue;
        }
        else {
            if(!mn.IsValidForPayment()) continue;
        }
        int64_t nScore = mn.CalculateScore(blockHash).GetCompact(false);

        vecJnodeScores.push_back(std::make_pair(nScore, &mn));
    }

    sort(vecJnodeScores.rbegin(), vecJnodeScores.rend(), CompareScoreMN());

    int nRank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CJnode*)& scorePair, vecJnodeScores) {
        nRank++;
        if(scorePair.second->vin.prevout == vin.prevout) return nRank;
    }

    return -1;
}

std::vector<std::pair<int, CJnode> > CJnodeMan::GetJnodeRanks(int nBlockHeight, int nMinProtocol)
{
    std::vector<std::pair<int64_t, CJnode*> > vecJnodeScores;
    std::vector<std::pair<int, CJnode> > vecJnodeRanks;

    //make sure we know about this block
    uint256 blockHash = uint256();
    if(!GetBlockHash(blockHash, nBlockHeight)) return vecJnodeRanks;

    LOCK(cs);

    // scan for winner
    BOOST_FOREACH(CJnode& mn, vJnodes) {

        if(mn.nProtocolVersion < nMinProtocol || !mn.IsEnabled()) continue;

        int64_t nScore = mn.CalculateScore(blockHash).GetCompact(false);

        vecJnodeScores.push_back(std::make_pair(nScore, &mn));
    }

    sort(vecJnodeScores.rbegin(), vecJnodeScores.rend(), CompareScoreMN());

    int nRank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CJnode*)& s, vecJnodeScores) {
        nRank++;
        vecJnodeRanks.push_back(std::make_pair(nRank, *s.second));
    }

    return vecJnodeRanks;
}

CJnode* CJnodeMan::GetJnodeByRank(int nRank, int nBlockHeight, int nMinProtocol, bool fOnlyActive)
{
    std::vector<std::pair<int64_t, CJnode*> > vecJnodeScores;

    LOCK(cs);

    uint256 blockHash;
    if(!GetBlockHash(blockHash, nBlockHeight)) {
        LogPrintf("CJnode::GetJnodeByRank -- ERROR: GetBlockHash() failed at nBlockHeight %d\n", nBlockHeight);
        return NULL;
    }

    // Fill scores
    BOOST_FOREACH(CJnode& mn, vJnodes) {

        if(mn.nProtocolVersion < nMinProtocol) continue;
        if(fOnlyActive && !mn.IsEnabled()) continue;

        int64_t nScore = mn.CalculateScore(blockHash).GetCompact(false);

        vecJnodeScores.push_back(std::make_pair(nScore, &mn));
    }

    sort(vecJnodeScores.rbegin(), vecJnodeScores.rend(), CompareScoreMN());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CJnode*)& s, vecJnodeScores){
        rank++;
        if(rank == nRank) {
            return s.second;
        }
    }

    return NULL;
}

void CJnodeMan::ProcessJnodeConnections()
{
    //we don't care about this for regtest
    if(Params().NetworkIDString() == CBaseChainParams::REGTEST) return;

    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes) {
        if(pnode->fJnode) {
            if(darkSendPool.pSubmittedToJnode != NULL && pnode->addr == darkSendPool.pSubmittedToJnode->addr) continue;
            // LogPrintf("Closing Jnode connection: peer=%d, addr=%s\n", pnode->id, pnode->addr.ToString());
            pnode->fDisconnect = true;
        }
    }
}

std::pair<CService, std::set<uint256> > CJnodeMan::PopScheduledMnbRequestConnection()
{
    LOCK(cs);
    if(listScheduledMnbRequestConnections.empty()) {
        return std::make_pair(CService(), std::set<uint256>());
    }

    std::set<uint256> setResult;

    listScheduledMnbRequestConnections.sort();
    std::pair<CService, uint256> pairFront = listScheduledMnbRequestConnections.front();

    // squash hashes from requests with the same CService as the first one into setResult
    std::list< std::pair<CService, uint256> >::iterator it = listScheduledMnbRequestConnections.begin();
    while(it != listScheduledMnbRequestConnections.end()) {
        if(pairFront.first == it->first) {
            setResult.insert(it->second);
            it = listScheduledMnbRequestConnections.erase(it);
        } else {
            // since list is sorted now, we can be sure that there is no more hashes left
            // to ask for from this addr
            break;
        }
    }
    return std::make_pair(pairFront.first, setResult);
}


void CJnodeMan::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{

//    LogPrint("jnode", "CJnodeMan::ProcessMessage, strCommand=%s\n", strCommand);
    if(fLiteMode) return; // disable all Dash specific functionality
    if(!jnodeSync.IsBlockchainSynced()) return;

    if (strCommand == NetMsgType::MNANNOUNCE) { //Jnode Broadcast
        CJnodeBroadcast mnb;
        vRecv >> mnb;

        pfrom->setAskFor.erase(mnb.GetHash());

        LogPrintf("MNANNOUNCE -- Jnode announce, jnode=%s\n", mnb.vin.prevout.ToStringShort());

        int nDos = 0;

        if (CheckMnbAndUpdateJnodeList(pfrom, mnb, nDos)) {
            // use announced Jnode as a peer
            addrman.Add(CAddress(mnb.addr, NODE_NETWORK), pfrom->addr, 2*60*60);
        } else if(nDos > 0) {
            Misbehaving(pfrom->GetId(), nDos);
        }

        if(fJnodesAdded) {
            NotifyJnodeUpdates();
        }
    } else if (strCommand == NetMsgType::MNPING) { //Jnode Ping

        CJnodePing mnp;
        vRecv >> mnp;

        uint256 nHash = mnp.GetHash();

        pfrom->setAskFor.erase(nHash);

        LogPrint("jnode", "MNPING -- Jnode ping, jnode=%s\n", mnp.vin.prevout.ToStringShort());

        // Need LOCK2 here to ensure consistent locking order because the CheckAndUpdate call below locks cs_main
        LOCK2(cs_main, cs);

        if(mapSeenJnodePing.count(nHash)) return; //seen
        mapSeenJnodePing.insert(std::make_pair(nHash, mnp));

        LogPrint("jnode", "MNPING -- Jnode ping, jnode=%s new\n", mnp.vin.prevout.ToStringShort());

        // see if we have this Jnode
        CJnode* pmn = mnodeman.Find(mnp.vin);

        // too late, new MNANNOUNCE is required
        if(pmn && pmn->IsNewStartRequired()) return;

        int nDos = 0;
        if(mnp.CheckAndUpdate(pmn, false, nDos)) return;

        if(nDos > 0) {
            // if anything significant failed, mark that node
            Misbehaving(pfrom->GetId(), nDos);
        } else if(pmn != NULL) {
            // nothing significant failed, mn is a known one too
            return;
        }

        // something significant is broken or mn is unknown,
        // we might have to ask for a jnode entry once
        AskForMN(pfrom, mnp.vin);

    } else if (strCommand == NetMsgType::DSEG) { //Get Jnode list or specific entry
        // Ignore such requests until we are fully synced.
        // We could start processing this after jnode list is synced
        // but this is a heavy one so it's better to finish sync first.
        if (!jnodeSync.IsSynced()) return;

        CTxIn vin;
        vRecv >> vin;

        LogPrint("jnode", "DSEG -- Jnode list, jnode=%s\n", vin.prevout.ToStringShort());

        LOCK(cs);

        if(vin == CTxIn()) { //only should ask for this once
            //local network
            bool isLocal = (pfrom->addr.IsRFC1918() || pfrom->addr.IsLocal());

            if(!isLocal && Params().NetworkIDString() == CBaseChainParams::MAIN) {
                std::map<CNetAddr, int64_t>::iterator i = mAskedUsForJnodeList.find(pfrom->addr);
                if (i != mAskedUsForJnodeList.end()){
                    int64_t t = (*i).second;
                    if (GetTime() < t) {
                        Misbehaving(pfrom->GetId(), 34);
                        LogPrintf("DSEG -- peer already asked me for the list, peer=%d\n", pfrom->id);
                        return;
                    }
                }
                int64_t askAgain = GetTime() + DSEG_UPDATE_SECONDS;
                mAskedUsForJnodeList[pfrom->addr] = askAgain;
            }
        } //else, asking for a specific node which is ok

        int nInvCount = 0;

        BOOST_FOREACH(CJnode& mn, vJnodes) {
            if (vin != CTxIn() && vin != mn.vin) continue; // asked for specific vin but we are not there yet
            if (mn.addr.IsRFC1918() || mn.addr.IsLocal()) continue; // do not send local network jnode
            if (mn.IsUpdateRequired()) continue; // do not send outdated jnodes

            LogPrint("jnode", "DSEG -- Sending Jnode entry: jnode=%s  addr=%s\n", mn.vin.prevout.ToStringShort(), mn.addr.ToString());
            CJnodeBroadcast mnb = CJnodeBroadcast(mn);
            uint256 hash = mnb.GetHash();
            pfrom->PushInventory(CInv(MSG_JNODE_ANNOUNCE, hash));
            pfrom->PushInventory(CInv(MSG_JNODE_PING, mn.lastPing.GetHash()));
            nInvCount++;

            if (!mapSeenJnodeBroadcast.count(hash)) {
                mapSeenJnodeBroadcast.insert(std::make_pair(hash, std::make_pair(GetTime(), mnb)));
            }

            if (vin == mn.vin) {
                LogPrintf("DSEG -- Sent 1 Jnode inv to peer %d\n", pfrom->id);
                return;
            }
        }

        if(vin == CTxIn()) {
            pfrom->PushMessage(NetMsgType::SYNCSTATUSCOUNT, JNODE_SYNC_LIST, nInvCount);
            LogPrintf("DSEG -- Sent %d Jnode invs to peer %d\n", nInvCount, pfrom->id);
            return;
        }
        // smth weird happen - someone asked us for vin we have no idea about?
        LogPrint("jnode", "DSEG -- No invs sent to peer %d\n", pfrom->id);

    } else if (strCommand == NetMsgType::MNVERIFY) { // Jnode Verify

        // Need LOCK2 here to ensure consistent locking order because the all functions below call GetBlockHash which locks cs_main
        LOCK2(cs_main, cs);

        CJnodeVerification mnv;
        vRecv >> mnv;

        if(mnv.vchSig1.empty()) {
            // CASE 1: someone asked me to verify myself /IP we are using/
            SendVerifyReply(pfrom, mnv);
        } else if (mnv.vchSig2.empty()) {
            // CASE 2: we _probably_ got verification we requested from some jnode
            ProcessVerifyReply(pfrom, mnv);
        } else {
            // CASE 3: we _probably_ got verification broadcast signed by some jnode which verified another one
            ProcessVerifyBroadcast(pfrom, mnv);
        }
    }
}

// Verification of jnodes via unique direct requests.

void CJnodeMan::DoFullVerificationStep()
{
    if(activeJnode.vin == CTxIn()) return;
    if(!jnodeSync.IsSynced()) return;

    std::vector<std::pair<int, CJnode> > vecJnodeRanks = GetJnodeRanks(pCurrentBlockIndex->nHeight - 1, MIN_POSE_PROTO_VERSION);

    // Need LOCK2 here to ensure consistent locking order because the SendVerifyRequest call below locks cs_main
    // through GetHeight() signal in ConnectNode
    LOCK2(cs_main, cs);

    int nCount = 0;

    int nMyRank = -1;
    int nRanksTotal = (int)vecJnodeRanks.size();

    // send verify requests only if we are in top MAX_POSE_RANK
    std::vector<std::pair<int, CJnode> >::iterator it = vecJnodeRanks.begin();
    while(it != vecJnodeRanks.end()) {
        if(it->first > MAX_POSE_RANK) {
            LogPrint("jnode", "CJnodeMan::DoFullVerificationStep -- Must be in top %d to send verify request\n",
                        (int)MAX_POSE_RANK);
            return;
        }
        if(it->second.vin == activeJnode.vin) {
            nMyRank = it->first;
            LogPrint("jnode", "CJnodeMan::DoFullVerificationStep -- Found self at rank %d/%d, verifying up to %d jnodes\n",
                        nMyRank, nRanksTotal, (int)MAX_POSE_CONNECTIONS);
            break;
        }
        ++it;
    }

    // edge case: list is too short and this jnode is not enabled
    if(nMyRank == -1) return;

    // send verify requests to up to MAX_POSE_CONNECTIONS jnodes
    // starting from MAX_POSE_RANK + nMyRank and using MAX_POSE_CONNECTIONS as a step
    int nOffset = MAX_POSE_RANK + nMyRank - 1;
    if(nOffset >= (int)vecJnodeRanks.size()) return;

    std::vector<CJnode*> vSortedByAddr;
    BOOST_FOREACH(CJnode& mn, vJnodes) {
        vSortedByAddr.push_back(&mn);
    }

    sort(vSortedByAddr.begin(), vSortedByAddr.end(), CompareByAddr());

    it = vecJnodeRanks.begin() + nOffset;
    while(it != vecJnodeRanks.end()) {
        if(it->second.IsPoSeVerified() || it->second.IsPoSeBanned()) {
            LogPrint("jnode", "CJnodeMan::DoFullVerificationStep -- Already %s%s%s jnode %s address %s, skipping...\n",
                        it->second.IsPoSeVerified() ? "verified" : "",
                        it->second.IsPoSeVerified() && it->second.IsPoSeBanned() ? " and " : "",
                        it->second.IsPoSeBanned() ? "banned" : "",
                        it->second.vin.prevout.ToStringShort(), it->second.addr.ToString());
            nOffset += MAX_POSE_CONNECTIONS;
            if(nOffset >= (int)vecJnodeRanks.size()) break;
            it += MAX_POSE_CONNECTIONS;
            continue;
        }
        LogPrint("jnode", "CJnodeMan::DoFullVerificationStep -- Verifying jnode %s rank %d/%d address %s\n",
                    it->second.vin.prevout.ToStringShort(), it->first, nRanksTotal, it->second.addr.ToString());
        if(SendVerifyRequest(CAddress(it->second.addr, NODE_NETWORK), vSortedByAddr)) {
            nCount++;
            if(nCount >= MAX_POSE_CONNECTIONS) break;
        }
        nOffset += MAX_POSE_CONNECTIONS;
        if(nOffset >= (int)vecJnodeRanks.size()) break;
        it += MAX_POSE_CONNECTIONS;
    }

    LogPrint("jnode", "CJnodeMan::DoFullVerificationStep -- Sent verification requests to %d jnodes\n", nCount);
}

// This function tries to find jnodes with the same addr,
// find a verified one and ban all the other. If there are many nodes
// with the same addr but none of them is verified yet, then none of them are banned.
// It could take many times to run this before most of the duplicate nodes are banned.

void CJnodeMan::CheckSameAddr()
{
    if(!jnodeSync.IsSynced() || vJnodes.empty()) return;

    std::vector<CJnode*> vBan;
    std::vector<CJnode*> vSortedByAddr;

    {
        LOCK(cs);

        CJnode* pprevJnode = NULL;
        CJnode* pverifiedJnode = NULL;

        BOOST_FOREACH(CJnode& mn, vJnodes) {
            vSortedByAddr.push_back(&mn);
        }

        sort(vSortedByAddr.begin(), vSortedByAddr.end(), CompareByAddr());

        BOOST_FOREACH(CJnode* pmn, vSortedByAddr) {
            // check only (pre)enabled jnodes
            if(!pmn->IsEnabled() && !pmn->IsPreEnabled()) continue;
            // initial step
            if(!pprevJnode) {
                pprevJnode = pmn;
                pverifiedJnode = pmn->IsPoSeVerified() ? pmn : NULL;
                continue;
            }
            // second+ step
            if(pmn->addr == pprevJnode->addr) {
                if(pverifiedJnode) {
                    // another jnode with the same ip is verified, ban this one
                    vBan.push_back(pmn);
                } else if(pmn->IsPoSeVerified()) {
                    // this jnode with the same ip is verified, ban previous one
                    vBan.push_back(pprevJnode);
                    // and keep a reference to be able to ban following jnodes with the same ip
                    pverifiedJnode = pmn;
                }
            } else {
                pverifiedJnode = pmn->IsPoSeVerified() ? pmn : NULL;
            }
            pprevJnode = pmn;
        }
    }

    // ban duplicates
    BOOST_FOREACH(CJnode* pmn, vBan) {
        LogPrintf("CJnodeMan::CheckSameAddr -- increasing PoSe ban score for jnode %s\n", pmn->vin.prevout.ToStringShort());
        pmn->IncreasePoSeBanScore();
    }
}

bool CJnodeMan::SendVerifyRequest(const CAddress& addr, const std::vector<CJnode*>& vSortedByAddr)
{
    if(netfulfilledman.HasFulfilledRequest(addr, strprintf("%s", NetMsgType::MNVERIFY)+"-request")) {
        // we already asked for verification, not a good idea to do this too often, skip it
        LogPrint("jnode", "CJnodeMan::SendVerifyRequest -- too many requests, skipping... addr=%s\n", addr.ToString());
        return false;
    }

    CNode* pnode = ConnectNode(addr, NULL, false, true);
    if(pnode == NULL) {
        LogPrintf("CJnodeMan::SendVerifyRequest -- can't connect to node to verify it, addr=%s\n", addr.ToString());
        return false;
    }

    netfulfilledman.AddFulfilledRequest(addr, strprintf("%s", NetMsgType::MNVERIFY)+"-request");
    // use random nonce, store it and require node to reply with correct one later
    CJnodeVerification mnv(addr, GetRandInt(999999), pCurrentBlockIndex->nHeight - 1);
    mWeAskedForVerification[addr] = mnv;
    LogPrintf("CJnodeMan::SendVerifyRequest -- verifying node using nonce %d addr=%s\n", mnv.nonce, addr.ToString());
    pnode->PushMessage(NetMsgType::MNVERIFY, mnv);

    return true;
}

void CJnodeMan::SendVerifyReply(CNode* pnode, CJnodeVerification& mnv)
{
    // only jnodes can sign this, why would someone ask regular node?
    if(!fJNode) {
        // do not ban, malicious node might be using my IP
        // and trying to confuse the node which tries to verify it
        return;
    }

    if(netfulfilledman.HasFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-reply")) {
//        // peer should not ask us that often
        LogPrintf("JnodeMan::SendVerifyReply -- ERROR: peer already asked me recently, peer=%d\n", pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    uint256 blockHash;
    if(!GetBlockHash(blockHash, mnv.nBlockHeight)) {
        LogPrintf("JnodeMan::SendVerifyReply -- can't get block hash for unknown block height %d, peer=%d\n", mnv.nBlockHeight, pnode->id);
        return;
    }

    std::string strMessage = strprintf("%s%d%s", activeJnode.service.ToString(), mnv.nonce, blockHash.ToString());

    if(!darkSendSigner.SignMessage(strMessage, mnv.vchSig1, activeJnode.keyJnode)) {
        LogPrintf("JnodeMan::SendVerifyReply -- SignMessage() failed\n");
        return;
    }

    std::string strError;

    if(!darkSendSigner.VerifyMessage(activeJnode.pubKeyJnode, mnv.vchSig1, strMessage, strError)) {
        LogPrintf("JnodeMan::SendVerifyReply -- VerifyMessage() failed, error: %s\n", strError);
        return;
    }

    pnode->PushMessage(NetMsgType::MNVERIFY, mnv);
    netfulfilledman.AddFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-reply");
}

void CJnodeMan::ProcessVerifyReply(CNode* pnode, CJnodeVerification& mnv)
{
    std::string strError;

    // did we even ask for it? if that's the case we should have matching fulfilled request
    if(!netfulfilledman.HasFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-request")) {
        LogPrintf("CJnodeMan::ProcessVerifyReply -- ERROR: we didn't ask for verification of %s, peer=%d\n", pnode->addr.ToString(), pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    // Received nonce for a known address must match the one we sent
    if(mWeAskedForVerification[pnode->addr].nonce != mnv.nonce) {
        LogPrintf("CJnodeMan::ProcessVerifyReply -- ERROR: wrong nounce: requested=%d, received=%d, peer=%d\n",
                    mWeAskedForVerification[pnode->addr].nonce, mnv.nonce, pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    // Received nBlockHeight for a known address must match the one we sent
    if(mWeAskedForVerification[pnode->addr].nBlockHeight != mnv.nBlockHeight) {
        LogPrintf("CJnodeMan::ProcessVerifyReply -- ERROR: wrong nBlockHeight: requested=%d, received=%d, peer=%d\n",
                    mWeAskedForVerification[pnode->addr].nBlockHeight, mnv.nBlockHeight, pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    uint256 blockHash;
    if(!GetBlockHash(blockHash, mnv.nBlockHeight)) {
        // this shouldn't happen...
        LogPrintf("JnodeMan::ProcessVerifyReply -- can't get block hash for unknown block height %d, peer=%d\n", mnv.nBlockHeight, pnode->id);
        return;
    }

//    // we already verified this address, why node is spamming?
    if(netfulfilledman.HasFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-done")) {
        LogPrintf("CJnodeMan::ProcessVerifyReply -- ERROR: already verified %s recently\n", pnode->addr.ToString());
        Misbehaving(pnode->id, 20);
        return;
    }

    {
        LOCK(cs);

        CJnode* prealJnode = NULL;
        std::vector<CJnode*> vpJnodesToBan;
        std::vector<CJnode>::iterator it = vJnodes.begin();
        std::string strMessage1 = strprintf("%s%d%s", pnode->addr.ToString(), mnv.nonce, blockHash.ToString());
        while(it != vJnodes.end()) {
            if(CAddress(it->addr, NODE_NETWORK) == pnode->addr) {
                if(darkSendSigner.VerifyMessage(it->pubKeyJnode, mnv.vchSig1, strMessage1, strError)) {
                    // found it!
                    prealJnode = &(*it);
                    if(!it->IsPoSeVerified()) {
                        it->DecreasePoSeBanScore();
                    }
                    netfulfilledman.AddFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-done");

                    // we can only broadcast it if we are an activated jnode
                    if(activeJnode.vin == CTxIn()) continue;
                    // update ...
                    mnv.addr = it->addr;
                    mnv.vin1 = it->vin;
                    mnv.vin2 = activeJnode.vin;
                    std::string strMessage2 = strprintf("%s%d%s%s%s", mnv.addr.ToString(), mnv.nonce, blockHash.ToString(),
                                            mnv.vin1.prevout.ToStringShort(), mnv.vin2.prevout.ToStringShort());
                    // ... and sign it
                    if(!darkSendSigner.SignMessage(strMessage2, mnv.vchSig2, activeJnode.keyJnode)) {
                        LogPrintf("JnodeMan::ProcessVerifyReply -- SignMessage() failed\n");
                        return;
                    }

                    std::string strError;

                    if(!darkSendSigner.VerifyMessage(activeJnode.pubKeyJnode, mnv.vchSig2, strMessage2, strError)) {
                        LogPrintf("JnodeMan::ProcessVerifyReply -- VerifyMessage() failed, error: %s\n", strError);
                        return;
                    }

                    mWeAskedForVerification[pnode->addr] = mnv;
                    mnv.Relay();

                } else {
                    vpJnodesToBan.push_back(&(*it));
                }
            }
            ++it;
        }
        // no real jnode found?...
        if(!prealJnode) {
            // this should never be the case normally,
            // only if someone is trying to game the system in some way or smth like that
            LogPrintf("CJnodeMan::ProcessVerifyReply -- ERROR: no real jnode found for addr %s\n", pnode->addr.ToString());
            Misbehaving(pnode->id, 20);
            return;
        }
        LogPrintf("CJnodeMan::ProcessVerifyReply -- verified real jnode %s for addr %s\n",
                    prealJnode->vin.prevout.ToStringShort(), pnode->addr.ToString());
        // increase ban score for everyone else
        BOOST_FOREACH(CJnode* pmn, vpJnodesToBan) {
            pmn->IncreasePoSeBanScore();
            LogPrint("jnode", "CJnodeMan::ProcessVerifyBroadcast -- increased PoSe ban score for %s addr %s, new score %d\n",
                        prealJnode->vin.prevout.ToStringShort(), pnode->addr.ToString(), pmn->nPoSeBanScore);
        }
        LogPrintf("CJnodeMan::ProcessVerifyBroadcast -- PoSe score increased for %d fake jnodes, addr %s\n",
                    (int)vpJnodesToBan.size(), pnode->addr.ToString());
    }
}

void CJnodeMan::ProcessVerifyBroadcast(CNode* pnode, const CJnodeVerification& mnv)
{
    std::string strError;

    if(mapSeenJnodeVerification.find(mnv.GetHash()) != mapSeenJnodeVerification.end()) {
        // we already have one
        return;
    }
    mapSeenJnodeVerification[mnv.GetHash()] = mnv;

    // we don't care about history
    if(mnv.nBlockHeight < pCurrentBlockIndex->nHeight - MAX_POSE_BLOCKS) {
        LogPrint("jnode", "JnodeMan::ProcessVerifyBroadcast -- Outdated: current block %d, verification block %d, peer=%d\n",
                    pCurrentBlockIndex->nHeight, mnv.nBlockHeight, pnode->id);
        return;
    }

    if(mnv.vin1.prevout == mnv.vin2.prevout) {
        LogPrint("jnode", "JnodeMan::ProcessVerifyBroadcast -- ERROR: same vins %s, peer=%d\n",
                    mnv.vin1.prevout.ToStringShort(), pnode->id);
        // that was NOT a good idea to cheat and verify itself,
        // ban the node we received such message from
        Misbehaving(pnode->id, 100);
        return;
    }

    uint256 blockHash;
    if(!GetBlockHash(blockHash, mnv.nBlockHeight)) {
        // this shouldn't happen...
        LogPrintf("JnodeMan::ProcessVerifyBroadcast -- Can't get block hash for unknown block height %d, peer=%d\n", mnv.nBlockHeight, pnode->id);
        return;
    }

    int nRank = GetJnodeRank(mnv.vin2, mnv.nBlockHeight, MIN_POSE_PROTO_VERSION);

    if (nRank == -1) {
        LogPrint("jnode", "CJnodeMan::ProcessVerifyBroadcast -- Can't calculate rank for jnode %s\n",
                    mnv.vin2.prevout.ToStringShort());
        return;
    }

    if(nRank > MAX_POSE_RANK) {
        LogPrint("jnode", "CJnodeMan::ProcessVerifyBroadcast -- Mastrernode %s is not in top %d, current rank %d, peer=%d\n",
                    mnv.vin2.prevout.ToStringShort(), (int)MAX_POSE_RANK, nRank, pnode->id);
        return;
    }

    {
        LOCK(cs);

        std::string strMessage1 = strprintf("%s%d%s", mnv.addr.ToString(), mnv.nonce, blockHash.ToString());
        std::string strMessage2 = strprintf("%s%d%s%s%s", mnv.addr.ToString(), mnv.nonce, blockHash.ToString(),
                                mnv.vin1.prevout.ToStringShort(), mnv.vin2.prevout.ToStringShort());

        CJnode* pmn1 = Find(mnv.vin1);
        if(!pmn1) {
            LogPrintf("CJnodeMan::ProcessVerifyBroadcast -- can't find jnode1 %s\n", mnv.vin1.prevout.ToStringShort());
            return;
        }

        CJnode* pmn2 = Find(mnv.vin2);
        if(!pmn2) {
            LogPrintf("CJnodeMan::ProcessVerifyBroadcast -- can't find jnode2 %s\n", mnv.vin2.prevout.ToStringShort());
            return;
        }

        if(pmn1->addr != mnv.addr) {
            LogPrintf("CJnodeMan::ProcessVerifyBroadcast -- addr %s do not match %s\n", mnv.addr.ToString(), pnode->addr.ToString());
            return;
        }

        if(darkSendSigner.VerifyMessage(pmn1->pubKeyJnode, mnv.vchSig1, strMessage1, strError)) {
            LogPrintf("JnodeMan::ProcessVerifyBroadcast -- VerifyMessage() for jnode1 failed, error: %s\n", strError);
            return;
        }

        if(darkSendSigner.VerifyMessage(pmn2->pubKeyJnode, mnv.vchSig2, strMessage2, strError)) {
            LogPrintf("JnodeMan::ProcessVerifyBroadcast -- VerifyMessage() for jnode2 failed, error: %s\n", strError);
            return;
        }

        if(!pmn1->IsPoSeVerified()) {
            pmn1->DecreasePoSeBanScore();
        }
        mnv.Relay();

        LogPrintf("CJnodeMan::ProcessVerifyBroadcast -- verified jnode %s for addr %s\n",
                    pmn1->vin.prevout.ToStringShort(), pnode->addr.ToString());

        // increase ban score for everyone else with the same addr
        int nCount = 0;
        BOOST_FOREACH(CJnode& mn, vJnodes) {
            if(mn.addr != mnv.addr || mn.vin.prevout == mnv.vin1.prevout) continue;
            mn.IncreasePoSeBanScore();
            nCount++;
            LogPrint("jnode", "CJnodeMan::ProcessVerifyBroadcast -- increased PoSe ban score for %s addr %s, new score %d\n",
                        mn.vin.prevout.ToStringShort(), mn.addr.ToString(), mn.nPoSeBanScore);
        }
        LogPrintf("CJnodeMan::ProcessVerifyBroadcast -- PoSe score incresed for %d fake jnodes, addr %s\n",
                    nCount, pnode->addr.ToString());
    }
}

std::string CJnodeMan::ToString() const
{
    std::ostringstream info;

    info << "Jnodes: " << (int)vJnodes.size() <<
            ", peers who asked us for Jnode list: " << (int)mAskedUsForJnodeList.size() <<
            ", peers we asked for Jnode list: " << (int)mWeAskedForJnodeList.size() <<
            ", entries in Jnode list we asked for: " << (int)mWeAskedForJnodeListEntry.size() <<
            ", jnode index size: " << indexJnodes.GetSize() <<
            ", nDsqCount: " << (int)nDsqCount;

    return info.str();
}

void CJnodeMan::UpdateJnodeList(CJnodeBroadcast mnb)
{
    try {
        LogPrintf("CJnodeMan::UpdateJnodeList\n");
        LOCK2(cs_main, cs);
        mapSeenJnodePing.insert(std::make_pair(mnb.lastPing.GetHash(), mnb.lastPing));
        mapSeenJnodeBroadcast.insert(std::make_pair(mnb.GetHash(), std::make_pair(GetTime(), mnb)));

        LogPrintf("CJnodeMan::UpdateJnodeList -- jnode=%s  addr=%s\n", mnb.vin.prevout.ToStringShort(), mnb.addr.ToString());

        CJnode *pmn = Find(mnb.vin);
        if (pmn == NULL) {
            CJnode mn(mnb);
            if (Add(mn)) {
                jnodeSync.AddedJnodeList();
            }
        } else {
            CJnodeBroadcast mnbOld = mapSeenJnodeBroadcast[CJnodeBroadcast(*pmn).GetHash()].second;
            if (pmn->UpdateFromNewBroadcast(mnb)) {
                jnodeSync.AddedJnodeList();
                mapSeenJnodeBroadcast.erase(mnbOld.GetHash());
            }
        }
    } catch (const std::exception &e) {
        PrintExceptionContinue(&e, "UpdateJnodeList");
    }
}

bool CJnodeMan::CheckMnbAndUpdateJnodeList(CNode* pfrom, CJnodeBroadcast mnb, int& nDos)
{
    // Need LOCK2 here to ensure consistent locking order because the SimpleCheck call below locks cs_main
    LOCK(cs_main);

    {
        LOCK(cs);
        nDos = 0;
        LogPrint("jnode", "CJnodeMan::CheckMnbAndUpdateJnodeList -- jnode=%s\n", mnb.vin.prevout.ToStringShort());

        uint256 hash = mnb.GetHash();
        if (mapSeenJnodeBroadcast.count(hash) && !mnb.fRecovery) { //seen
            LogPrint("jnode", "CJnodeMan::CheckMnbAndUpdateJnodeList -- jnode=%s seen\n", mnb.vin.prevout.ToStringShort());
            // less then 2 pings left before this MN goes into non-recoverable state, bump sync timeout
            if (GetTime() - mapSeenJnodeBroadcast[hash].first > JNODE_NEW_START_REQUIRED_SECONDS - JNODE_MIN_MNP_SECONDS * 2) {
                LogPrint("jnode", "CJnodeMan::CheckMnbAndUpdateJnodeList -- jnode=%s seen update\n", mnb.vin.prevout.ToStringShort());
                mapSeenJnodeBroadcast[hash].first = GetTime();
                jnodeSync.AddedJnodeList();
            }
            // did we ask this node for it?
            if (pfrom && IsMnbRecoveryRequested(hash) && GetTime() < mMnbRecoveryRequests[hash].first) {
                LogPrint("jnode", "CJnodeMan::CheckMnbAndUpdateJnodeList -- mnb=%s seen request\n", hash.ToString());
                if (mMnbRecoveryRequests[hash].second.count(pfrom->addr)) {
                    LogPrint("jnode", "CJnodeMan::CheckMnbAndUpdateJnodeList -- mnb=%s seen request, addr=%s\n", hash.ToString(), pfrom->addr.ToString());
                    // do not allow node to send same mnb multiple times in recovery mode
                    mMnbRecoveryRequests[hash].second.erase(pfrom->addr);
                    // does it have newer lastPing?
                    if (mnb.lastPing.sigTime > mapSeenJnodeBroadcast[hash].second.lastPing.sigTime) {
                        // simulate Check
                        CJnode mnTemp = CJnode(mnb);
                        mnTemp.Check();
                        LogPrint("jnode", "CJnodeMan::CheckMnbAndUpdateJnodeList -- mnb=%s seen request, addr=%s, better lastPing: %d min ago, projected mn state: %s\n", hash.ToString(), pfrom->addr.ToString(), (GetTime() - mnb.lastPing.sigTime) / 60, mnTemp.GetStateString());
                        if (mnTemp.IsValidStateForAutoStart(mnTemp.nActiveState)) {
                            // this node thinks it's a good one
                            LogPrint("jnode", "CJnodeMan::CheckMnbAndUpdateJnodeList -- jnode=%s seen good\n", mnb.vin.prevout.ToStringShort());
                            mMnbRecoveryGoodReplies[hash].push_back(mnb);
                        }
                    }
                }
            }
            return true;
        }
        mapSeenJnodeBroadcast.insert(std::make_pair(hash, std::make_pair(GetTime(), mnb)));

        LogPrint("jnode", "CJnodeMan::CheckMnbAndUpdateJnodeList -- jnode=%s new\n", mnb.vin.prevout.ToStringShort());

        if (!mnb.SimpleCheck(nDos)) {
            LogPrint("jnode", "CJnodeMan::CheckMnbAndUpdateJnodeList -- SimpleCheck() failed, jnode=%s\n", mnb.vin.prevout.ToStringShort());
            return false;
        }

        // search Jnode list
        CJnode *pmn = Find(mnb.vin);
        if (pmn) {
            CJnodeBroadcast mnbOld = mapSeenJnodeBroadcast[CJnodeBroadcast(*pmn).GetHash()].second;
            if (!mnb.Update(pmn, nDos)) {
                LogPrint("jnode", "CJnodeMan::CheckMnbAndUpdateJnodeList -- Update() failed, jnode=%s\n", mnb.vin.prevout.ToStringShort());
                return false;
            }
            if (hash != mnbOld.GetHash()) {
                mapSeenJnodeBroadcast.erase(mnbOld.GetHash());
            }
        }
    } // end of LOCK(cs);

    if(mnb.CheckOutpoint(nDos)) {
        Add(mnb);
        jnodeSync.AddedJnodeList();
        // if it matches our Jnode privkey...
        if(fJNode && mnb.pubKeyJnode == activeJnode.pubKeyJnode) {
            mnb.nPoSeBanScore = -JNODE_POSE_BAN_MAX_SCORE;
            if(mnb.nProtocolVersion == PROTOCOL_VERSION) {
                // ... and PROTOCOL_VERSION, then we've been remotely activated ...
                LogPrintf("CJnodeMan::CheckMnbAndUpdateJnodeList -- Got NEW Jnode entry: jnode=%s  sigTime=%lld  addr=%s\n",
                            mnb.vin.prevout.ToStringShort(), mnb.sigTime, mnb.addr.ToString());
                activeJnode.ManageState();
            } else {
                // ... otherwise we need to reactivate our node, do not add it to the list and do not relay
                // but also do not ban the node we get this message from
                LogPrintf("CJnodeMan::CheckMnbAndUpdateJnodeList -- wrong PROTOCOL_VERSION, re-activate your MN: message nProtocolVersion=%d  PROTOCOL_VERSION=%d\n", mnb.nProtocolVersion, PROTOCOL_VERSION);
                return false;
            }
        }
        mnb.RelayJNode();
    } else {
        LogPrintf("CJnodeMan::CheckMnbAndUpdateJnodeList -- Rejected Jnode entry: %s  addr=%s\n", mnb.vin.prevout.ToStringShort(), mnb.addr.ToString());
        return false;
    }

    return true;
}

void CJnodeMan::UpdateLastPaid()
{
    LOCK(cs);
    if(fLiteMode) return;
    if(!pCurrentBlockIndex) {
        // LogPrintf("CJnodeMan::UpdateLastPaid, pCurrentBlockIndex=NULL\n");
        return;
    }

    static bool IsFirstRun = true;
    // Do full scan on first run or if we are not a jnode
    // (MNs should update this info on every block, so limited scan should be enough for them)
    int nMaxBlocksToScanBack = (IsFirstRun || !fJNode) ? mnpayments.GetStorageLimit() : LAST_PAID_SCAN_BLOCKS;

    LogPrint("mnpayments", "CJnodeMan::UpdateLastPaid -- nHeight=%d, nMaxBlocksToScanBack=%d, IsFirstRun=%s\n",
                             pCurrentBlockIndex->nHeight, nMaxBlocksToScanBack, IsFirstRun ? "true" : "false");

    BOOST_FOREACH(CJnode& mn, vJnodes) {
        mn.UpdateLastPaid(pCurrentBlockIndex, nMaxBlocksToScanBack);
    }

    // every time is like the first time if winners list is not synced
    IsFirstRun = !jnodeSync.IsWinnersListSynced();
}

void CJnodeMan::CheckAndRebuildJnodeIndex()
{
    LOCK(cs);

    if(GetTime() - nLastIndexRebuildTime < MIN_INDEX_REBUILD_TIME) {
        return;
    }

    if(indexJnodes.GetSize() <= MAX_EXPECTED_INDEX_SIZE) {
        return;
    }

    if(indexJnodes.GetSize() <= int(vJnodes.size())) {
        return;
    }

    indexJnodesOld = indexJnodes;
    indexJnodes.Clear();
    for(size_t i = 0; i < vJnodes.size(); ++i) {
        indexJnodes.AddJnodeVIN(vJnodes[i].vin);
    }

    fIndexRebuilt = true;
    nLastIndexRebuildTime = GetTime();
}

void CJnodeMan::UpdateWatchdogVoteTime(const CTxIn& vin)
{
    LOCK(cs);
    CJnode* pMN = Find(vin);
    if(!pMN)  {
        return;
    }
    pMN->UpdateWatchdogVoteTime();
    nLastWatchdogVoteTime = GetTime();
}

bool CJnodeMan::IsWatchdogActive()
{
    LOCK(cs);
    // Check if any jnodes have voted recently, otherwise return false
    return (GetTime() - nLastWatchdogVoteTime) <= JNODE_WATCHDOG_MAX_SECONDS;
}

void CJnodeMan::CheckJnode(const CTxIn& vin, bool fForce)
{
    LOCK(cs);
    CJnode* pMN = Find(vin);
    if(!pMN)  {
        return;
    }
    pMN->Check(fForce);
}

void CJnodeMan::CheckJnode(const CPubKey& pubKeyJnode, bool fForce)
{
    LOCK(cs);
    CJnode* pMN = Find(pubKeyJnode);
    if(!pMN)  {
        return;
    }
    pMN->Check(fForce);
}

int CJnodeMan::GetJnodeState(const CTxIn& vin)
{
    LOCK(cs);
    CJnode* pMN = Find(vin);
    if(!pMN)  {
        return CJnode::JNODE_NEW_START_REQUIRED;
    }
    return pMN->nActiveState;
}

int CJnodeMan::GetJnodeState(const CPubKey& pubKeyJnode)
{
    LOCK(cs);
    CJnode* pMN = Find(pubKeyJnode);
    if(!pMN)  {
        return CJnode::JNODE_NEW_START_REQUIRED;
    }
    return pMN->nActiveState;
}

bool CJnodeMan::IsJnodePingedWithin(const CTxIn& vin, int nSeconds, int64_t nTimeToCheckAt)
{
    LOCK(cs);
    CJnode* pMN = Find(vin);
    if(!pMN) {
        return false;
    }
    return pMN->IsPingedWithin(nSeconds, nTimeToCheckAt);
}

void CJnodeMan::SetJnodeLastPing(const CTxIn& vin, const CJnodePing& mnp)
{
    LOCK(cs);
    CJnode* pMN = Find(vin);
    if(!pMN)  {
        return;
    }
    pMN->lastPing = mnp;
    mapSeenJnodePing.insert(std::make_pair(mnp.GetHash(), mnp));

    CJnodeBroadcast mnb(*pMN);
    uint256 hash = mnb.GetHash();
    if(mapSeenJnodeBroadcast.count(hash)) {
        mapSeenJnodeBroadcast[hash].second.lastPing = mnp;
    }
}

void CJnodeMan::UpdatedBlockTip(const CBlockIndex *pindex)
{
    pCurrentBlockIndex = pindex;
    LogPrint("jnode", "CJnodeMan::UpdatedBlockTip -- pCurrentBlockIndex->nHeight=%d\n", pCurrentBlockIndex->nHeight);

    CheckSameAddr();

    if(fJNode) {
        // normal wallet does not need to update this every block, doing update on rpc call should be enough
        UpdateLastPaid();
    }
}

void CJnodeMan::NotifyJnodeUpdates()
{
    // Avoid double locking
    bool fJnodesAddedLocal = false;
    bool fJnodesRemovedLocal = false;
    {
        LOCK(cs);
        fJnodesAddedLocal = fJnodesAdded;
        fJnodesRemovedLocal = fJnodesRemoved;
    }

    if(fJnodesAddedLocal) {
//        governance.CheckJnodeOrphanObjects();
//        governance.CheckJnodeOrphanVotes();
    }
    if(fJnodesRemovedLocal) {
//        governance.UpdateCachesAndClean();
    }

    LOCK(cs);
    fJnodesAdded = false;
    fJnodesRemoved = false;
}
