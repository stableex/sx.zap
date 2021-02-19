#include <eosio/print.hpp>
#include <sx.utils/utils.hpp>
#include <sx.curve/curve.sx.hpp>
#include <eosio.token/eosio.token.hpp>

#include "zap.sx.hpp"

namespace sx {

[[eosio::on_notify("*::transfer")]]
void zap::on_transfer( const name from, const name to, const asset quantity, const string memo )
{
    // authenticate incoming `from` account
    require_auth( from );

    // ignore transfers
    if ( to != get_self() || memo == get_self().to_string() || from == "eosio.ram"_n || from == CURVE_CONTRACT) return;

    // user input
    const extended_asset ext_in = { quantity, get_first_receiver() };
    const symbol_code symcode = utils::parse_symbol_code(memo);
    check( symcode.raw(), "zap.sx::on_transfer: wrong memo format (ex: \"SXA\")");

    // calculate curve split
    const auto [ ext_in0, ext_in1, ext_lp_sym] = get_curve_split( ext_in, symcode );
    const extended_symbol ext_sym0 = ext_in0.get_extended_symbol();
    const extended_symbol ext_sym1 = ext_in1.get_extended_symbol();

    // // make sure zap.sx account is clean of tokens
    // const asset bal0 = zap::get_balance( ext_sym0, get_self() );
    // const asset bal1 = zap::get_balance( ext_sym1, get_self() );
    // const asset bal_lptokens = zap::get_balance( ext_lp_sym, get_self() );
    // check( bal0 == quantity /*&& bal1.amount == 0 && bal_lptokens.amount == 0*/, "zap.sx::on_transfer: balance not clean");

    // swap part of tokens for deposit
    transfer( get_self(), CURVE_CONTRACT, ext_in - ext_in0, "swap,0," + symcode.to_string() );

    // deposit pair to curve.sx
    flush_action flush( get_self(), { get_self(), "active"_n } );
    flush.send( ext_sym0, CURVE_CONTRACT, "deposit," + symcode.to_string() );
    flush.send( ext_sym1, CURVE_CONTRACT, "deposit," + symcode.to_string() );
    deposit( symcode );

    // send excess back to sender
    flush.send( ext_sym0, from, "excess" );
    flush.send( ext_sym1, from, "excess" );

    // send lptoken.sx to sender
    flush.send( ext_lp_sym, from, "liquidity" );
}


tuple<extended_asset, extended_asset, extended_symbol> zap::get_curve_split(const extended_asset ext_in, const symbol_code pair_id) {

    sx::curve::pairs_table _pairs( CURVE_CONTRACT, CURVE_CONTRACT.value );
    auto pairs = _pairs.get( pair_id.raw(), "zap.sx::get_curve_split: `pair_id` does not exist on curve.sx");

    if (pairs.reserve0.get_extended_symbol() != ext_in.get_extended_symbol()) std::swap(pairs.reserve0, pairs.reserve1);
    check(pairs.reserve0.get_extended_symbol() == ext_in.get_extended_symbol(), "zap.sx::get_curve_split: invalid token for this `pair_id`");

    const auto amp = sx::curve::get_amplifier(pair_id);
    sx::curve::config_table _config( CURVE_CONTRACT, CURVE_CONTRACT.value );
    auto config = _config.get();

    const uint8_t precision0 = pairs.reserve0.quantity.symbol.precision();
    const uint8_t precision1 = pairs.reserve1.quantity.symbol.precision();
    const uint8_t precision_in = ext_in.quantity.symbol.precision();
    const int128_t res0_amount = sx::curve::mul_amount( pairs.reserve0.quantity.amount, MAX_PRECISION, precision0 );
    const int128_t res1_amount = sx::curve::mul_amount( pairs.reserve1.quantity.amount, MAX_PRECISION, precision1 );
    const int128_t in_amount = sx::curve::mul_amount( ext_in.quantity.amount, MAX_PRECISION, precision_in );

    //find best split using binary search
    uint128_t l = 0, r = in_amount;
    int128_t in0_amount = 0, in1_amount = 0;
    int i=20;    //20 iterations for binary search or until we got within 1 bips
    while(i-- && r-l > in_amount/10000 ){
        in0_amount = (r + l)/2;
        in1_amount = Curve::get_amount_out(in_amount - in0_amount, res0_amount, res1_amount, amp, config.trade_fee + config.protocol_fee);

        if(in0_amount * (res0_amount + in_amount - in0_amount + res1_amount - in1_amount) > (res0_amount + in_amount - in0_amount) * (in0_amount + in1_amount))
            r = in0_amount;
        else
            l = in0_amount;
    }
    const auto in0 = extended_asset { sx::curve::div_amount(static_cast<int64_t>(in0_amount), MAX_PRECISION, precision0), ext_in.get_extended_symbol() };
    const auto in1 = extended_asset { sx::curve::div_amount(static_cast<int64_t>(in1_amount), MAX_PRECISION, precision1), pairs.reserve1.get_extended_symbol() };

    return { in0, in1, pairs.liquidity.get_extended_symbol() };
}


[[eosio::action]]
void zap::flush( const extended_symbol ext_sym, const name to, const string memo )
{
    require_auth( get_self() );

    const extended_asset balance = zap::get_balance( ext_sym, get_self() );
    if ( balance.quantity.amount > 0 ) transfer( get_self(), to, balance, memo );
}

// sx.curve helpers
void zap::deposit( const symbol_code symcode )
{
    sx::curve::deposit_action deposit( CURVE_CONTRACT, { get_self(), "active"_n } );
    deposit.send( get_self(), symcode );
}

// eosio.token helpers
void zap::transfer( const name from, const name to, const extended_asset value, const string memo )
{
    eosio::token::transfer_action transfer( value.contract, { from, "active"_n });
    transfer.send( from, to, value.quantity, memo );
}

extended_asset zap::get_balance( const extended_symbol ext_sym, const name owner )
{
    eosio::token::accounts _accounts( ext_sym.get_contract(), owner.value );
    auto accounts = _accounts.find( ext_sym.get_symbol().code().raw() );
    if ( accounts == _accounts.end() ) return { 0, ext_sym };
    check( ext_sym.get_symbol() == accounts->balance.symbol, "zap::get_balanace: extended symbol mismatch balance");
    return { accounts->balance.amount, ext_sym };
}

}