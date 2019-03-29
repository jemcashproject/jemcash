Jnode Build Instructions and Notes
=============================
 - Version 0.1.6
 - Date: 14 December 2017
 - More detailed guide available here: https://jemcash.info/jemcash-jnode-setup-guide/

Prerequisites
-------------
 - Ubuntu 16.04+
 - Libraries to build from jemcash source
 - Port **2810** is open

Step 1. Build
----------------------
**1.1.**  Check out from source:

    git clone https://github.com/jemcashproject/jemcash

**1.2.**  See [README.md](README.md) for instructions on building.

Step 2. (Optional - only if firewall is running). Open port 2810
----------------------
**2.1.**  Run:

    sudo ufw allow 2810
    sudo ufw default allow outgoing
    sudo ufw enable

Step 3. First run on your Local Wallet
----------------------
**3.0.**  Go to the checked out folder

    cd jemcash

**3.1.**  Start daemon in testnet mode:

    ./src/jemcashd -daemon -server -testnet

**3.2.**  Generate jnodeprivkey:

    ./src/jemcash-cli jnode genkey

(Store this key)

**3.3.**  Get wallet address:

    ./src/jemcash-cli getaccountaddress 0

**3.4.**  Send to received address **exactly 1000 JEM** in **1 transaction**. Wait for 15 confirmations.

**3.5.**  Stop daemon:

    ./src/jemcash-cli stop

Step 4. In your VPS where you are hosting your Jnode. Update config files
----------------------
**4.1.**  Create file **jemcash.conf** (in folder **~/.jemcash**)

    rpcuser=username
    rpcpassword=password
    rpcallowip=127.0.0.1
    debug=1
    txindex=1
    daemon=1
    server=1
    listen=1
    maxconnections=24
    jnode=1
    jnodeprivkey=XXXXXXXXXXXXXXXXX  ## Replace with your jnode private key
    externalip=XXX.XXX.XXX.XXX:2810 ## Replace with your node external IP

**4.2.**  Create file **jnode.conf** (in 2 folders **~/.jemcash** and **~/.jemcash/testnet3**) contains the following info:
 - LABEL: A one word name you make up to call your node (ex. JN1)
 - IP:PORT: Your jnode VPS's IP, and the port is always 2811.
 - JNODEPRIVKEY: This is the result of your "jnode genkey" from earlier.
 - TRANSACTION HASH: The collateral tx. hash from the 1000 JEM deposit.
 - INDEX: The Index is always 0 or 1.

To get TRANSACTION HASH, run:

    ./src/jemcash-cli jnode outputs

The output will look like:

    { "d6fd38868bb8f9958e34d5155437d009b72dfd33fc28874c87fd42e51c0f74fdb" : "0", }

Sample of jnode.conf:

    JN1 51.52.53.54:2811 XrxSr3fXpX3dZcU7CoiFuFWqeHYw83r28btCFfIHqf6zkMp1PZ4 d6fd38868bb8f9958e34d5155437d009b72dfd33fc28874c87fd42e51c0f74fdb 0

Step 5. Run a jnode
----------------------
**5.1.**  Start jnode:

    ./src/jemcash-cli jnode start-alias <LABEL>

For example:

    ./src/jemcash-cli jnode start-alias JN1

**5.2.**  To check node status:

    ./src/jemcash-cli jnode debug

If not successfully started, just repeat start command
