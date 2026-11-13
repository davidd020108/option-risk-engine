-- Sample data. Companies, prices and positions are fictional and only
-- illustrate how the engine works.

INSERT INTO settings (key, value) VALUES ('risk_free_rate', 0.04);

INSERT INTO underlyings (symbol, name, sector) VALUES
    ('TECH', 'Sample Tech Co.',   'Technology'),
    ('BANK', 'Sample Bank Co.',   'Financials'),
    ('ENGY', 'Sample Energy Co.', 'Energy');

INSERT INTO market_data (symbol, spot, vol, div_yield) VALUES
    ('TECH', 100.0, 0.35, 0.000),
    ('BANK',  50.0, 0.22, 0.045),
    ('ENGY',  80.0, 0.30, 0.030);

INSERT INTO instruments (instrument_id, symbol, option_type, style, strike, maturity_years) VALUES
    ( 1, 'TECH', 'C', 'E', 100.0, 0.25),
    ( 2, 'TECH', 'P', 'E',  90.0, 0.25),
    ( 3, 'TECH', 'C', 'E', 120.0, 1.00),
    ( 4, 'TECH', 'P', 'A', 100.0, 0.50),
    ( 5, 'BANK', 'C', 'E',  50.0, 0.50),
    ( 6, 'BANK', 'P', 'E',  45.0, 0.50),
    ( 7, 'BANK', 'C', 'A',  48.0, 1.00),
    ( 8, 'BANK', 'P', 'A',  55.0, 1.00),
    ( 9, 'ENGY', 'C', 'E',  80.0, 0.25),
    (10, 'ENGY', 'P', 'E',  70.0, 0.75),
    (11, 'ENGY', 'P', 'A',  85.0, 0.50),
    (12, 'ENGY', 'C', 'E',  95.0, 1.00);

-- Two books: a long-volatility book and an option-selling (short-vol) book.
INSERT INTO positions (position_id, book, instrument_id, quantity) VALUES
    ( 1, 'LONG_VOL',  1,  20),
    ( 2, 'LONG_VOL',  2,  20),
    ( 3, 'LONG_VOL',  3,  10),
    ( 4, 'LONG_VOL',  5,  30),
    ( 5, 'LONG_VOL',  6,  30),
    ( 6, 'LONG_VOL',  9,  15),
    ( 7, 'LONG_VOL', 10,  15),
    ( 8, 'SHORT_VOL', 2, -40),
    ( 9, 'SHORT_VOL', 4, -25),
    (10, 'SHORT_VOL', 6, -50),
    (11, 'SHORT_VOL', 7, -20),
    (12, 'SHORT_VOL', 8, -15),
    (13, 'SHORT_VOL', 11, -30),
    (14, 'SHORT_VOL', 12, -25);
