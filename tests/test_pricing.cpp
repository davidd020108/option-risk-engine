// Unit tests for the pricing library. Each test checks a property that must
// hold for any correct implementation: known reference values, no-arbitrage
// identities, convergence between methods, and Greeks against finite differences.

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "pricing.hpp"

using namespace ore;

namespace {

int failures = 0;

void expect_near(const std::string& name, double actual, double expected, double tol) {
    const bool ok = std::fabs(actual - expected) <= tol;
    std::printf("[%s] %-58s got %.6f, expected %.6f (tol %.1e)\n", ok ? "PASS" : "FAIL",
                name.c_str(), actual, expected, tol);
    if (!ok) ++failures;
}

void expect_true(const std::string& name, bool condition) {
    std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", name.c_str());
    if (!condition) ++failures;
}

const Market kMkt{100.0, 0.05, 0.0, 0.20};
const OptionSpec kCall{OptionType::Call, ExerciseStyle::European, 100.0, 1.0};
const OptionSpec kPut{OptionType::Put, ExerciseStyle::European, 100.0, 1.0};

void test_reference_values() {
    // Hull, Options, Futures and Other Derivatives: S=42, K=40, r=10%, sigma=20%, T=0.5.
    const Market m{42.0, 0.10, 0.0, 0.20};
    expect_near("Black-Scholes call, Hull reference",
                black_scholes({OptionType::Call, ExerciseStyle::European, 40.0, 0.5}, m).price,
                4.7594, 1e-4);
    expect_near("Black-Scholes put, Hull reference",
                black_scholes({OptionType::Put, ExerciseStyle::European, 40.0, 0.5}, m).price,
                0.8086, 1e-4);
}

void test_put_call_parity() {
    const Market m{100.0, 0.05, 0.03, 0.25};
    const double c = black_scholes(kCall, m).price;
    const double p = black_scholes(kPut, m).price;
    const double rhs = m.spot * std::exp(-m.div_yield) - 100.0 * std::exp(-m.rate);
    expect_near("put-call parity with dividends", c - p, rhs, 1e-10);
}

void test_binomial_convergence() {
    const double bs = black_scholes(kCall, kMkt).price;
    expect_near("CRR tree (2000 steps) converges to Black-Scholes call",
                binomial_price(kCall, kMkt, 2000), bs, 2e-3);
    const double err_100 = std::fabs(binomial_price(kPut, kMkt, 100) - black_scholes(kPut, kMkt).price);
    const double err_1000 = std::fabs(binomial_price(kPut, kMkt, 1000) - black_scholes(kPut, kMkt).price);
    expect_true("tree error shrinks as steps increase (100 -> 1000)", err_1000 < err_100);
}

void test_american_bounds() {
    OptionSpec am_put = kPut;
    am_put.style = ExerciseStyle::American;
    OptionSpec am_call = kCall;
    am_call.style = ExerciseStyle::American;
    const double eu_put = binomial_price(kPut, kMkt, 1000);
    const double us_put = binomial_price(am_put, kMkt, 1000);
    expect_true("American put worth more than European put (r > 0)", us_put > eu_put + 1e-3);
    const Market deep_itm{70.0, kMkt.rate, kMkt.div_yield, kMkt.vol};
    expect_true("deep in-the-money American put worth at least intrinsic value (30)",
                binomial_price(am_put, deep_itm, 1000) >= 30.0 - 1e-12);
    expect_near("American call on non-dividend stock equals European call",
                binomial_price(am_call, kMkt, 1000), binomial_price(kCall, kMkt, 1000), 1e-9);
}

void test_monte_carlo() {
    const double bs = black_scholes(kCall, kMkt).price;
    const McResult plain = monte_carlo_european(kCall, kMkt, 200000, 7, false);
    const McResult anti = monte_carlo_european(kCall, kMkt, 200000, 7, true);
    expect_near("Monte Carlo call within 3 standard errors of closed form", plain.price, bs,
                3 * plain.std_error);
    expect_true("antithetic variates reduce the standard error",
                anti.std_error < plain.std_error);
}

void test_greeks_vs_finite_differences() {
    const Market m{100.0, 0.05, 0.02, 0.30};
    const OptionSpec opt{OptionType::Put, ExerciseStyle::European, 95.0, 0.75};
    const Greeks g = black_scholes(opt, m);
    auto price = [&](Market x) { return black_scholes(opt, x).price; };

    const double h = 0.01;
    Market up = m, dn = m;
    up.spot += h;
    dn.spot -= h;
    expect_near("delta vs central difference", g.delta, (price(up) - price(dn)) / (2 * h), 1e-6);
    expect_near("gamma vs second difference", g.gamma,
                (price(up) - 2 * price(m) + price(dn)) / (h * h), 1e-4);

    up = m, dn = m;
    up.vol += 1e-4;
    dn.vol -= 1e-4;
    expect_near("vega (per vol point) vs bump", g.vega, (price(up) - price(dn)) / 2e-4 / 100, 1e-6);

    OptionSpec later = opt;
    later.maturity -= 1.0 / 365.0;
    expect_near("theta (per day) vs one-day roll", g.theta,
                black_scholes(later, m).price - g.price, 2e-3);

    // Tree Greeks for an American option should be close to the closed form
    // when early exercise has no value (call, no dividends).
    const Market nodiv{100.0, 0.05, 0.0, 0.25};
    const OptionSpec am_call{OptionType::Call, ExerciseStyle::American, 100.0, 1.0};
    const OptionSpec eu_call{OptionType::Call, ExerciseStyle::European, 100.0, 1.0};
    const Greeks tree = binomial_greeks(am_call, nodiv, 1000);
    const Greeks cf = black_scholes(eu_call, nodiv);
    expect_near("tree delta matches closed form", tree.delta, cf.delta, 2e-3);
    expect_near("tree gamma matches closed form", tree.gamma, cf.gamma, 2e-4);
    expect_near("tree vega matches closed form", tree.vega, cf.vega, 2e-3);
}

void test_implied_vol() {
    const Market m{100.0, 0.03, 0.0, 0.27};
    const double target = black_scholes(kCall, m).price;
    expect_near("implied vol recovers the input volatility", implied_vol(target, kCall, m), 0.27,
                1e-8);
}

}  // namespace

int main() {
    test_reference_values();
    test_put_call_parity();
    test_binomial_convergence();
    test_american_bounds();
    test_monte_carlo();
    test_greeks_vs_finite_differences();
    test_implied_vol();
    std::printf("\n%s: %d failure(s)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
                failures);
    return failures == 0 ? 0 : 1;
}
