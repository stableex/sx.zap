#include <eosio/print.hpp>
#include <sx.utils/utils.hpp>
#include <sx.curve/curve.sx.hpp>
#include <eosio.token/eosio.token.hpp>
#include <sx.defilend/defilend.hpp>

#include "zap.sx.hpp"

namespace sx {

[[eosio::on_notify("*::transfer")]]
void zap::on_transfer( const name from, const name to, const asset quantity, const string memo )
{
    // authenticate incoming `from` account
    require_auth( from );

    // ignore transfers
    if ( to != get_self() || memo == get_self().to_string() || from == "eosio.ram"_n || from == CURVE_CONTRACT || from == defilend::code) return;

    // user input
    const extended_asset ext_in = { quantity, get_first_receiver() };
    const symbol_code symcode = utils::parse_symbol_code(memo);
    check( symcode.raw(), "zap.sx::on_transfer: wrong memo format (ex: \"SXA\")");

    extended_symbol lp_ext_sym = get_curve_token(symcode);
    if( lp_ext_sym.get_symbol().is_valid() ){
        do_deposit(ext_in, lp_ext_sym, from);
        return;
    }

    lp_ext_sym = get_curve_token(quantity.symbol.code());
    if( lp_ext_sym.get_symbol().is_valid() ){
        do_withdraw(ext_in, symcode, from);
        return;
    }

    check(false, "zap.sx::on_transfer: wrong memo format");

}

extended_symbol zap::get_curve_token(const symbol_code& symcode)
{
    sx::curve::pairs_table _pairs( CURVE_CONTRACT, CURVE_CONTRACT.value );
    auto pairs = _pairs.find( symcode.raw() );
    if(pairs == _pairs.end()) return {};

    return pairs->liquidity.get_extended_symbol();
}

bool zap::is_wrapped_pair(const symbol_code& symcode)
{
    sx::curve::pairs_table _pairs( CURVE_CONTRACT, CURVE_CONTRACT.value );
    auto pairs = _pairs.find( symcode.raw() );
    if(pairs == _pairs.end()) return false;

    return defilend::is_btoken(pairs->reserve0.quantity.symbol.code()) && defilend::is_btoken(pairs->reserve1.quantity.symbol.code());
}

void zap::do_deposit(const extended_asset& ext_quantity, const extended_symbol& ext_sym_lptoken, const name& owner )
{
    auto ext_in = ext_quantity;
    bool must_wrap = !defilend::is_btoken(ext_in.quantity.symbol.code()) && is_wrapped_pair(ext_sym_lptoken.get_symbol().code());
    if(must_wrap) ext_in = defilend::wrap(ext_in.quantity);

    // calculate curve split
    const symbol_code symcode = ext_sym_lptoken.get_symbol().code();
    const auto [ ext_in0, ext_in1] = get_curve_split( ext_in, symcode );
    const extended_symbol ext_sym0 = ext_in0.get_extended_symbol();
    const extended_symbol ext_sym1 = ext_in1.get_extended_symbol();

    // // make sure zap.sx account is clean of tokens
    const extended_asset bal0 = utils::get_balance( ext_sym0, get_self() );
    const extended_asset bal1 = utils::get_balance( ext_sym1, get_self() );
    const extended_asset bal_lptokens = utils::get_balance( ext_sym_lptoken, get_self() );
    check( ( must_wrap && bal0.quantity.amount == 0 || bal0 == ext_in ) && bal1.quantity.amount == 0 && bal_lptokens.quantity.amount == 0, "zap.sx::on_transfer: balance not clean");

    flush_action flush( get_self(), { get_self(), "active"_n } );
    if(must_wrap) {
        defilend::unstake(get_self(), get_self(), ext_in.quantity.symbol.code());
        flush.send( ext_quantity.get_extended_symbol(), defilend::code, "deposit");
    }

    // swap part of tokens for deposit
    transfer( get_self(), CURVE_CONTRACT, ext_in - ext_in0, "swap,0," + symcode.to_string() );

    // deposit pair to curve.sx
    flush.send( ext_sym0, CURVE_CONTRACT, "deposit," + symcode.to_string() );
    flush.send( ext_sym1, CURVE_CONTRACT, "deposit," + symcode.to_string() );
    deposit( symcode );

    // send excess back to sender
    flush.send( ext_sym0, owner, "excess" );
    flush.send( ext_sym1, owner, "excess" );

    // send lptoken.sx to sender
    flush.send( ext_sym_lptoken, owner, "liquidity" );
}

void zap::do_withdraw(const extended_asset& ext_in, const symbol_code& symcode_to, const name& owner )
{
    const auto pair_id = ext_in.quantity.symbol.code();

    sx::curve::pairs_table _pairs( CURVE_CONTRACT, CURVE_CONTRACT.value );
    auto pairs = _pairs.get( pair_id.raw(), "zap.sx::do_withdraw: `pair_id` does not exist on curve.sx");
    check(ext_in.get_extended_symbol() == pairs.liquidity.get_extended_symbol(), "zap.sx::do_withdraw: invalid liquidity token");

    const auto btoken_to = defilend::get_btoken(symcode_to).get_symbol().code();
    if(pairs.reserve0.quantity.symbol.code() != symcode_to && pairs.reserve0.quantity.symbol.code() != btoken_to) swap(pairs.reserve0, pairs.reserve1);
    check(pairs.reserve0.quantity.symbol.code() == symcode_to || pairs.reserve0.quantity.symbol.code() == btoken_to, "zap.sx::do_withdraw: no such token in pair");

    const extended_symbol ext_sym0 = pairs.reserve0.get_extended_symbol();
    const extended_symbol ext_sym1 = pairs.reserve1.get_extended_symbol();

    const extended_asset bal0 = utils::get_balance( ext_sym0, get_self() );
    const extended_asset bal1 = utils::get_balance( ext_sym1, get_self() );
    check( bal0.quantity.amount == 0 && bal1.quantity.amount == 0, "zap.sx::do_withdraw: balance not clean");

    // withdraw
    transfer( get_self(), CURVE_CONTRACT, ext_in, "");

    // convert sym1 to sym0
    flush_action flush( get_self(), { get_self(), "active"_n } );
    flush.send( ext_sym1, CURVE_CONTRACT, "swap,0," + pair_id.to_string() );

    //if target symbol - one of the reserves: flush it, otherwise: try to unwrap it
    if(ext_sym0.get_symbol().code() == symcode_to){
        flush.send( ext_sym0, owner, "Curve.sx: withdraw" );
    }
    else {
        const auto out = defilend::unwrap(bal0.quantity);
        check(out.quantity.symbol.code() == symcode_to, "zap.sx::do_withdraw: can't unwrap token");
        flush.send( ext_sym0, defilend::code, "");      //unwrap
        flush.send( out.get_extended_symbol(), owner, "Curve.sx: withdraw" );
    }
}



pair<extended_asset, extended_asset> zap::get_curve_split(const extended_asset ext_in, const symbol_code pair_id) {

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
    int i=20;    //20 iterations for binary search or until we get close enough
    while(i-- && r-l > in_amount/1000000 ){
        in0_amount = (r + l)/2;
        auto in1_0_amount = (in_amount - in0_amount)*(10000 - config.protocol_fee)/10000;   //how much will be added in res0 after swap
        in1_amount = Curve::get_amount_out(in1_0_amount, res0_amount, res1_amount, amp, config.trade_fee);

        if(in0_amount * (res0_amount + in1_0_amount + res1_amount - in1_amount) > (res0_amount + in1_0_amount) * (in0_amount + in1_amount))
            r = in0_amount;
        else
            l = in0_amount;
    }
    const auto in0 = extended_asset { sx::curve::div_amount(static_cast<int64_t>(in0_amount), MAX_PRECISION, precision0), ext_in.get_extended_symbol() };
    const auto in1 = extended_asset { sx::curve::div_amount(static_cast<int64_t>(in1_amount), MAX_PRECISION, precision1), pairs.reserve1.get_extended_symbol() };

    return { in0, in1 };
}


[[eosio::action]]
void zap::flush( const extended_symbol ext_sym, const name to, const string memo )
{
    require_auth( get_self() );

    const extended_asset balance = utils::get_balance( ext_sym, get_self() );
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

}