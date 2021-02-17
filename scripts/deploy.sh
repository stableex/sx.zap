#!/bin/bash

# unlock wallet
cleos wallet unlock --password $(cat ~/eosio-wallet/.pass)

blanc++ zap.sx.cpp -I ..

# create account
cleos create account eosio zap.sx EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV

# contract
cleos set contract zap.sx . zap.sx.wasm zap.sx.abi

# @eosio.code permission
cleos set account permission zap.sx active --add-code
