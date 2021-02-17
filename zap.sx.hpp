#pragma once

#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>

using namespace eosio;
using namespace std;

static constexpr name CURVE_CONTRACT = "curve.sx"_n;

namespace sx {
class [[eosio::contract("zap.sx")]] zap : public eosio::contract {
public:
    zap(name rec, name code, datastream<const char*> ds)
      : contract(rec, code, ds)
    {};

    [[eosio::on_notify("*::transfer")]]
    void on_transfer( const name from, const name to, const asset quantity, const std::string memo );

    //transfer {ext_quantity} to {to}. If {ext_quantity}==0 - flush all
    [[eosio::action]]
    void flush(const extended_symbol& ext_sym, name to, const string& memo, uint64_t min);
    using flush_action = action_wrapper<"flush"_n, &zap::flush>;

private:

    //based on ext_in and pair_id find best split between reserves
    tuple<extended_asset, extended_asset, extended_symbol> get_curve_split(const extended_asset ext_in, const symbol_code pair_id);
};
}
