#include <eosio/print.hpp>
#include <sx.utils/utils.hpp>
#include <sx.curve/curve.sx.hpp>
#include <eosio.token/eosio.token.hpp>

#include "zap.sx.hpp"


[[eosio::on_notify("*::transfer")]]
void sx::zap::on_transfer( const name from, const name to, const asset quantity, const string memo )
{
    // authenticate incoming `from` account
    require_auth( from );

    // ignore transfers
    if ( to != get_self() || memo == get_self().to_string() || from == "eosio.ram"_n || from == CURVE_CONTRACT) return;

    const symbol_code symcode = utils::parse_symbol_code(memo);
    check(symcode.raw(), "Zap.sx: wrong memo format");

    const auto [ ext_in0, ext_in1, ext_lp_sym] = get_curve_split(extended_asset{ quantity, get_first_receiver()}, symcode);

    print("\n", quantity, " => ", ext_in0.quantity, " + ", ext_in1.quantity);

    //make sure zap.sx account is clean of tokens
    const asset bal0 = eosio::token::get_balance( ext_in0.contract, get_self(), ext_in0.quantity.symbol.code() );
   // const asset bal1 = eosio::token::get_balance( ext_in1.contract, get_self(), ext_in1.quantity.symbol.code() );
   // const asset bal_lptokens = eosio::token::get_balance( ext_lp_sym.get_contract(), get_self(), symcode );
    check(bal0 == quantity /*&& bal1.amount == 0 && bal_lptokens.amount == 0*/, "Zap.sx: balance not clean");


    eosio::token::transfer_action transfer( ext_in0.contract, permission_level{ get_self(), "active"_n } );
    flush_action flush0( ext_in0.contract, permission_level{ get_self(), "active"_n } );
    flush_action flush1( ext_in1.contract, permission_level{ get_self(), "active"_n } );
    flush_action flushltoken( ext_lp_sym.get_contract(), permission_level{ get_self(), "active"_n } );
    sx::curve::deposit_action deposit( CURVE_CONTRACT, permission_level{ get_self(), "active"_n } );

    //swap for second liquidity token
    transfer.send( get_self(), CURVE_CONTRACT, ext_in0.quantity, "swap,0," + symcode.to_string() );

    //deposit liquidity
    // flush0.send( ext_in0.get_extended_symbol(), CURVE_CONTRACT, "deposit," + symcode.to_string(), 1 );
    // flush1.send( ext_in1.get_extended_symbol(), CURVE_CONTRACT, "deposit," + symcode.to_string(), 1 );
    // deposit.send( from, symcode );

    // //send excess
    // flush0.send( ext_in0.get_extended_symbol(), from, "excess", 0 );
    // flush1.send( ext_in1.get_extended_symbol(), from, "excess", 0 );

    //flush lp tokens
    flushltoken.send( ext_lp_sym, from, "liquidity", 1 );
}


tuple<extended_asset, extended_asset, extended_symbol> sx::zap::get_curve_split(const extended_asset ext_in, const symbol_code pair_id) {

    sx::curve::pairs_table _pairs( CURVE_CONTRACT, CURVE_CONTRACT.value );
    auto pairs = _pairs.get( pair_id.raw(), "Zap.sx: `pair_id` does not exist on Curve.sx");

    if(pairs.reserve0.get_extended_symbol() != ext_in.get_extended_symbol()) std::swap(pairs.reserve0, pairs.reserve1);
    check(pairs.reserve0.get_extended_symbol() == ext_in.get_extended_symbol(), "Zap.sx: Invalid token for this `pair_id`");

    const auto amp = sx::curve::get_amplifier(pair_id);
    print("\nReserves: ", pairs.reserve0.quantity, " & ", pairs.reserve1.quantity);

    const uint8_t precision0 = pairs.reserve0.quantity.symbol.precision();
    const uint8_t precision1 = pairs.reserve1.quantity.symbol.precision();
    const uint8_t precision_in = ext_in.quantity.symbol.precision();
    const int128_t res0_amount = sx::curve::mul_amount( pairs.reserve0.quantity.amount, MAX_PRECISION, precision0 );
    const int128_t res1_amount = sx::curve::mul_amount( pairs.reserve1.quantity.amount, MAX_PRECISION, precision1 );
    const int128_t in_amount = sx::curve::mul_amount( ext_in.quantity.amount, MAX_PRECISION, precision_in );

    const auto in0_amount = (in_amount * res0_amount) / (res0_amount + res1_amount);
    const auto in0 = extended_asset { sx::curve::div_amount(static_cast<int64_t>(in0_amount), MAX_PRECISION, precision0), ext_in.get_extended_symbol() };
    const auto in1 = extended_asset { sx::curve::div_amount(static_cast<int64_t>(in_amount - in0_amount), MAX_PRECISION, precision1), pairs.reserve1.get_extended_symbol() };

    return { in0, in1, pairs.liquidity.get_extended_symbol() };
}

[[eosio::action]]
void sx::zap::flush(const extended_symbol& ext_sym, const name to, const string& memo, uint64_t min)
{
    require_auth( get_self() );

    const asset balance = eosio::token::get_balance( ext_sym.get_contract(), get_self(), ext_sym.get_symbol().code() );
    check(balance.amount >= 0, "Zap.sx: Nothing to transfer");

    eosio::token::transfer_action transfer( ext_sym.get_contract(), permission_level{ get_self(), "active"_n } );
    transfer.send( get_self(), to, balance, memo );
}
