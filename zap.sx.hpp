#pragma once

#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>

using namespace eosio;
using namespace std;

static constexpr name CURVE_CONTRACT = "curve.sx"_n;

namespace sx {

class [[eosio::contract("zap.sx")]] zap : public eosio::contract {
public:
    using contract::contract;

    [[eosio::on_notify("*::transfer")]]
    void on_transfer( const name from, const name to, const asset quantity, const std::string memo );

    // flush all of {ext_sym} to {to} with {memo}, fail if balance amount < {min}
    [[eosio::action]]
    void flush( const extended_symbol ext_sym, const name to, const string memo );
    using flush_action = action_wrapper<"flush"_n, &zap::flush>;

private:

    // based on {ext_in} and {pair_id} find proper split for incoming deposit
    pair<extended_asset, extended_asset> get_curve_split(const extended_asset ext_in, const symbol_code pair_id);

    // find lp token {symcode} in pairs
    extended_symbol get_curve_token(const symbol_code& symcode);

    bool is_wrapped_pair(const symbol_code& symcode);

    // process deposit {ext_in} => {ext_sym_lptoken} for {owner}, i.e. USDT => SXA
    void do_deposit(const extended_asset& ext_in, const extended_symbol& ext_sym_lptoken, const name& owner );

    // process withdrawal {ext_in} => {symcode} for {owner}, i.e. SXA => USN
    void do_withdraw(const extended_asset& ext_in, const symbol_code& symcode, const name& owner );

    // eosio.token helpers
    void transfer( const name from, const name to, const extended_asset value, const string memo );

    // sx.curve helpers
    void deposit( const symbol_code symcode );
};

}
