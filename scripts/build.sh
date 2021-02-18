#!/bin/bash

# unlock wallet
cleos wallet unlock --password $(cat ~/eosio-wallet/.pass)

# build
blanc++ zap.sx.cpp -I ..
cleos set contract zap.sx . zap.sx.wasm zap.sx.abi
