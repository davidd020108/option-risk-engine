-- Schema for the option risk database.
-- Re-running this file rebuilds every table from scratch.

PRAGMA foreign_keys = OFF;
DROP VIEW  IF EXISTS position_risk;
DROP TABLE IF EXISTS model_checks;
DROP TABLE IF EXISTS scenario_pnl;
DROP TABLE IF EXISTS scenarios;
DROP TABLE IF EXISTS valuations;
DROP TABLE IF EXISTS positions;
DROP TABLE IF EXISTS instruments;
DROP TABLE IF EXISTS market_data;
DROP TABLE IF EXISTS underlyings;
DROP TABLE IF EXISTS settings;
PRAGMA foreign_keys = ON;

CREATE TABLE settings (
    key   TEXT PRIMARY KEY,
    value REAL NOT NULL
);

CREATE TABLE underlyings (
    symbol TEXT PRIMARY KEY,
    name   TEXT NOT NULL,
    sector TEXT NOT NULL
);

CREATE TABLE market_data (
    symbol    TEXT PRIMARY KEY REFERENCES underlyings(symbol),
    spot      REAL NOT NULL CHECK (spot > 0),
    vol       REAL NOT NULL CHECK (vol > 0),
    div_yield REAL NOT NULL DEFAULT 0
);

CREATE TABLE instruments (
    instrument_id  INTEGER PRIMARY KEY,
    symbol         TEXT    NOT NULL REFERENCES underlyings(symbol),
    option_type    TEXT    NOT NULL CHECK (option_type IN ('C', 'P')),
    style          TEXT    NOT NULL CHECK (style IN ('E', 'A')),
    strike         REAL    NOT NULL CHECK (strike > 0),
    maturity_years REAL    NOT NULL CHECK (maturity_years > 0),
    multiplier     INTEGER NOT NULL DEFAULT 100
);

CREATE TABLE positions (
    position_id   INTEGER PRIMARY KEY,
    book          TEXT    NOT NULL,
    instrument_id INTEGER NOT NULL REFERENCES instruments(instrument_id),
    quantity      INTEGER NOT NULL CHECK (quantity <> 0)  -- contracts; negative = short
);

-- Written by the C++ engine: per-unit price and Greeks for every position.
CREATE TABLE valuations (
    position_id INTEGER PRIMARY KEY REFERENCES positions(position_id),
    model       TEXT NOT NULL,
    unit_price  REAL NOT NULL,
    delta       REAL NOT NULL,
    gamma       REAL NOT NULL,
    vega        REAL NOT NULL,  -- per 1 vol point
    theta       REAL NOT NULL,  -- per calendar day
    rho         REAL NOT NULL   -- per 1% rate move
);

CREATE TABLE scenarios (
    scenario_id INTEGER PRIMARY KEY,
    spot_shock  REAL NOT NULL,  -- relative, e.g. -0.10 = spot down 10%
    vol_shock   REAL NOT NULL,  -- absolute, e.g. 0.05 = vol up 5 points
    UNIQUE (spot_shock, vol_shock)
);

-- Full revaluation P&L vs. the delta-gamma-vega Taylor approximation.
CREATE TABLE scenario_pnl (
    position_id    INTEGER NOT NULL REFERENCES positions(position_id),
    scenario_id    INTEGER NOT NULL REFERENCES scenarios(scenario_id),
    full_reval_pnl REAL NOT NULL,
    taylor_pnl     REAL NOT NULL,
    PRIMARY KEY (position_id, scenario_id)
);

-- Cross-checks between pricing methods for every instrument.
CREATE TABLE model_checks (
    instrument_id          INTEGER PRIMARY KEY REFERENCES instruments(instrument_id),
    closed_form_price      REAL,     -- Black-Scholes, European exercise
    binomial_price         REAL NOT NULL,
    mc_price               REAL,
    mc_std_error           REAL,
    early_exercise_premium REAL      -- American tree price minus European closed form
);

-- Position-level risk in money terms, ready for aggregation.
CREATE VIEW position_risk AS
SELECT
    p.position_id,
    p.book,
    i.symbol,
    u.sector,
    i.option_type,
    i.style,
    i.strike,
    i.maturity_years,
    p.quantity,
    p.quantity * i.multiplier                                      AS units,
    p.quantity * i.multiplier * v.unit_price                       AS market_value,
    p.quantity * i.multiplier * v.delta * m.spot                   AS dollar_delta,
    p.quantity * i.multiplier * v.gamma * m.spot * m.spot / 100.0  AS dollar_gamma_1pct,
    p.quantity * i.multiplier * v.vega                             AS vega_per_pt,
    p.quantity * i.multiplier * v.theta                            AS theta_per_day,
    p.quantity * i.multiplier * v.rho                              AS rho_per_pct
FROM positions   p
JOIN instruments i ON i.instrument_id = p.instrument_id
JOIN underlyings u ON u.symbol = i.symbol
JOIN market_data m ON m.symbol = i.symbol
JOIN valuations  v ON v.position_id = p.position_id;
