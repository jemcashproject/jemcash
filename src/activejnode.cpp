// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activejnode.h"
#include "consensus/consensus.h"
#include "jnode.h"
#include "jnode-sync.h"
#include "jnode-payments.h"
#include "jnodeman.h"
#include "protocol.h"

extern CWallet *pwalletMain;

// Keep track of the active Jnode
CActiveJnode activeJnode;

void CActiveJnode::ManageState() {
    LogPrint("jnode", "CActiveJnode::ManageState -- Start\n");
    if (!fJNode) {
        LogPrint("jnode", "CActiveJnode::ManageState -- Not a jnode, returning\n");
        return;
    }

    if (Params().NetworkIDString() != CBaseChainParams::REGTEST && !jnodeSync.IsBlockchainSynced()) {
        nState = ACTIVE_JNODE_SYNC_IN_PROCESS;
        LogPrintf("CActiveJnode::ManageState -- %s: %s\n", GetStateString(), GetStatus());
        return;
    }

    if (nState == ACTIVE_JNODE_SYNC_IN_PROCESS) {
        nState = ACTIVE_JNODE_INITIAL;
    }

    LogPrint("jnode", "CActiveJnode::ManageState -- status = %s, type = %s, pinger enabled = %d\n",
             GetStatus(), GetTypeString(), fPingerEnabled);

    if (eType == JNODE_UNKNOWN) {
        ManageStateInitial();
    }

    if (eType == JNODE_REMOTE) {
        ManageStateRemote();
    } else if (eType == JNODE_LOCAL) {
        // Try Remote Start first so the started local jnode can be restarted without recreate jnode broadcast.
        ManageStateRemote();
        if (nState != ACTIVE_JNODE_STARTED)
            ManageStateLocal();
    }

    SendJnodePing();
}

std::string CActiveJnode::GetStateString() const {
    switch (nState) {
        case ACTIVE_JNODE_INITIAL:
            return "INITIAL";
        case ACTIVE_JNODE_SYNC_IN_PROCESS:
            return "SYNC_IN_PROCESS";
        case ACTIVE_JNODE_INPUT_TOO_NEW:
            return "INPUT_TOO_NEW";
        case ACTIVE_JNODE_NOT_CAPABLE:
            return "NOT_CAPABLE";
        case ACTIVE_JNODE_STARTED:
            return "STARTED";
        default:
            return "UNKNOWN";
    }
}

std::string CActiveJnode::GetStatus() const {
    switch (nState) {
        case ACTIVE_JNODE_INITIAL:
            return "Node just started, not yet activated";
        case ACTIVE_JNODE_SYNC_IN_PROCESS:
            return "Sync in progress. Must wait until sync is complete to start Jnode";
        case ACTIVE_JNODE_INPUT_TOO_NEW:
            return strprintf("Jnode input must have at least %d confirmations",
                             Params().GetConsensus().nJnodeMinimumConfirmations);
        case ACTIVE_JNODE_NOT_CAPABLE:
            return "Not capable jnode: " + strNotCapableReason;
        case ACTIVE_JNODE_STARTED:
            return "Jnode successfully started";
        default:
            return "Unknown";
    }
}

std::string CActiveJnode::GetTypeString() const {
    std::string strType;
    switch (eType) {
        case JNODE_UNKNOWN:
            strType = "UNKNOWN";
            break;
        case JNODE_REMOTE:
            strType = "REMOTE";
            break;
        case JNODE_LOCAL:
            strType = "LOCAL";
            break;
        default:
            strType = "UNKNOWN";
            break;
    }
    return strType;
}

bool CActiveJnode::SendJnodePing() {
    if (!fPingerEnabled) {
        LogPrint("jnode",
                 "CActiveJnode::SendJnodePing -- %s: jnode ping service is disabled, skipping...\n",
                 GetStateString());
        return false;
    }

    if (!mnodeman.Has(vin)) {
        strNotCapableReason = "Jnode not in jnode list";
        nState = ACTIVE_JNODE_NOT_CAPABLE;
        LogPrintf("CActiveJnode::SendJnodePing -- %s: %s\n", GetStateString(), strNotCapableReason);
        return false;
    }

    CJnodePing mnp(vin);
    if (!mnp.Sign(keyJnode, pubKeyJnode)) {
        LogPrintf("CActiveJnode::SendJnodePing -- ERROR: Couldn't sign Jnode Ping\n");
        return false;
    }

    // Update lastPing for our jnode in Jnode list
    if (mnodeman.IsJnodePingedWithin(vin, JNODE_MIN_MNP_SECONDS, mnp.sigTime)) {
        LogPrintf("CActiveJnode::SendJnodePing -- Too early to send Jnode Ping\n");
        return false;
    }

    mnodeman.SetJnodeLastPing(vin, mnp);

    LogPrintf("CActiveJnode::SendJnodePing -- Relaying ping, collateral=%s\n", vin.ToString());
    mnp.Relay();

    return true;
}

