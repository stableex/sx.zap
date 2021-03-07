# **`SX Zap`**

> Peripheral EOSIO smart contract to simplify SX Curve deposit/withdrawal

## Quickstart

### Deposit

> Deposit liquidity using only one of the pool tokens.
> Transfer tokens with memo: `<pair_id>`. Zap.SX will split transferred amount, convert portion on Curve.SX, deposit everything to Curve.SX and send back liquidity tokens. 

```bash
$ cleos transfer myaccount zap.sx "10.0000 USDT" "SXA" --contract tethertether
# => receive corresponding SXA liquidity tokens and excess if there is any
```

### Withdraw

> Withdraw liquidity in desired tokens 
> Transfer liquidity tokens with the desired token symbol code as memo. Zap.SX will withdraw liquidity, convert to the desired tokens on Curve.SX and send all tokens to the owner. 

```bash
$ cleos transfer myaccount zap.sx "10.0000 SXA" "USDT" --contract lptoken.sx
# => receive corresponding liquidity converted to USDT
```
