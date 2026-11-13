// ore_run: prices an option portfolio stored in SQLite, writes Greeks,
// stress-test P&L and model cross-checks back to the database, then runs the
// SQL risk reports in sql/risk_reports.sql.
//
// Usage: ore_run [database_path] [sql_directory]

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "db.hpp"
#include "pricing.hpp"

namespace {

using ore::Database;
using ore::ExerciseStyle;
using ore::Greeks;
using ore::Market;
using ore::OptionSpec;
using ore::OptionType;

struct Position {
    int position_id;
    int instrument_id;
    int units;  // contracts x multiplier
    OptionSpec spec;
    Market market;
};

std::vector<Position> load_positions(Database& db, double rate) {
    auto stmt = db.prepare(R"(
        SELECT p.position_id, i.instrument_id, p.quantity * i.multiplier,
               i.option_type, i.style, i.strike, i.maturity_years,
               m.spot, m.vol, m.div_yield
        FROM positions p
        JOIN instruments i ON i.instrument_id = p.instrument_id
        JOIN market_data m ON m.symbol = i.symbol
        ORDER BY p.position_id)");
    std::vector<Position> out;
    while (stmt.step()) {
        Position p;
        p.position_id = stmt.column_int(0);
        p.instrument_id = stmt.column_int(1);
        p.units = stmt.column_int(2);
        p.spec.type = stmt.column_text(3) == "C" ? OptionType::Call : OptionType::Put;
        p.spec.style = stmt.column_text(4) == "E" ? ExerciseStyle::European
                                                  : ExerciseStyle::American;
        p.spec.strike = stmt.column_double(5);
        p.spec.maturity = stmt.column_double(6);
        p.market = {stmt.column_double(7), rate, stmt.column_double(9), stmt.column_double(8)};
        out.push_back(p);
    }
    return out;
}

void write_valuations(Database& db, const std::vector<Position>& positions,
                      std::map<int, Greeks>& base) {
    auto ins = db.prepare(
        "INSERT INTO valuations (position_id, model, unit_price, delta, gamma, vega, theta, rho)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    for (const auto& p : positions) {
        const Greeks g = ore::price_option(p.spec, p.market);
        base[p.position_id] = g;
        const std::string model = p.spec.style == ExerciseStyle::European
                                      ? "black_scholes"
                                      : "crr_binomial_500";
        ins.bind(1, p.position_id).bind(2, model).bind(3, g.price).bind(4, g.delta);
        ins.bind(5, g.gamma).bind(6, g.vega).bind(7, g.theta).bind(8, g.rho);
        ins.step();
        ins.reset();
    }
}

int write_scenarios(Database& db, const std::vector<Position>& positions,
                    const std::map<int, Greeks>& base) {
    const std::vector<double> spot_shocks = {-0.20, -0.15, -0.10, -0.05, 0.0,
                                             0.05,  0.10,  0.15,  0.20};
    const std::vector<double> vol_shocks = {-0.05, 0.0, 0.05};

    auto ins_scn = db.prepare("INSERT INTO scenarios (spot_shock, vol_shock) VALUES (?, ?)");
    auto ins_pnl = db.prepare(
        "INSERT INTO scenario_pnl (position_id, scenario_id, full_reval_pnl, taylor_pnl)"
        " VALUES (?, ?, ?, ?)");

    int count = 0;
    for (double ds : spot_shocks) {
        for (double dv : vol_shocks) {
            ins_scn.bind(1, ds).bind(2, dv);
            ins_scn.step();
            ins_scn.reset();
            const int scenario_id = static_cast<int>(sqlite3_last_insert_rowid(db.handle()));
            ++count;

            for (const auto& p : positions) {
                const Greeks& g = base.at(p.position_id);
                Market shocked = p.market;
                shocked.spot *= 1.0 + ds;
                shocked.vol = std::max(0.01, p.market.vol + dv);
                const double new_price = ore::price(p.spec, shocked);

                const double d_spot = shocked.spot - p.market.spot;
                const double d_vol_pts = (shocked.vol - p.market.vol) * 100.0;
                const double taylor_unit =
                    g.delta * d_spot + 0.5 * g.gamma * d_spot * d_spot + g.vega * d_vol_pts;

                ins_pnl.bind(1, p.position_id).bind(2, scenario_id);
                ins_pnl.bind(3, p.units * (new_price - g.price));
                ins_pnl.bind(4, p.units * taylor_unit);
                ins_pnl.step();
                ins_pnl.reset();
            }
        }
    }
    return count;
}

void write_model_checks(Database& db, double rate) {
    auto sel = db.prepare(R"(
        SELECT i.instrument_id, i.option_type, i.style, i.strike, i.maturity_years,
               m.spot, m.vol, m.div_yield
        FROM instruments i JOIN market_data m ON m.symbol = i.symbol
        ORDER BY i.instrument_id)");
    auto ins = db.prepare(
        "INSERT INTO model_checks (instrument_id, closed_form_price, binomial_price, mc_price,"
        " mc_std_error, early_exercise_premium) VALUES (?, ?, ?, ?, ?, ?)");

    while (sel.step()) {
        const int id = sel.column_int(0);
        OptionSpec spec{sel.column_text(1) == "C" ? OptionType::Call : OptionType::Put,
                        sel.column_text(2) == "E" ? ExerciseStyle::European
                                                  : ExerciseStyle::American,
                        sel.column_double(3), sel.column_double(4)};
        const Market mkt{sel.column_double(5), rate, sel.column_double(7), sel.column_double(6)};

        const double tree = ore::binomial_price(spec, mkt, 1000);
        ins.bind(1, id).bind(3, tree);
        if (spec.style == ExerciseStyle::European) {
            const auto mc = ore::monte_carlo_european(spec, mkt, 200000, 42 + id, true);
            ins.bind(2, ore::black_scholes(spec, mkt).price);
            ins.bind(4, mc.price).bind(5, mc.std_error);
        } else {
            OptionSpec european = spec;
            european.style = ExerciseStyle::European;
            ins.bind(6, tree - ore::black_scholes(european, mkt).price);
        }
        ins.step();
        ins.reset();
    }
}

std::string format_cell(ore::Statement& stmt, int col) {
    if (stmt.column_is_null(col)) return "-";
    if (stmt.column_is_text(col)) return stmt.column_text(col);
    if (stmt.column_is_integer(col)) return std::to_string(stmt.column_int(col));
    const double v = stmt.column_double(col);
    if (v == std::floor(v) && std::fabs(v) < 1e15) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.0f", v);
        return buf;
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.4f", v);
    std::string s = buf;
    s.erase(s.find_last_not_of('0') + 1);
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

// Runs every statement in a SQL script and prints each result set as a table.
// The "-- report:" comment above a statement becomes its title.
void run_reports(Database& db, const std::string& path) {
    const std::string script = ore::read_file(path);
    const char* cursor = script.c_str();
    while (*cursor) {
        const char* tail = nullptr;
        sqlite3_stmt* raw = nullptr;
        if (sqlite3_prepare_v2(db.handle(), cursor, -1, &raw, &tail) != SQLITE_OK) {
            throw std::runtime_error(std::string("report prepare: ") +
                                     sqlite3_errmsg(db.handle()));
        }
        const std::string chunk(cursor, tail);
        cursor = tail;
        if (!raw) continue;  // only whitespace or comments left
        ore::Statement stmt(db.handle(), raw);

        std::string title;
        std::istringstream lines(chunk);
        for (std::string line; std::getline(lines, line);) {
            const auto pos = line.find("-- report:");
            if (pos != std::string::npos) title = line.substr(pos + 10);
        }

        std::vector<std::vector<std::string>> rows;
        std::vector<std::string> header;
        for (int c = 0; c < stmt.column_count(); ++c) header.push_back(stmt.column_name(c));
        while (stmt.step()) {
            std::vector<std::string> row;
            for (int c = 0; c < stmt.column_count(); ++c) row.push_back(format_cell(stmt, c));
            rows.push_back(row);
        }

        std::vector<size_t> width(header.size());
        for (size_t c = 0; c < header.size(); ++c) {
            width[c] = header[c].size();
            for (const auto& r : rows) width[c] = std::max(width[c], r[c].size());
        }
        auto print_row = [&](const std::vector<std::string>& r) {
            for (size_t c = 0; c < r.size(); ++c) {
                std::cout << (c ? "  " : "");
                std::cout << std::string(width[c] - r[c].size(), ' ') << r[c];
            }
            std::cout << '\n';
        };
        std::cout << "\n==" << title << '\n';
        print_row(header);
        size_t total = 0;
        for (size_t w : width) total += w + 2;
        std::cout << std::string(total - 2, '-') << '\n';
        for (const auto& r : rows) print_row(r);
    }
}

}  // namespace

int main(int argc, char** argv) {
    const std::string db_path = argc > 1 ? argv[1] : "risk.db";
    const std::string sql_dir = argc > 2 ? argv[2] : "sql";

    try {
        const auto start = std::chrono::steady_clock::now();
        Database db(db_path);
        db.exec_file(sql_dir + "/schema.sql");
        db.exec_file(sql_dir + "/seed.sql");

        auto rate_stmt = db.prepare("SELECT value FROM settings WHERE key = 'risk_free_rate'");
        if (!rate_stmt.step()) throw std::runtime_error("risk_free_rate missing from settings");
        const double rate = rate_stmt.column_double(0);

        const auto positions = load_positions(db, rate);
        std::map<int, Greeks> base;
        int scenarios = 0;
        {
            ore::Transaction tx(db);
            write_valuations(db, positions, base);
            scenarios = write_scenarios(db, positions, base);
            write_model_checks(db, rate);
            tx.commit();
        }
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - start)
                            .count();

        std::cout << "Priced " << positions.size() << " positions under " << scenarios
                  << " scenarios (" << positions.size() * scenarios << " full revaluations) in "
                  << ms << " ms. Results written to " << db_path << ".\n";

        run_reports(db, sql_dir + "/risk_reports.sql");
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