void CActiveJnode::ManageStateInitial() {
    LogPrint("jnode", "CActiveJnode::ManageStateInitial -- status = %s, type = %s, pinger enabled = %d\n",
             GetStatus(), GetTypeString(), fPingerEnabled);

    // Check that our local network configuration is correct
    if (!fListen) {
        // listen option is probably overwritten by smth else, no good
        nState = ACTIVE_JNODE_NOT_CAPABLE;
        strNotCapableReason = "Jnode must accept connections from outside. Make sure listen configuration option is not overwritten by some another parameter.";
        LogPrintf("CActiveJnode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    bool fFoundLocal = false;
    {
        LOCK(cs_vNodes);

        // First try to find whatever local address is specified by externalip option
        fFoundLocal = GetLocal(service) && CJnode::IsValidNetAddr(service);
        if (!fFoundLocal) {
            // nothing and no live connections, can't do anything for now
            if (vNodes.empty()) {
                nState = ACTIVE_JNODE_NOT_CAPABLE;
                strNotCapableReason = "Can't detect valid external address. Will retry when there are some connections available.";
                LogPrintf("CActiveJnode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
                return;
            }
            // We have some peers, let's try to find our local address from one of them
            BOOST_FOREACH(CNode * pnode, vNodes)
            {
                if (pnode->fSuccessfullyConnected && pnode->addr.IsIPv4()) {
                    fFoundLocal = GetLocal(service, &pnode->addr) && CJnode::IsValidNetAddr(service);
                    if (fFoundLocal) break;
                }
            }
        }
    }

    if (!fFoundLocal) {
        nState = ACTIVE_JNODE_NOT_CAPABLE;
        strNotCapableReason = "Can't detect valid external address. Please consider using the externalip configuration option if problem persists. Make sure to use IPv4 address only.";
        LogPrintf("CActiveJnode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    int mainnetDefaultPort = Params(CBaseChainParams::MAIN).GetDefaultPort();
    if (Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if (service.GetPort() != mainnetDefaultPort) {
            nState = ACTIVE_JNODE_NOT_CAPABLE;
            strNotCapableReason = strprintf("Invalid port: %u - only %d is supported on mainnet.", service.GetPort(),
                                            mainnetDefaultPort);
            LogPrintf("CActiveJnode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
    } else if (service.GetPort() == mainnetDefaultPort) {
        nState = ACTIVE_JNODE_NOT_CAPABLE;
        strNotCapableReason = strprintf("Invalid port: %u - %d is only supported on mainnet.", service.GetPort(),
                                        mainnetDefaultPort);
        LogPrintf("CActiveJnode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    LogPrintf("CActiveJnode::ManageStateInitial -- Checking inbound connection to '%s'\n", service.ToString());
    //TODO
    if (!ConnectNode(CAddress(service, NODE_NETWORK), NULL, false, true)) {
        nState = ACTIVE_JNODE_NOT_CAPABLE;
        strNotCapableReason = "Could not connect to " + service.ToString();
        LogPrintf("CActiveJnode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    // Default to REMOTE
    eType = JNODE_REMOTE;

    // Check if wallet funds are available
    if (!pwalletMain) {
        LogPrintf("CActiveJnode::ManageStateInitial -- %s: Wallet not available\n", GetStateString());
        return;
    }

    if (pwalletMain->IsLocked()) {
        LogPrintf("CActiveJnode::ManageStateInitial -- %s: Wallet is locked\n", GetStateString());
        return;
    }

    if (pwalletMain->GetBalance() < JNODE_COIN_REQUIRED * COIN) {
        LogPrintf("CActiveJnode::ManageStateInitial -- %s: Wallet balance is < 2500 JEM\n", GetStateString());
        return;
    }

    // Choose coins to use
    CPubKey pubKeyCollateral;
    CKey keyCollateral;

    // If collateral is found switch to LOCAL mode
    if (pwalletMain->GetJnodeVinAndKeys(vin, pubKeyCollateral, keyCollateral)) {
        eType = JNODE_LOCAL;
    }

    LogPrint("jnode", "CActiveJnode::ManageStateInitial -- End status = %s, type = %s, pinger enabled = %d\n",
             GetStatus(), GetTypeString(), fPingerEnabled);
}

void CActiveJnode::ManageStateRemote() {
    LogPrint("jnode",
             "CActiveJnode::ManageStateRemote -- Start status = %s, type = %s, pinger enabled = %d, pubKeyJnode.GetID() = %s\n",
             GetStatus(), fPingerEnabled, GetTypeString(), pubKeyJnode.GetID().ToString());

    mnodeman.CheckJnode(pubKeyJnode);
    jnode_info_t infoMn = mnodeman.GetJnodeInfo(pubKeyJnode);

    if (infoMn.fInfoValid) {
        if (infoMn.nProtocolVersion < MIN_JNODE_PAYMENT_PROTO_VERSION_1
                || infoMn.nProtocolVersion > MIN_JNODE_PAYMENT_PROTO_VERSION_2) {
            nState = ACTIVE_JNODE_NOT_CAPABLE;
            strNotCapableReason = "Invalid protocol version";
            LogPrintf("CActiveJnode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if (service != infoMn.addr) {
            nState = ACTIVE_JNODE_NOT_CAPABLE;
            // LogPrintf("service: %s\n", service.ToString());
            // LogPrintf("infoMn.addr: %s\n", infoMn.addr.ToString());
            strNotCapableReason = "Broadcasted IP doesn't match our external address. Make sure you issued a new broadcast if IP of this jnode changed recently.";
            LogPrintf("CActiveJnode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if (!CJnode::IsValidStateForAutoStart(infoMn.nActiveState)) {
            nState = ACTIVE_JNODE_NOT_CAPABLE;
            strNotCapableReason = strprintf("Jnode in %s state", CJnode::StateToString(infoMn.nActiveState));
            LogPrintf("CActiveJnode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if (nState != ACTIVE_JNODE_STARTED) {
            LogPrintf("CActiveJnode::ManageStateRemote -- STARTED!\n");
            vin = infoMn.vin;
            service = infoMn.addr;
            fPingerEnabled = true;
            nState = ACTIVE_JNODE_STARTED;
        }
    } else {
        nState = ACTIVE_JNODE_NOT_CAPABLE;
        strNotCapableReason = "Jnode not in jnode list";
        LogPrintf("CActiveJnode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
    }
}

void CActiveJnode::ManageStateLocal() {
    LogPrint("jnode", "CActiveJnode::ManageStateLocal -- status = %s, type = %s, pinger enabled = %d\n",
             GetStatus(), GetTypeString(), fPingerEnabled);
    if (nState == ACTIVE_JNODE_STARTED) {
        return;
    }

    // Choose coins to use
    CPubKey pubKeyCollateral;
    CKey keyCollateral;

    if (pwalletMain->GetJnodeVinAndKeys(vin, pubKeyCollateral, keyCollateral)) {
        int nInputAge = GetInputAge(vin);
        if (nInputAge < Params().GetConsensus().nJnodeMinimumConfirmations) {
            nState = ACTIVE_JNODE_INPUT_TOO_NEW;
            strNotCapableReason = strprintf(_("%s - %d confirmations"), GetStatus(), nInputAge);
            LogPrintf("CActiveJnode::ManageStateLocal -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }

        {
            LOCK(pwalletMain->cs_wallet);
            pwalletMain->LockCoin(vin.prevout);
        }

        CJnodeBroadcast mnb;
        std::string strError;
        if (!CJnodeBroadcast::Create(vin, service, keyCollateral, pubKeyCollateral, keyJnode,
                                     pubKeyJnode, strError, mnb)) {
            nState = ACTIVE_JNODE_NOT_CAPABLE;
            strNotCapableReason = "Error creating mastenode broadcast: " + strError;
            LogPrintf("CActiveJnode::ManageStateLocal -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }

        fPingerEnabled = true;
        nState = ACTIVE_JNODE_STARTED;

        //update to jnode list
        LogPrintf("CActiveJnode::ManageStateLocal -- Update Jnode List\n");
        mnodeman.UpdateJnodeList(mnb);
        mnodeman.NotifyJnodeUpdates();

        //send to all peers
        LogPrintf("CActiveJnode::ManageStateLocal -- Relay broadcast, vin=%s\n", vin.ToString());
        mnb.RelayJNode();
    }
}
