// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ACTIVEJNODE_H
#define ACTIVEJNODE_H

#include "net.h"
#include "key.h"
#include "wallet/wallet.h"

class CActiveJnode;

static const int ACTIVE_JNODE_INITIAL          = 0; // initial state
static const int ACTIVE_JNODE_SYNC_IN_PROCESS  = 1;
static const int ACTIVE_JNODE_INPUT_TOO_NEW    = 2;
static const int ACTIVE_JNODE_NOT_CAPABLE      = 3;
static const int ACTIVE_JNODE_STARTED          = 4;

extern CActiveJnode activeJnode;

// Responsible for activating the Jnode and pinging the network
class CActiveJnode
{
public:
    enum jnode_type_enum_t {
        JNODE_UNKNOWN = 0,
        JNODE_REMOTE  = 1,
        JNODE_LOCAL   = 2
    };

private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    jnode_type_enum_t eType;

    bool fPingerEnabled;

    /// Ping Jnode
    bool SendJnodePing();

public:
    // Keys for the active Jnode
    CPubKey pubKeyJnode;
    CKey keyJnode;

    // Initialized while registering Jnode
    CTxIn vin;
    CService service;

    int nState; // should be one of ACTIVE_JNODE_XXXX
    std::string strNotCapableReason;

    CActiveJnode()
        : eType(JNODE_UNKNOWN),
          fPingerEnabled(false),
          pubKeyJnode(),
          keyJnode(),
          vin(),
          service(),
          nState(ACTIVE_JNODE_INITIAL)
    {}

    /// Manage state of active Jnode
    void ManageState();

    std::string GetStateString() const;
    std::string GetStatus() const;
    std::string GetTypeString() const;

private:
    void ManageStateInitial();
    void ManageStateRemote();
    void ManageStateLocal();
};

#endif
