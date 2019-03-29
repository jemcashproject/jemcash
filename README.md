Jemcash v1.0.0
===============

[![Build Status](https://travis-ci.com/jemcashofficial/jemcash.svg?branch=CI)](https://travis-ci.com/jemcashofficial/jemcash)

What is Jemcash?
--------------

[Jemcash](https://jemcash.info) uses the Zerocoin Protocol, which allows users to have complete privacy via Zero-Knowledge cryptographic proofs. It is worth noting that Jemcash is unrelated to other cryptocurrencies utilizing the Zerocash Protocol. Although Zerocash is a development from Zerocoin, their respective implementations are not simple forks of each other, but rely on different cryptographic assumptions with various tradeoffs. Both approaches supplement each other quite nicely, and a good way to describe them would be sibling projects.

The Zerocoin Protocol is being actively researched and improved, such as removing the trustless setup and reducing proof sizes.


Linux Build Instructions and Notes
==================================

Dependencies
----------------------
1.  Update packages

        sudo apt-get update

2.  Install required packages

        sudo apt-get install build-essential libtool autotools-dev automake pkg-config libssl-dev libevent-dev bsdmainutils libboost-all-dev

3.  Install Berkeley DB 4.8

        sudo apt-get install software-properties-common
        sudo add-apt-repository ppa:bitcoin/bitcoin
        sudo apt-get update
        sudo apt-get install libdb4.8-dev libdb4.8++-dev

4.  Install QT 5

        sudo apt-get install libminiupnpc-dev libzmq3-dev
        sudo apt-get install libqt5gui5 libqt5core5a libqt5dbus5 qttools5-dev qttools5-dev-tools libprotobuf-dev protobuf-compiler libqrencode-dev

Build
----------------------
1.  Clone the source:

        git clone https://github.com/jemcashproject/jemcash

2.  Build Jemcash-core:

    Configure and build the headless Jemcash binaries as well as the GUI (if Qt is found).

    You can disable the GUI build by passing `--without-gui` to configure.
        
        ./autogen.sh
        ./configure
        make

3.  It is recommended to build and run the unit tests:

        make check


macOS Build Instructions and Notes
=====================================
See (doc/build-macos.md) for instructions on building on macOS.



Windows (64/32 bit) Build Instructions and Notes
=====================================
See (doc/build-windows.md) for instructions on building on Windows 64/32 bit.
