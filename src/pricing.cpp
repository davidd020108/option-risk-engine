#include "pricing.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <stdexcept>
#include <vector>

namespace ore {

namespace {

constexpr double kDaysPerYear = 365.0;
constexpr double kPi = 3.14159265358979323846;

double payoff(OptionType type, double spot, double strike) {
    return type == OptionType::Call ? std::max(spot - strike, 0.0)
                                    : std::max(strike - spot, 0.0);
}

void validate(const OptionSpec& opt, const Market& mkt) {
    if (opt.strike <= 0.0 || opt.maturity <= 0.0 || mkt.spot <= 0.0 || mkt.vol <= 0.0) {
        throw std::invalid_argument("strike, maturity, spot and vol must be positive");
    }
}

// Builds the tree once and returns the option values at the first three time
// layers, which is all we need for price, delta, gamma and theta.
struct TreeSnapshot {
    double v0;
    double v1_up, v1_down;
    double v2_up, v2_mid, v2_down;
    double u, d, dt;
};

TreeSnapshot run_tree(const OptionSpec& opt, const Market& mkt, int steps) {
    validate(opt, mkt);
    if (steps < 2) throw std::invalid_argument("binomial tree needs at least 2 steps");

    const double dt = opt.maturity / steps;
    const double u = std::exp(mkt.vol * std::sqrt(dt));
    const double d = 1.0 / u;
    const double growth = std::exp((mkt.rate - mkt.div_yield) * dt);
    const double p = (growth - d) / (u - d);
    if (p <= 0.0 || p >= 1.0) throw std::runtime_error("tree is not arbitrage-free; add steps");
    const double disc = std::exp(-mkt.rate * dt);
    const bool american = opt.style == ExerciseStyle::American;

    // values[j] holds the option value at node with j up-moves.
    std::vector<double> values(steps + 1);
    for (int j = 0; j <= steps; ++j) {
        const double s = mkt.spot * std::pow(u, j) * std::pow(d, steps - j);
        values[j] = payoff(opt.type, s, opt.strike);
    }

    TreeSnapshot snap{};
    snap.u = u;
    snap.d = d;
    snap.dt = dt;
    for (int i = steps - 1; i >= 0; --i) {
        for (int j = 0; j <= i; ++j) {
            double cont = disc * (p * values[j + 1] + (1.0 - p) * values[j]);
            if (american) {
                const double s = mkt.spot * std::pow(u, j) * std::pow(d, i - j);
                cont = std::max(cont, payoff(opt.type, s, opt.strike));
            }
            values[j] = cont;
        }
        if (i == 2) {
            snap.v2_down = values[0];
            snap.v2_mid = values[1];
            snap.v2_up = values[2];
        } else if (i == 1) {
            snap.v1_down = values[0];
            snap.v1_up = values[1];
        }
    }
    snap.v0 = values[0];
    return snap;
}

}  // namespace

double norm_pdf(double x) { return std::exp(-0.5 * x * x) / std::sqrt(2.0 * kPi); }

double norm_cdf(double x) { return 0.5 * std::erfc(-x / std::sqrt(2.0)); }

Greeks black_scholes(const OptionSpec& opt, const Market& mkt) {
    validate(opt, mkt);
    const double s = mkt.spot, k = opt.strike, t = opt.maturity;
    const double r = mkt.rate, q = mkt.div_yield, sigma = mkt.vol;
    const double sqrt_t = std::sqrt(t);
    const double d1 = (std::log(s / k) + (r - q + 0.5 * sigma * sigma) * t) / (sigma * sqrt_t);
    const double d2 = d1 - sigma * sqrt_t;
    const double df_r = std::exp(-r * t);
    const double df_q = std::exp(-q * t);
    const double pdf_d1 = norm_pdf(d1);

    Greeks g;
    g.gamma = df_q * pdf_d1 / (s * sigma * sqrt_t);
    g.vega = s * df_q * pdf_d1 * sqrt_t / 100.0;
    const double theta_common = -s * df_q * pdf_d1 * sigma / (2.0 * sqrt_t);

    if (opt.type == OptionType::Call) {
        g.price = s * df_q * norm_cdf(d1) - k * df_r * norm_cdf(d2);
        g.delta = df_q * norm_cdf(d1);
        g.theta = (theta_common - r * k * df_r * norm_cdf(d2) + q * s * df_q * norm_cdf(d1)) /
                  kDaysPerYear;
        g.rho = k * t * df_r * norm_cdf(d2) / 100.0;
    } else {
        g.price = k * df_r * norm_cdf(-d2) - s * df_q * norm_cdf(-d1);
        g.delta = df_q * (norm_cdf(d1) - 1.0);
        g.theta = (theta_common + r * k * df_r * norm_cdf(-d2) - q * s * df_q * norm_cdf(-d1)) /
                  kDaysPerYear;
        g.rho = -k * t * df_r * norm_cdf(-d2) / 100.0;
    }
    return g;
}

double binomial_price(const OptionSpec& opt, const Market& mkt, int steps) {
    return run_tree(opt, mkt, steps).v0;
}

Greeks binomial_greeks(const OptionSpec& opt, const Market& mkt, int steps) {
    const TreeSnapshot t = run_tree(opt, mkt, steps);
    const double s = mkt.spot;
    const double s_up = s * t.u, s_down = s * t.d;
    const double s_uu = s * t.u * t.u, s_dd = s * t.d * t.d;  // middle node at step 2 is s

    Greeks g;
    g.price = t.v0;
    g.delta = (t.v1_up - t.v1_down) / (s_up - s_down);
    const double delta_up = (t.v2_up - t.v2_mid) / (s_uu - s);
    const double delta_down = (t.v2_mid - t.v2_down) / (s - s_dd);
    g.gamma = (delta_up - delta_down) / (0.5 * (s_uu - s_dd));
    g.theta = (t.v2_mid - t.v0) / (2.0 * t.dt) / kDaysPerYear;

    const double vol_bump = 0.01, rate_bump = 0.0001;
    Market m = mkt;
    m.vol = mkt.vol + vol_bump;
    const double v_up = binomial_price(opt, m, steps);
    m.vol = mkt.vol - vol_bump;
    const double v_down = binomial_price(opt, m, steps);
    g.vega = (v_up - v_down) / (2.0 * vol_bump) / 100.0;

    m = mkt;
    m.rate = mkt.rate + rate_bump;
    const double r_up = binomial_price(opt, m, steps);
    m.rate = mkt.rate - rate_bump;
    const double r_down = binomial_price(opt, m, steps);
    g.rho = (r_up - r_down) / (2.0 * rate_bump) / 100.0;
    return g;
}

McResult monte_carlo_european(const OptionSpec& opt, const Market& mkt, int paths,
                              std::uint64_t seed, bool antithetic) {
    validate(opt, mkt);
    if (opt.style != ExerciseStyle::European) {
        throw std::invalid_argument("monte_carlo_european only prices European options");
    }
    if (paths < 2) throw std::invalid_argument("need at least 2 paths");

    std::mt19937_64 rng(seed);
    std::normal_distribution<double> normal(0.0, 1.0);
    const double t = opt.maturity;
    const double drift = (mkt.rate - mkt.div_yield - 0.5 * mkt.vol * mkt.vol) * t;
    const double diffusion = mkt.vol * std::sqrt(t);
    const double disc = std::exp(-mkt.rate * t);

    // With antithetic variates each sample is the average of a path and its
    // mirror image, so the samples stay independent and the usual standard
    // error formula still applies.
    const int samples = antithetic ? paths / 2 : paths;
    double sum = 0.0, sum_sq = 0.0;
    for (int i = 0; i < samples; ++i) {
        const double z = normal(rng);
        double x = payoff(opt.type, mkt.spot * std::exp(drift + diffusion * z), opt.strike);
        if (antithetic) {
            x = 0.5 * (x + payoff(opt.type, mkt.spot * std::exp(drift - diffusion * z), opt.strike));
        }
        x *= disc;
        sum += x;
        sum_sq += x * x;
    }
    const double mean = sum / samples;
    const double var = (sum_sq - samples * mean * mean) / (samples - 1);
    return {mean, std::sqrt(std::max(var, 0.0) / samples)};
}

Greeks price_option(const OptionSpec& opt, const Market& mkt) {
    return opt.style == ExerciseStyle::European ? black_scholes(opt, mkt)
                                                : binomial_greeks(opt, mkt, 500);
}

double price(const OptionSpec& opt, const Market& mkt) {
    return opt.style == ExerciseStyle::European ? black_scholes(opt, mkt).price
                                                : binomial_price(opt, mkt, 500);
}

double implied_vol(double target_price, const OptionSpec& opt, Market mkt, double lo, double hi,
                   double tol) {
    auto price_at = [&](double vol) {
        mkt.vol = vol;
        return price(opt, mkt);
    };
    if (target_price < price_at(lo) || target_price > price_at(hi)) {
        throw std::domain_error("target price outside the bracketed volatility range");
    }
    for (int i = 0; i < 200 && hi - lo > tol; ++i) {
        const double mid = 0.5 * (lo + hi);
        (price_at(mid) < target_price ? lo : hi) = mid;
    }
    return 0.5 * (lo + hi);
}

}  // namespace ore
