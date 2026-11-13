#pragma once

#include <cstdint>

namespace ore {

enum class OptionType { Call, Put };
enum class ExerciseStyle { European, American };

struct OptionSpec {
    OptionType type;
    ExerciseStyle style;
    double strike;
    double maturity;  // in years
};

struct Market {
    double spot;
    double rate;       // continuously compounded risk-free rate
    double div_yield;  // continuous dividend yield
    double vol;        // annualised volatility, e.g. 0.25
};

// Greeks are reported per unit of the underlying option, in desk conventions:
//   vega  - price change for a 1 vol-point move (0.01)
//   theta - price change for one calendar day passing
//   rho   - price change for a 1% (0.01) move in rates
struct Greeks {
    double price = 0.0;
    double delta = 0.0;
    double gamma = 0.0;
    double vega = 0.0;
    double theta = 0.0;
    double rho = 0.0;
};

struct McResult {
    double price;
    double std_error;
};

double norm_pdf(double x);
double norm_cdf(double x);

// Closed-form Black-Scholes-Merton price and Greeks (European exercise only).
Greeks black_scholes(const OptionSpec& opt, const Market& mkt);

// Cox-Ross-Rubinstein binomial tree; handles European and American exercise.
double binomial_price(const OptionSpec& opt, const Market& mkt, int steps);

// Delta, gamma and theta read off the tree; vega and rho by central bumping.
Greeks binomial_greeks(const OptionSpec& opt, const Market& mkt, int steps);

// Monte Carlo under GBM for European options, with optional antithetic variates.
McResult monte_carlo_european(const OptionSpec& opt, const Market& mkt, int paths,
                              std::uint64_t seed, bool antithetic);

// Production pricer: closed form for European, 500-step tree for American.
Greeks price_option(const OptionSpec& opt, const Market& mkt);

// Price only, same models as price_option. Used for scenario revaluation,
// where Greeks are not needed and bumping the tree would cost 5x the time.
double price(const OptionSpec& opt, const Market& mkt);

// Implied volatility by bisection on price_option.
double implied_vol(double target_price, const OptionSpec& opt, Market mkt,
                   double lo = 1e-4, double hi = 5.0, double tol = 1e-10);

}  // namespace ore
