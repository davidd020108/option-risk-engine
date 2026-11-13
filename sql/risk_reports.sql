-- Risk reports run against the database after the C++ engine has filled
-- valuations, scenario_pnl and model_checks.
-- Each query is preceded by a "-- report:" line that ore_run prints as a title.

-- report: 1. Portfolio Greeks by book (money terms)
WITH by_book AS (
    SELECT
        book,
        COUNT(*)               AS positions,
        SUM(market_value)      AS market_value,
        SUM(dollar_delta)      AS dollar_delta,
        SUM(dollar_gamma_1pct) AS gamma_1pct,
        SUM(vega_per_pt)       AS vega_per_pt,
        SUM(theta_per_day)     AS theta_per_day,
        0                      AS sort_key
    FROM position_risk
    GROUP BY book
    UNION ALL
    SELECT 'TOTAL', COUNT(*), SUM(market_value), SUM(dollar_delta), SUM(dollar_gamma_1pct),
           SUM(vega_per_pt), SUM(theta_per_day), 1
    FROM position_risk
)
SELECT book, positions,
       ROUND(market_value) AS market_value,
       ROUND(dollar_delta) AS dollar_delta,
       ROUND(gamma_1pct)   AS gamma_1pct,
       ROUND(vega_per_pt)  AS vega_per_pt,
       ROUND(theta_per_day) AS theta_per_day
FROM by_book
ORDER BY sort_key, book;

-- report: 2. Risk concentration by underlying
WITH by_underlying AS (
    SELECT symbol, sector,
           SUM(dollar_delta)      AS dollar_delta,
           SUM(dollar_gamma_1pct) AS gamma_1pct,
           SUM(vega_per_pt)       AS vega_per_pt
    FROM position_risk
    GROUP BY symbol, sector
)
SELECT symbol, sector,
       ROUND(dollar_delta) AS dollar_delta,
       ROUND(gamma_1pct)   AS gamma_1pct,
       ROUND(vega_per_pt)  AS vega_per_pt,
       ROUND(100.0 * ABS(vega_per_pt) / SUM(ABS(vega_per_pt)) OVER (), 1) AS pct_of_abs_vega
FROM by_underlying
ORDER BY ABS(vega_per_pt) DESC;

-- report: 3. Five largest vega positions (with rank inside their book)
SELECT position_id, book, symbol,
       option_type || style AS type,
       strike, maturity_years AS maturity, quantity,
       ROUND(vega_per_pt) AS vega_per_pt,
       RANK() OVER (PARTITION BY book ORDER BY ABS(vega_per_pt) DESC) AS rank_in_book
FROM position_risk
ORDER BY ABS(vega_per_pt) DESC
LIMIT 5;

-- report: 4. Portfolio P&L grid, full revaluation (rows: spot move, columns: vol move)
SELECT printf('%+.0f%%', s.spot_shock * 100) AS spot_move,
       ROUND(SUM(CASE WHEN s.vol_shock < -1e-9 THEN sp.full_reval_pnl END)) AS "vol -5pt",
       ROUND(SUM(CASE WHEN ABS(s.vol_shock) <= 1e-9 THEN sp.full_reval_pnl END)) AS "vol flat",
       ROUND(SUM(CASE WHEN s.vol_shock > 1e-9 THEN sp.full_reval_pnl END)) AS "vol +5pt"
FROM scenario_pnl sp
JOIN scenarios s USING (scenario_id)
GROUP BY s.spot_shock
ORDER BY s.spot_shock;

-- report: 5. Two worst stress scenarios per book
WITH book_pnl AS (
    SELECT p.book, s.spot_shock, s.vol_shock, SUM(sp.full_reval_pnl) AS pnl
    FROM scenario_pnl sp
    JOIN positions p USING (position_id)
    JOIN scenarios s USING (scenario_id)
    GROUP BY p.book, s.scenario_id
),
ranked AS (
    SELECT *, ROW_NUMBER() OVER (PARTITION BY book ORDER BY pnl) AS loss_rank
    FROM book_pnl
)
SELECT book, loss_rank,
       printf('%+.0f%%', spot_shock * 100) AS spot_move,
       printf('%+.0fpt', vol_shock * 100)  AS vol_move,
       ROUND(pnl) AS pnl
FROM ranked
WHERE loss_rank <= 2
ORDER BY book, loss_rank;

-- report: 6. Where the delta-gamma-vega approximation breaks down (vol flat)
SELECT printf('%+.0f%%', s.spot_shock * 100)          AS spot_move,
       ROUND(SUM(sp.full_reval_pnl))                  AS full_reval_pnl,
       ROUND(SUM(sp.taylor_pnl))                      AS taylor_pnl,
       ROUND(SUM(sp.taylor_pnl) - SUM(sp.full_reval_pnl)) AS approx_error
FROM scenario_pnl sp
JOIN scenarios s USING (scenario_id)
WHERE ABS(s.vol_shock) <= 1e-9
GROUP BY s.spot_shock
ORDER BY s.spot_shock;

-- report: 7. Model validation: closed form vs. binomial tree vs. Monte Carlo
SELECT i.instrument_id AS id, i.symbol,
       i.option_type || i.style AS type,
       i.strike, i.maturity_years AS maturity,
       ROUND(c.closed_form_price, 4) AS closed_form,
       ROUND(c.binomial_price, 4)    AS tree_1000,
       ROUND(c.binomial_price - c.closed_form_price, 4) AS tree_minus_cf,
       ROUND(c.mc_price, 4)          AS monte_carlo,
       ROUND(c.mc_std_error, 4)      AS mc_se,
       CASE
           WHEN c.mc_price IS NULL THEN '-'
           WHEN ABS(c.mc_price - c.closed_form_price) <= 3 * c.mc_std_error THEN 'PASS'
           ELSE 'CHECK'
       END AS mc_within_3se,
       ROUND(c.early_exercise_premium, 4) AS early_ex_premium
FROM model_checks c
JOIN instruments i USING (instrument_id)
ORDER BY i.instrument_id;
