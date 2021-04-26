#!/bin/bash

# unlock wallet
cleos wallet unlock --password $(cat ~/eosio-wallet/.pass)

cleos transfer myaccount zap.sx "1000.0000 A" "AB"

cleos transfer myaccount zap.sx "1000.0000 B" "AB"