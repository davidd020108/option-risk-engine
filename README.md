# Option Risk Engine (C++ / SQL)

A small risk system for an equity option portfolio. A C++17 pricing library
values every position and computes its Greeks, a stress-testing engine
revalues the book under 27 market scenarios, and everything is stored in a
SQLite database where SQL reports aggregate the risk by book, underlying and
scenario.

It extends my [option-pricing project](https://github.com/davidd020108/2025-Finance-Quant-Camp)
from the Erdős Institute (Python) into a compiled pricer with a database layer,
which is closer to how a desk risk system is organised.

```
             +-------------------+          +-----------------------------+
 sql/seed -> |  SQLite database  | <------> |  C++ engine (ore_run)       |
             |                   |          |  - Black-Scholes + Greeks   |
             |  instruments      |  read    |  - CRR binomial (American)  |
             |  market_data      |  ------> |  - Monte Carlo (antithetic) |
             |  positions        |          |  - implied vol (bisection)  |
             |                   |  write   |  - scenario revaluation     |
             |  valuations      | <------  +-----------------------------+
             |  scenario_pnl     |
             |  model_checks     |  ---->  sql/risk_reports.sql (CTEs, window functions)
             +-------------------+
```

## What it does

**Pricing (C++, `src/pricing.cpp`)**

- Black-Scholes-Merton prices and analytic Greeks (delta, gamma, vega, theta, rho) with a continuous dividend yield.
- Cox-Ross-Rubinstein binomial tree for American options. Delta, gamma and theta are read off the first layers of the same tree; vega and rho come from central bumps.
- Monte Carlo for European options with antithetic variates and standard errors.
- Implied volatility by bisection.

**Risk (C++ writes, SQL reads)**

- Values 14 positions in two books (a long-volatility book and an option-selling book) on 3 underlyings.
- Revalues every position under a 9 x 3 grid of spot moves (-20% to +20%) and vol moves (-5 to +5 vol points): 378 full revaluations, written in a single transaction.
- Stores the delta-gamma-vega Taylor estimate beside each full revaluation, so SQL can show where the approximation fails.
- Cross-checks every instrument across methods: closed form vs. 1,000-step tree vs. 200,000-path Monte Carlo, plus the early-exercise premium of each American option.

**SQL (`sql/`)**

- `schema.sql`: normalised tables with foreign keys and CHECK constraints, plus a `position_risk` view that converts per-unit Greeks into money terms (dollar delta, dollar gamma per 1% move, vega per vol point, theta per day).
- `risk_reports.sql`: seven reports using CTEs, `UNION ALL` totals, conditional aggregation (a pivot), and window functions (`RANK`, `ROW_NUMBER`, `SUM() OVER ()`).

## Build and run

Requires a C++17 compiler, CMake 3.16+ and SQLite3 development headers
(`brew install cmake sqlite` on macOS, `apt install cmake libsqlite3-dev` on Ubuntu).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/test_pricing                 # unit tests
./build/ore_run build/risk.db sql    # price, stress-test, print reports
```

The database is left at `build/risk.db`, so you can keep querying it with `sqlite3 build/risk.db`.

## Tests

`tests/test_pricing.cpp` checks properties any correct pricer must satisfy:

- Black-Scholes against Hull's textbook reference values (call 4.7594, put 0.8086).
- Put-call parity with dividends, to 1e-10.
- The binomial tree converges to Black-Scholes, and its error shrinks as steps increase.
- An American put is worth more than the European put; a deep in-the-money American put is worth at least its intrinsic value; an American call on a non-dividend stock equals the European call.
- Monte Carlo lands within 3 standard errors of the closed form, and antithetic variates reduce the standard error.
- Analytic delta, gamma, vega and theta match finite differences; tree Greeks match the closed form.
- Implied volatility recovers the input volatility to 1e-8.

## Sample output

The market data and positions in `sql/seed.sql` are fictional.

```
Priced 14 positions under 27 scenarios (378 full revaluations) in 420 ms. Results written to build/risk.db.
```

**Portfolio Greeks by book**

```
     book  positions  market_value  dollar_delta  gamma_1pct  vega_per_pt  theta_per_day
----------------------------------------------------------------------------------------
 LONG_VOL          7         53162        184535       20279         2373           -259
SHORT_VOL          7        -99247        308813      -25786        -3832            289
    TOTAL         14        -46085        493347       -5506        -1459             30
```

**Portfolio P&L grid (full revaluation)**: the book is net long delta and net short vega, so the worst case is a sell-off combined with a volatility spike.

```
spot_move  vol -5pt  vol flat  vol +5pt
---------------------------------------
     -20%   -109686   -112907   -117290
     -15%    -76653    -81532    -87148
     -10%    -46473    -52467    -58975
      -5%    -18734    -25414    -32477
      +0%      7101         0     -7473
      +5%     31422     23986     16272
     +10%     54458     46794     38911
     +15%     76141     68443     60531
     +20%     96440     88981     81213
```

**Where the delta-gamma-vega approximation breaks down**: the Taylor estimate is accurate for small moves, but the error grows quickly with the size of the move (about 3,200 at -20%), which is why stress tests use full revaluation.

```
spot_move  full_reval_pnl  taylor_pnl  approx_error
---------------------------------------------------
     -20%         -112907     -109682          3225
     -15%          -81532      -80197          1335
     -10%          -52467      -52088           379
      -5%          -25414      -25356            58
      +0%               0           0             0
      +5%           23986       23979            -7
     +10%           46794       46582          -213
     +15%           68443       67807          -635
     +20%           88981       87657         -1324
```

**Model validation**: the tree is within 0.002 of the closed form for every European option, and every Monte Carlo price lies within 3 standard errors.

```
id  symbol  type  strike  maturity  closed_form  tree_1000  tree_minus_cf  monte_carlo   mc_se  mc_within_3se  early_ex_premium
-------------------------------------------------------------------------------------------------------------------------------
 1    TECH    CE     100      0.25       7.4467      7.445        -0.0017       7.4784  0.0202           PASS                 -
 2    TECH    PE      90      0.25       2.5223     2.5235         0.0012       2.5109  0.0106           PASS                 -
 3    TECH    CE     120         1       8.5117     8.5127          0.001       8.5261  0.0418           PASS                 -
 4    TECH    PA     100       0.5            -     8.9634              -            -       -              -            0.1711
 5    BANK    CE      50       0.5       2.9739     2.9732        -0.0008       2.9791  0.0085           PASS                 -
 6    BANK    PE      45       0.5       1.0998     1.1001         0.0003       1.1028  0.0047           PASS                 -
 7    BANK    CA      48         1            -     5.0895              -            -       -              -            0.0812
 8    BANK    PA      55         1            -     7.4319              -            -       -              -            0.0543
 9    ENGY    CE      80      0.25       4.8409     4.8398        -0.0012       4.8542  0.0133           PASS                 -
10    ENGY    PE      70      0.75       3.4864      3.486        -0.0003       3.4792  0.0122           PASS                 -
11    ENGY    PA      85       0.5            -     9.4206              -            -       -              -            0.0861
12    ENGY    CE      95         1       4.6668     4.6653        -0.0015       4.6655  0.0248           PASS                 -
```

## Project layout

```
include/pricing.hpp   pricing API
include/db.hpp        RAII wrappers for the SQLite C API (Database, Statement, Transaction)
src/pricing.cpp       Black-Scholes, binomial tree, Monte Carlo, implied vol
src/db.cpp            SQLite wrapper implementation
src/main.cpp          loads the portfolio, writes valuations, scenarios and checks, prints reports
sql/schema.sql        tables, constraints and the position_risk view
sql/seed.sql          sample instruments, market data and positions
sql/risk_reports.sql  risk reports
tests/test_pricing.cpp
```

## Possible extensions

- A volatility surface by strike and maturity instead of one volatility per underlying.
- Historical-simulation VaR from a table of daily returns.
- Parallel scenario revaluation with `std::thread`.
