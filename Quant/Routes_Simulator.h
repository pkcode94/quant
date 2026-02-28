#pragma once

#include "AppContext.h"
#include "HtmlHelpers.h"
#include "Simulator.h"
#include <mutex>
#include <sstream>

inline void registerSimulatorRoutes(httplib::Server& svr, AppContext& ctx)
{
    auto& db = ctx.defaultDb;
    auto& dbMutex = ctx.dbMutex;

    // ========== GET /simulator — configuration form ==========
    svr.Get("/simulator", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        std::ostringstream h;
        h << std::fixed << std::setprecision(17);
        h << html::msgBanner(req) << html::errBanner(req);
        h << "<h1>&#9881; Simulator</h1>"
             "<p style='color:#64748b;font-size:0.85em;'>"
             "Forward simulation: enter a price series and parameters. "
             "The engine buys at entry levels when prices drop, sells at exit "
             "levels when prices rise, and tracks capital/fees/P&amp;L.<br>"
             "Backtest: use historical prices to verify your fee hedging covers costs.</p>";

        double wal = db.loadWalletBalance();

        h << "<form class='card' method='POST' action='/simulator/run'>"
             "<h3>Configuration</h3>"
             "<label>Symbol</label>"
             "<input type='text' name='symbol' value='BTC' required><br>"
             "<label>Starting Capital</label>"
             "<input type='number' name='capital' step='any' value='" << wal << "' required><br>"
             "<h3>Price Series (one per line: timestamp,price)</h3>"
             "<div style='margin-bottom:6px;'>"
             "<a class='btn btn-sm' href='/simulator/load-trades'>Load from trade history</a>"
             "<span style='color:#64748b;font-size:0.78em;margin-left:8px;'>Seed prices from existing trades for backtesting</span>"
             "</div>"
             "<textarea name='priceSeries' rows='10' cols='50' "
             "placeholder='1700000000,50000&#10;1700003600,49500&#10;1700007200,51000&#10;...' "
             "style='width:100%;font-family:monospace;font-size:0.85em;background:#0b1426;color:#cbd5e1;"
             "border:1px solid #1a2744;border-radius:4px;padding:8px;'></textarea><br>"
             "<h3>Entry Parameters</h3>"
             "<label>Entry Risk (0=conservative, 1=aggressive)</label>"
             "<input type='number' name='entryRisk' step='any' value='0.5'><br>"
             "<label>Entry Steepness</label>"
             "<input type='number' name='entrySteepness' step='any' value='6.0'><br>"
             "<label>Entry Levels</label>"
             "<input type='number' name='entryLevels' value='5'><br>"
             "<label>Range Below (price units)</label>"
             "<input type='number' name='rangeBelow' step='any' value='0'><br>"
             "<label>Range Above (price units)</label>"
             "<input type='number' name='rangeAbove' step='any' value='0'><br>"
             "<h3>Exit Parameters</h3>"
             "<label>Exit Risk (0=sell early, 1=sell late)</label>"
             "<input type='number' name='exitRisk' step='any' value='0.5'><br>"
             "<label>Exit Fraction (0-1)</label>"
             "<input type='number' name='exitFraction' step='any' value='1.0'><br>"
             "<label>Exit Steepness</label>"
             "<input type='number' name='exitSteepness' step='any' value='4.0'><br>"
             "<h3>Fee &amp; Overhead Parameters</h3>"
             "<label>Buy Fee Rate (e.g. 0.001 = 0.1%)</label>"
             "<input type='number' name='buyFeeRate' step='any' value='0.001'><br>"
             "<label>Sell Fee Rate</label>"
             "<input type='number' name='sellFeeRate' step='any' value='0.001'><br>"
             "<label>Fee Spread</label>"
             "<input type='number' name='feeSpread' step='any' value='0.001'><br>"
             "<label>Fee Hedging Coefficient</label>"
             "<input type='number' name='feeHedging' step='any' value='1'><br>"
             "<label>Surplus Rate</label>"
             "<input type='number' name='surplusRate' step='any' value='0.02'><br>"
             "<label>Symbol Count</label>"
             "<input type='number' name='symbolCount' value='1'><br>"
             "<label>Delta Time</label>"
             "<input type='number' name='deltaTime' step='any' value='1'><br>"
             "<label>Coefficient K</label>"
             "<input type='number' name='coefficientK' step='any' value='0'><br>"
             "<label>Max Risk (0=disabled)</label>"
             "<input type='number' name='maxRisk' step='any' value='0'><br>"
             "<label>Min Risk</label>"
             "<input type='number' name='minRisk' step='any' value='0'><br>"
             "<label>DT Count (downturn cycles to buffer)</label>"
             "<input type='number' name='downtrendCount' value='1'><br>"
             "<label>SL Fraction (0-1, fraction sold at SL)</label>"
             "<input type='number' name='stopLossFraction' step='any' value='1'><br>"
             "<label>SL Hedge Count (future SL hits to pre-fund)</label>"
             "<input type='number' name='stopLossHedgeCount' value='0'><br>"
             "<label>Future Trade Fees (chain trades to pre-hedge)</label>"
             "<input type='number' name='futureTradeCount' value='0'><br>"
             "<h3>Chain Mode</h3>"
             "<label>Chain Cycles</label><select name='chainCycles'><option value='0'>Off</option><option value='1'>On</option></select><br>"
             "<label>Savings Rate (0-1, fraction of profit saved)</label>"
             "<input type='number' name='savingsRate' step='any' value='0'><br>"
             "<br><button>Run Simulation</button></form>";

        res.set_content(html::wrap("Simulator", h.str()), "text/html");
    });

    // ========== GET /simulator/load-trades — pre-fill price series from trade history ==========
    svr.Get("/simulator/load-trades", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        PriceSeries ps;
        db.seedPriceSeries(ps);

        auto trades = db.loadTrades();
        double wal = db.loadWalletBalance();

        // Build a pre-filled form with the price series textarea populated
        std::ostringstream h;
        h << std::fixed << std::setprecision(17);
        h << "<h1>&#9881; Simulator — Backtest from Trades</h1>"
             "<p style='color:#64748b;font-size:0.85em;'>"
             "Price series loaded from trade timestamps. "
             "You can add more data points or edit existing ones before running.</p>";

        // Determine the primary symbol (most trades)
        std::map<std::string, int> symCount;
        for (const auto& t : trades)
            if (t.timestamp > 0) symCount[t.symbol]++;
        std::string bestSym = "BTC";
        int bestCount = 0;
        for (const auto& sc : symCount)
            if (sc.second > bestCount) { bestSym = sc.first; bestCount = sc.second; }

        // Build the price series text
        std::ostringstream priceText;
        priceText << std::fixed << std::setprecision(8);
        for (const auto& sym : ps.symbols())
        {
            const auto& pts = ps.data().at(sym);
            for (const auto& pt : pts)
                priceText << pt.timestamp << "," << pt.price << "\n";
        }

        h << "<form class='card' method='POST' action='/simulator/run'>"
             "<h3>Configuration</h3>"
             "<label>Symbol</label>"
             "<input type='text' name='symbol' value='" << html::esc(bestSym) << "' required><br>"
             "<label>Starting Capital</label>"
             "<input type='number' name='capital' step='any' value='" << wal << "' required><br>"
             "<h3>Price Series (" << bestCount << " points from trades)</h3>"
             "<textarea name='priceSeries' rows='15' cols='50' "
             "style='width:100%;font-family:monospace;font-size:0.85em;background:#0b1426;color:#cbd5e1;"
             "border:1px solid #1a2744;border-radius:4px;padding:8px;'>"
          << html::esc(priceText.str())
          << "</textarea><br>"
             "<h3>Entry Parameters</h3>"
             "<label>Entry Risk (0=conservative, 1=aggressive)</label>"
             "<input type='number' name='entryRisk' step='any' value='0.5'><br>"
             "<label>Entry Steepness</label>"
             "<input type='number' name='entrySteepness' step='any' value='6.0'><br>"
             "<label>Entry Levels</label>"
             "<input type='number' name='entryLevels' value='5'><br>"
             "<label>Range Below (price units)</label>"
             "<input type='number' name='rangeBelow' step='any' value='0'><br>"
             "<label>Range Above (price units)</label>"
             "<input type='number' name='rangeAbove' step='any' value='0'><br>"
             "<h3>Exit Parameters</h3>"
             "<label>Exit Risk (0=sell early, 1=sell late)</label>"
             "<input type='number' name='exitRisk' step='any' value='0.5'><br>"
             "<label>Exit Fraction (0-1)</label>"
             "<input type='number' name='exitFraction' step='any' value='1.0'><br>"
             "<label>Exit Steepness</label>"
             "<input type='number' name='exitSteepness' step='any' value='4.0'><br>"
             "<h3>Fee &amp; Overhead Parameters</h3>"
             "<label>Buy Fee Rate (e.g. 0.001 = 0.1%)</label>"
             "<input type='number' name='buyFeeRate' step='any' value='0.001'><br>"
             "<label>Sell Fee Rate</label>"
             "<input type='number' name='sellFeeRate' step='any' value='0.001'><br>"
             "<label>Fee Spread</label>"
             "<input type='number' name='feeSpread' step='any' value='0.001'><br>"
             "<label>Fee Hedging Coefficient</label>"
             "<input type='number' name='feeHedging' step='any' value='1'><br>"
             "<label>Surplus Rate</label>"
             "<input type='number' name='surplusRate' step='any' value='0.02'><br>"
             "<label>Symbol Count</label>"
             "<input type='number' name='symbolCount' value='1'><br>"
             "<label>Delta Time</label>"
             "<input type='number' name='deltaTime' step='any' value='1'><br>"
             "<label>Coefficient K</label>"
             "<input type='number' name='coefficientK' step='any' value='0'><br>"
             "<label>Max Risk (0=disabled)</label>"
             "<input type='number' name='maxRisk' step='any' value='0'><br>"
             "<label>Min Risk</label>"
             "<input type='number' name='minRisk' step='any' value='0'><br>"
             "<label>DT Count (downturn cycles to buffer)</label>"
             "<input type='number' name='downtrendCount' value='1'><br>"
             "<label>SL Fraction (0-1, fraction sold at SL)</label>"
             "<input type='number' name='stopLossFraction' step='any' value='1'><br>"
             "<label>SL Hedge Count (future SL hits to pre-fund)</label>"
             "<input type='number' name='stopLossHedgeCount' value='0'><br>"
             "<label>Future Trade Fees (chain trades to pre-hedge)</label>"
             "<input type='number' name='futureTradeCount' value='0'><br>"
             "<h3>Chain Mode</h3>"
             "<label>Chain Cycles</label><select name='chainCycles'><option value='0'>Off</option><option value='1'>On</option></select><br>"
             "<label>Savings Rate (0-1, fraction of profit saved)</label>"
             "<input type='number' name='savingsRate' step='any' value='0'><br>"
             "<br><button>Run Backtest</button></form>";

        h << "<br><a class='btn' href='/simulator'>Back to Simulator</a>";
        res.set_content(html::wrap("Backtest", h.str()), "text/html");
    });

    // ========== POST /simulator/run — execute simulation ==========
    svr.Post("/simulator/run", [&](const httplib::Request& req, httplib::Response& res) {
        auto f = parseForm(req.body);
        std::string symbol = normalizeSymbol(fv(f, "symbol"));

        SimConfig cfg;
        cfg.symbol           = symbol;
        cfg.startingCapital  = fd(f, "capital");
        cfg.entryRisk        = fd(f, "entryRisk", 0.5);
        cfg.entrySteepness   = fd(f, "entrySteepness", 6.0);
        cfg.entryRangeBelow  = fd(f, "rangeBelow");
        cfg.entryRangeAbove  = fd(f, "rangeAbove");
        cfg.exitRisk         = fd(f, "exitRisk", 0.5);
        cfg.exitFraction     = fd(f, "exitFraction", 1.0);
        cfg.exitSteepness    = fd(f, "exitSteepness", 4.0);
        cfg.buyFeeRate       = fd(f, "buyFeeRate", 0.001);
        cfg.sellFeeRate      = fd(f, "sellFeeRate", 0.001);

        cfg.horizonParams.feeSpread              = fd(f, "feeSpread", 0.001);
        cfg.horizonParams.feeHedgingCoefficient  = fd(f, "feeHedging", 1.0);
        cfg.horizonParams.surplusRate             = fd(f, "surplusRate", 0.02);
        cfg.horizonParams.symbolCount             = fi(f, "symbolCount", 1);
        cfg.horizonParams.deltaTime               = fd(f, "deltaTime", 1.0);
        cfg.horizonParams.coefficientK            = fd(f, "coefficientK");
        cfg.horizonParams.maxRisk                 = fd(f, "maxRisk");
        cfg.horizonParams.minRisk                 = fd(f, "minRisk");
        cfg.horizonParams.horizonCount            = fi(f, "entryLevels", 5);
        cfg.horizonParams.portfolioPump           = cfg.startingCapital;
        cfg.horizonParams.futureTradeCount        = fi(f, "futureTradeCount", 0);
        cfg.horizonParams.stopLossFraction        = fd(f, "stopLossFraction", 1.0);
        cfg.horizonParams.stopLossHedgeCount      = fi(f, "stopLossHedgeCount", 0);
        cfg.downtrendCount                        = fi(f, "downtrendCount", 1);
        cfg.chainCycles                             = (fv(f, "chainCycles") == "1");
        cfg.savingsRate                             = fd(f, "savingsRate");

        // Parse price series
        std::string priceStr = fv(f, "priceSeries");
        {
            std::istringstream ss(priceStr);
            std::string line;
            while (std::getline(ss, line))
            {
                if (line.empty()) continue;
                // strip CR
                if (!line.empty() && line.back() == '\r') line.pop_back();
                auto comma = line.find(',');
                if (comma == std::string::npos) continue;
                try
                {
                    long long ts = std::stoll(line.substr(0, comma));
                    double px    = std::stod(line.substr(comma + 1));
                    cfg.prices.set(symbol, ts, px);
                }
                catch (...) {}
            }
        }

        if (!cfg.prices.hasSymbol(symbol))
        {
            res.set_redirect("/simulator?err=No+valid+price+data+entered", 303);
            return;
        }
        if (cfg.startingCapital <= 0)
        {
            res.set_redirect("/simulator?err=Capital+must+be+positive", 303);
            return;
        }

        // Run simulation
        auto result = Simulator::run(cfg);

        // Render results
        std::ostringstream h;
        h << std::fixed << std::setprecision(8);
        h << "<h1>&#9881; Simulation Results</h1>";
        h << "<h2>" << html::esc(symbol) << "</h2>";

        // Summary stats
        double winRate = (result.wins + result.losses > 0)
            ? static_cast<double>(result.wins) / (result.wins + result.losses) * 100.0 : 0;
        h << "<div class='row'>"
             "<div class='stat'><div class='lbl'>Starting Capital</div><div class='val'>"
          << cfg.startingCapital << "</div></div>"
             "<div class='stat'><div class='lbl'>Final Capital</div><div class='val "
          << (result.finalCapital >= cfg.startingCapital ? "buy" : "sell") << "'>"
          << result.finalCapital << "</div></div>"
             "<div class='stat'><div class='lbl'>Realized P&amp;L</div><div class='val "
          << (result.totalRealized >= 0 ? "buy" : "sell") << "'>"
          << result.totalRealized << "</div></div>"
             "<div class='stat'><div class='lbl'>Trades Opened</div><div class='val'>"
          << result.tradesOpened << "</div></div>"
             "<div class='stat'><div class='lbl'>Trades Closed</div><div class='val'>"
          << result.tradesClosed << "</div></div>"
             "<div class='stat'><div class='lbl'>Win Rate</div><div class='val "
          << (winRate >= 50 ? "buy" : "sell") << "'>"
          << winRate << "%</div></div>"
             "<div class='stat'><div class='lbl'>W / L</div><div class='val'>"
          << result.wins << " / " << result.losses << "</div></div>"
             "</div>";

        // Fee hedging analysis
        h << "<h2>Fee Hedging Analysis</h2>"
             "<div class='row'>"
             "<div class='stat'><div class='lbl'>Total Fees</div><div class='val'>"
          << result.totalFees << "</div></div>"
             "<div class='stat'><div class='lbl'>Buy Fees</div><div class='val'>"
          << result.totalBuyFees << "</div></div>"
             "<div class='stat'><div class='lbl'>Sell Fees</div><div class='val'>"
          << result.totalSellFees << "</div></div>"
             "<div class='stat'><div class='lbl'>Hedging Reserve</div><div class='val'>"
          << result.feeHedgingAmount << "</div></div>"
             "<div class='stat'><div class='lbl'>Coverage</div><div class='val "
          << (result.feeHedgingCoverage >= 1.0 ? "buy" : "sell") << "'>"
          << (result.feeHedgingCoverage * 100.0) << "%</div></div>"
             "</div>";
        if (result.feeHedgingCoverage >= 1.0)
            h << "<div class='msg'>Fee hedging fully covers all fees.</div>";
        else if (result.totalFees > 0)
            h << "<div class='msg err'>Fee hedging deficit: "
              << (result.totalFees - result.feeHedgingAmount) << " uncovered.</div>";

        // Chain mode stats
        if (result.cyclesCompleted > 0)
        {
            h << "<h2>Chain Mode</h2>"
                 "<div class='row'>"
                 "<div class='stat'><div class='lbl'>Cycles Completed</div><div class='val'>"
              << result.cyclesCompleted << "</div></div>"
                 "<div class='stat'><div class='lbl'>Savings Locked</div><div class='val buy'>"
              << result.totalSavings << "</div></div>"
                 "<div class='stat'><div class='lbl'>Capital + Savings</div><div class='val'>"
              << (result.finalCapital + result.totalSavings) << "</div></div>"
                 "</div>";
        }

        // Trades table
        if (!result.trades.empty())
        {
            h << "<h2>Entries (" << result.tradesOpened << ")</h2>"
                 "<table><tr><th>ID</th><th>Cycle</th><th>Symbol</th><th>Entry</th>"
                 "<th>Qty</th><th>Buy Fee</th><th>Cost</th><th>Remaining</th></tr>";
            for (const auto& t : result.trades)
            {
                h << "<tr><td>" << t.id << "</td>"
                  << "<td>" << t.cycle << "</td>"
                  << "<td>" << html::esc(t.symbol) << "</td>"
                  << "<td>" << t.entryPrice << "</td>"
                  << "<td>" << t.quantity << "</td>"
                  << "<td>" << t.buyFee << "</td>"
                  << "<td>" << (t.entryPrice * t.quantity + t.buyFee) << "</td>"
                  << "<td>" << t.remaining << "</td></tr>";
            }
            h << "</table>";
        }

        // Sells table
        if (!result.sells.empty())
        {
            h << "<h2>Exits (" << result.tradesClosed << ")</h2>"
                 "<table><tr><th>Buy</th><th>Cycle</th><th>Symbol</th><th>Entry</th>"
                 "<th>Exit</th><th>Qty</th><th>Sell Fee</th>"
                 "<th>Gross</th><th>Net</th></tr>";
            for (const auto& s : result.sells)
            {
                h << "<tr><td>" << s.buyId << "</td>"
                  << "<td>" << s.cycle << "</td>"
                  << "<td>" << html::esc(s.symbol) << "</td>"
                  << "<td>" << s.entryPrice << "</td>"
                  << "<td>" << s.sellPrice << "</td>"
                  << "<td>" << s.quantity << "</td>"
                  << "<td>" << s.sellFee << "</td>"
                  << "<td>" << s.grossProfit << "</td>"
                  << "<td class='" << (s.netProfit >= 0 ? "buy" : "sell") << "'>"
                  << s.netProfit << "</td></tr>";
            }
            h << "</table>";
            h << "<div class='row'>"
                 "<div class='stat'><div class='lbl'>Best Trade</div><div class='val buy'>"
              << result.bestTrade << "</div></div>"
                 "<div class='stat'><div class='lbl'>Worst Trade</div><div class='val sell'>"
              << result.worstTrade << "</div></div>"
                 "</div>";
        }

        // Capital curve (deduplicated — only show snapshots where state changed)
        if (result.snapshots.size() > 1)
        {
            // Build deduplicated list: only keep snapshots where capital/deployed/realized changed
            std::vector<const SimSnapshot*> unique;
            for (size_t i = 0; i < result.snapshots.size(); ++i)
            {
                const auto& s = result.snapshots[i];
                if (unique.empty()
                    || std::abs(s.capital  - unique.back()->capital)  > 1e-10
                    || std::abs(s.deployed - unique.back()->deployed) > 1e-10
                    || std::abs(s.realized - unique.back()->realized) > 1e-10
                    || s.openTrades != unique.back()->openTrades)
                {
                    unique.push_back(&s);
                }
            }
            // Always include the very last snapshot
            if (unique.empty() || unique.back() != &result.snapshots.back())
                unique.push_back(&result.snapshots.back());

            h << "<h2>Capital Curve (" << unique.size() << " events)</h2>"
                 "<table><tr><th>Time</th><th>Capital</th><th>Deployed</th>"
                 "<th>Total</th><th>Realized</th><th>Fees</th><th>Open</th></tr>";
            for (const auto* sp : unique)
            {
                const auto& s = *sp;
                std::time_t tt = static_cast<std::time_t>(s.timestamp);
                std::tm tm;
#ifdef _WIN32
                localtime_s(&tm, &tt);
#else
                localtime_r(&tt, &tm);
#endif
                char tbuf[32];
                std::strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M", &tm);
                h << "<tr><td>" << tbuf << "</td>"
                  << "<td>" << s.capital << "</td>"
                  << "<td>" << s.deployed << "</td>"
                  << "<td>" << (s.capital + s.deployed) << "</td>"
                  << "<td class='" << (s.realized >= 0 ? "buy" : "sell") << "'>"
                  << s.realized << "</td>"
                  << "<td>" << s.totalFees << "</td>"
                  << "<td>" << s.openTrades << "</td></tr>";
            }
            h << "</table>";
        }

        // ============= INTERACTIVE CHARTS =============
        // Serialize data to JSON for the JavaScript charts
        {
            const auto& pts = cfg.prices.data().at(symbol);

            // Price series JSON
            h << "\n<script>\nvar simPrices=[";
            for (size_t i = 0; i < pts.size(); ++i)
            {
                if (i > 0) h << ',';
                h << "{t:" << pts[i].timestamp << ",p:" << pts[i].price << "}";
            }
            h << "];\n";

            // Entries JSON
            h << "var simEntries=[";
            for (size_t i = 0; i < result.trades.size(); ++i)
            {
                if (i > 0) h << ',';
                const auto& t = result.trades[i];
                h << "{id:" << t.id
                  << ",e:" << t.entryPrice
                  << ",q:" << t.quantity
                  << ",r:" << t.remaining
                  << ",f:" << t.buyFee
                  << ",t:" << t.entryTime << "}";
            }
            h << "];\n";

            // Sells JSON
            h << "var simSells=[";
            for (size_t i = 0; i < result.sells.size(); ++i)
            {
                if (i > 0) h << ',';
                const auto& s = result.sells[i];
                h << "{bid:" << s.buyId
                  << ",ep:" << s.entryPrice
                  << ",sp:" << s.sellPrice
                  << ",q:" << s.quantity
                  << ",sf:" << s.sellFee
                  << ",gp:" << s.grossProfit
                  << ",np:" << s.netProfit
                  << ",t:" << s.sellTime << "}";
            }
            h << "];\n";

            // Snapshots JSON (deduplicated)
            h << "var simSnaps=[";
            bool firstSnap = true;
            double prevCap = -1, prevDep = -1;
            for (const auto& s : result.snapshots)
            {
                if (!firstSnap
                    && std::abs(s.capital - prevCap) < 1e-10
                    && std::abs(s.deployed - prevDep) < 1e-10)
                    continue;
                if (!firstSnap) h << ',';
                h << "{t:" << s.timestamp
                  << ",c:" << s.capital
                  << ",d:" << s.deployed
                  << ",r:" << s.realized
                  << ",f:" << s.totalFees
                  << ",o:" << s.openTrades << "}";
                prevCap = s.capital;
                prevDep = s.deployed;
                firstSnap = false;
            }
            h << "];\n";

            // Config info
            h << "var simCfg={cap:" << cfg.startingCapital
              << ",sym:'" << html::esc(symbol) << "'"
              << ",final:" << result.finalCapital
              << ",realized:" << result.totalRealized
              << ",fees:" << result.totalFees
              << "};\n";

            h << "</script>\n";
        }

        // Chart containers
        h << "<style>"
             ".sim-chart-wrap{position:relative;width:100%;background:#0b1426;"
             "border:1px solid #1a2744;border-radius:6px;margin:16px 0;overflow:hidden;}"
             ".sim-chart-wrap canvas{display:block;width:100%;}"
             ".sim-tip{position:absolute;background:#0f1b2dee;border:1px solid #1a2744;border-radius:6px;"
             "padding:6px 10px;font-size:0.75em;color:#cbd5e1;pointer-events:none;display:none;"
             "white-space:pre;z-index:10;font-family:monospace;}"
             ".chart-tabs{display:flex;gap:0;margin-top:16px;}"
             ".chart-tabs button{flex:1;background:#0b1426;border:1px solid #1a2744;color:#64748b;padding:8px;"
             "cursor:pointer;font-family:inherit;font-size:0.82em;border-radius:0;}"
             ".chart-tabs button:first-child{border-radius:6px 0 0 0;}"
             ".chart-tabs button:last-child{border-radius:0 6px 0 0;}"
             ".chart-tabs button.active{background:#1e40af;color:#fff;border-color:#1e40af;}"
             ".chart-legend{display:flex;gap:12px;flex-wrap:wrap;padding:6px 12px;"
             "background:#0f1b2d;border:1px solid #1a2744;border-top:none;font-size:0.72em;}"
             ".chart-legend span{display:inline-flex;align-items:center;gap:4px;}"
             ".chart-legend i{display:inline-block;width:16px;height:3px;border-radius:1px;}"
             "</style>";

        h << "<div class='chart-tabs' id='simTabs'>"
             "<button class='active' data-chart='price'>Price &amp; Trades</button>"
             "<button data-chart='capital'>Capital Curve</button>"
             "<button data-chart='pnl'>P&amp;L per Trade</button>"
             "</div>";

        h << "<div class='sim-chart-wrap' id='simChartWrap'>"
             "<canvas id='simCanvas'></canvas>"
             "<div class='sim-tip' id='simTip'></div>"
             "</div>";

        h << "<div class='chart-legend' id='simLegend'>"
             "<span><i style='background:#38bdf8'></i> Price</span>"
             "<span><i style='background:#22c55e'></i> Buy</span>"
             "<span><i style='background:#ef4444'></i> Sell</span>"
             "<span><i style='background:#60a5fa'></i> Capital</span>"
             "<span><i style='background:#c9a44a'></i> Deployed</span>"
             "<span><i style='background:#a78bfa'></i> Total</span>"
             "</div>";

        // Chart JavaScript
        h << R"(<script>
(function(){
'use strict';
var $=function(id){return document.getElementById(id);};
var canvas=$('simCanvas'),ctx=canvas.getContext('2d');
var wrap=$('simChartWrap'),tip=$('simTip');
var W,H,dpr;
var curChart='price';
var PAD_L=80,PAD_R=30,PAD_T=25,PAD_B=35;

function resize(){
  dpr=window.devicePixelRatio||1;
  W=wrap.clientWidth;H=Math.max(400,Math.min(600,window.innerHeight*0.5));
  canvas.width=W*dpr;canvas.height=H*dpr;
  canvas.style.width=W+'px';canvas.style.height=H+'px';
  ctx.setTransform(dpr,0,0,dpr,0,0);draw();
}
window.addEventListener('resize',resize);

function fp(v){if(Math.abs(v)>=1)return v.toFixed(2);if(Math.abs(v)>=0.01)return v.toFixed(4);return v.toFixed(8);}
function fmtTs(ts){var d=new Date(ts*1000);return d.toLocaleDateString()+' '+d.toLocaleTimeString([],{hour:'2-digit',minute:'2-digit'});}

// tabs
$('simTabs').addEventListener('click',function(e){
  if(e.target.tagName!=='BUTTON')return;
  document.querySelectorAll('#simTabs button').forEach(function(b){b.classList.remove('active');});
  e.target.classList.add('active');
  curChart=e.target.dataset.chart;draw();
});

function draw(){
  ctx.clearRect(0,0,W,H);ctx.fillStyle='#0b1426';ctx.fillRect(0,0,W,H);
  if(curChart==='price')drawPrice();
  else if(curChart==='capital')drawCapital();
  else drawPnl();
}

// ---- Price chart with trade markers ----
function drawPrice(){
  if(!simPrices.length){noData();return;}
  var ps=simPrices,tMin=ps[0].t,tMax=ps[ps.length-1].t;
  if(tMax<=tMin)tMax=tMin+1;
  var prices=ps.map(function(p){return p.p;});
  var pMin=Math.min.apply(null,prices),pMax=Math.max.apply(null,prices);
  // include entry/sell prices in range
  simEntries.forEach(function(e){if(e.e<pMin)pMin=e.e;if(e.e>pMax)pMax=e.e;});
  simSells.forEach(function(s){if(s.sp<pMin)pMin=s.sp;if(s.sp>pMax)pMax=s.sp;});
  var pad=(pMax-pMin)*0.08||1;pMin-=pad;pMax+=pad;

  var tx=function(t){return PAD_L+(t-tMin)/(tMax-tMin)*(W-PAD_L-PAD_R);};
  var py=function(p){return PAD_T+(1-(p-pMin)/(pMax-pMin))*(H-PAD_T-PAD_B);};

  drawGrid(pMin,pMax,tMin,tMax,tx,py);

  // price line
  ctx.strokeStyle='#38bdf8';ctx.lineWidth=1.5;ctx.beginPath();
  ps.forEach(function(p,i){var x=tx(p.t),y=py(p.p);if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);});
  ctx.stroke();

  // entry markers (green triangles)
  simEntries.forEach(function(e){
    var x=tx(e.t),y=py(e.e);
    ctx.fillStyle='#22c55e';ctx.beginPath();
    ctx.moveTo(x,y+8);ctx.lineTo(x-5,y+16);ctx.lineTo(x+5,y+16);ctx.closePath();ctx.fill();
    ctx.fillStyle='#22c55ecc';ctx.font='9px monospace';ctx.textAlign='center';
    ctx.fillText('#'+e.id+' '+fp(e.e),x,y+25);
  });

  // sell markers (red triangles, pointing down)
  simSells.forEach(function(s){
    var x=tx(s.t),y=py(s.sp);
    ctx.fillStyle=s.np>=0?'#ef4444':'#f59e0b';ctx.beginPath();
    ctx.moveTo(x,y-8);ctx.lineTo(x-5,y-16);ctx.lineTo(x+5,y-16);ctx.closePath();ctx.fill();
    ctx.fillStyle='#ef4444cc';ctx.font='9px monospace';ctx.textAlign='center';
    ctx.fillText(fp(s.sp),x,y-20);
  });

  // horizon lines: for each entry, draw dashed TP lines at exit trigger prices
  simSells.forEach(function(s){
    // draw a thin dashed line at the TP price across the trade's lifetime
    var buyEntry=simEntries.find(function(e){return e.id===s.bid;});
    if(!buyEntry)return;
    var x1=tx(buyEntry.t),x2=tx(s.t),yy=py(s.sp);
    ctx.save();ctx.strokeStyle='#22c55e44';ctx.lineWidth=1;ctx.setLineDash([4,3]);
    ctx.beginPath();ctx.moveTo(x1,yy);ctx.lineTo(x2,yy);ctx.stroke();ctx.restore();
  });

  // draw entry price lines across the holding period
  simEntries.forEach(function(e){
    var lastSell=0;
    simSells.forEach(function(s){if(s.bid===e.id&&s.t>lastSell)lastSell=s.t;});
    var endT=lastSell||tMax;
    var x1=tx(e.t),x2=tx(endT),yy=py(e.e);
    ctx.save();ctx.strokeStyle='#60a5fa33';ctx.lineWidth=1;ctx.setLineDash([6,4]);
    ctx.beginPath();ctx.moveTo(x1,yy);ctx.lineTo(x2,yy);ctx.stroke();ctx.restore();
  });

  setupTooltip(tx,py,tMin,tMax,pMin,pMax,'price');
}

// ---- Capital curve ----
function drawCapital(){
  if(!simSnaps.length){noData();return;}
  var ss=simSnaps,tMin=ss[0].t,tMax=ss[ss.length-1].t;
  if(tMax<=tMin)tMax=tMin+1;
  var vals=[];
  ss.forEach(function(s){vals.push(s.c);vals.push(s.c+s.d);});
  vals.push(simCfg.cap);
  var vMin=Math.min.apply(null,vals),vMax=Math.max.apply(null,vals);
  var pad=(vMax-vMin)*0.08||1;vMin-=pad;vMax+=pad;

  var tx=function(t){return PAD_L+(t-tMin)/(tMax-tMin)*(W-PAD_L-PAD_R);};
  var vy=function(v){return PAD_T+(1-(v-vMin)/(vMax-vMin))*(H-PAD_T-PAD_B);};

  drawGrid(vMin,vMax,tMin,tMax,tx,vy);

  // total (capital+deployed) area
  ctx.fillStyle='#a78bfa18';ctx.beginPath();ctx.moveTo(tx(ss[0].t),vy(0>vMin?vMin:0));
  ss.forEach(function(s){ctx.lineTo(tx(s.t),vy(s.c+s.d));});
  ctx.lineTo(tx(ss[ss.length-1].t),vy(0>vMin?vMin:0));ctx.closePath();ctx.fill();

  // total line
  ctx.strokeStyle='#a78bfa';ctx.lineWidth=1;ctx.beginPath();
  ss.forEach(function(s,i){var x=tx(s.t),y=vy(s.c+s.d);if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);});
  ctx.stroke();

  // deployed area
  ctx.fillStyle='#c9a44a18';ctx.beginPath();ctx.moveTo(tx(ss[0].t),vy(0>vMin?vMin:0));
  ss.forEach(function(s){ctx.lineTo(tx(s.t),vy(s.d));});
  ctx.lineTo(tx(ss[ss.length-1].t),vy(0>vMin?vMin:0));ctx.closePath();ctx.fill();

  // capital line
  ctx.strokeStyle='#60a5fa';ctx.lineWidth=2;ctx.beginPath();
  ss.forEach(function(s,i){var x=tx(s.t),y=vy(s.c);if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);});
  ctx.stroke();

  // deployed line
  ctx.strokeStyle='#c9a44a';ctx.lineWidth=1.5;ctx.setLineDash([6,3]);ctx.beginPath();
  ss.forEach(function(s,i){var x=tx(s.t),y=vy(s.d);if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);});
  ctx.stroke();ctx.setLineDash([]);

  // starting capital reference
  var scy=vy(simCfg.cap);
  ctx.save();ctx.strokeStyle='#64748b';ctx.lineWidth=1;ctx.setLineDash([4,4]);
  ctx.beginPath();ctx.moveTo(PAD_L,scy);ctx.lineTo(W-PAD_R,scy);ctx.stroke();
  ctx.fillStyle='#64748b';ctx.textAlign='left';ctx.font='9px monospace';
  ctx.fillText('Start: '+fp(simCfg.cap),PAD_L+4,scy-4);ctx.restore();

  // mark buy/sell events on capital line
  simEntries.forEach(function(e){
    var snap=findSnap(e.t);if(!snap)return;
    var x=tx(e.t),y=vy(snap.c);
    ctx.fillStyle='#22c55e';ctx.beginPath();ctx.arc(x,y,4,0,Math.PI*2);ctx.fill();
  });
  simSells.forEach(function(s){
    var snap=findSnap(s.t);if(!snap)return;
    var x=tx(s.t),y=vy(snap.c);
    ctx.fillStyle='#ef4444';ctx.beginPath();ctx.arc(x,y,4,0,Math.PI*2);ctx.fill();
  });

  setupTooltip(tx,vy,tMin,tMax,vMin,vMax,'capital');
}

// ---- P&L per trade (bar chart) ----
function drawPnl(){
  if(!simSells.length){noData();return;}
  var bars=simSells.map(function(s,i){return{i:i,np:s.np,bid:s.bid,sp:s.sp,q:s.q};});
  var maxAbs=Math.max.apply(null,bars.map(function(b){return Math.abs(b.np);}));
  if(maxAbs<1e-12)maxAbs=1;
  var pad=maxAbs*0.1;var vMin=-maxAbs-pad,vMax=maxAbs+pad;

  var barW=Math.max(8,Math.min(40,(W-PAD_L-PAD_R-bars.length*2)/bars.length));
  var gap=2;

  // grid
  var gs=8,gst=(vMax-vMin)/gs;
  ctx.font='10px monospace';ctx.fillStyle='#475569';ctx.textAlign='right';
  for(var i=0;i<=gs;i++){
    var v=vMin+gst*i,y=PAD_T+(1-(v-vMin)/(vMax-vMin))*(H-PAD_T-PAD_B);
    ctx.strokeStyle='#152238';ctx.lineWidth=1;ctx.beginPath();
    ctx.moveTo(PAD_L,y);ctx.lineTo(W-PAD_R,y);ctx.stroke();
    ctx.fillText(fp(v),PAD_L-4,y+3);
  }
  // zero line
  var zy=PAD_T+(1-(0-vMin)/(vMax-vMin))*(H-PAD_T-PAD_B);
  ctx.strokeStyle='#475569';ctx.lineWidth=1;ctx.beginPath();
  ctx.moveTo(PAD_L,zy);ctx.lineTo(W-PAD_R,zy);ctx.stroke();

  bars.forEach(function(b,i){
    var x=PAD_L+10+i*(barW+gap);
    var by=PAD_T+(1-(b.np-vMin)/(vMax-vMin))*(H-PAD_T-PAD_B);
    var bh=Math.abs(zy-by);
    ctx.fillStyle=b.np>=0?'#166534':'#991b1b';
    if(b.np>=0)ctx.fillRect(x,by,barW,bh);
    else ctx.fillRect(x,zy,barW,bh);
    // label
    ctx.fillStyle=b.np>=0?'#22c55e':'#ef4444';ctx.font='8px monospace';ctx.textAlign='center';
    ctx.fillText(fp(b.np),x+barW/2,b.np>=0?by-3:zy+bh+9);
    ctx.fillStyle='#64748b';ctx.fillText('#'+b.bid,x+barW/2,H-PAD_B+12);
  });

  // cumulative P&L line overlay
  var cum=0;
  ctx.strokeStyle='#c9a44a';ctx.lineWidth=2;ctx.beginPath();
  bars.forEach(function(b,i){
    cum+=b.np;
    var x=PAD_L+10+i*(barW+gap)+barW/2;
    var y=PAD_T+(1-(cum-vMin)/(vMax-vMin))*(H-PAD_T-PAD_B);
    if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);
  });
  ctx.stroke();
}

// ---- Helpers ----
function drawGrid(yMin,yMax,tMin,tMax,tx,py){
  var gs=8,gst=(yMax-yMin)/gs;
  ctx.font='10px monospace';ctx.fillStyle='#475569';ctx.textAlign='right';
  for(var i=0;i<=gs;i++){
    var v=yMin+gst*i,y=py(v);
    ctx.strokeStyle='#152238';ctx.lineWidth=1;ctx.beginPath();
    ctx.moveTo(PAD_L,y);ctx.lineTo(W-PAD_R,y);ctx.stroke();
    ctx.fillText(fp(v),PAD_L-4,y+3);
  }
  // time labels
  var tSteps=Math.min(8,Math.max(3,Math.floor(W/120)));
  ctx.textAlign='center';ctx.fillStyle='#475569';ctx.font='9px monospace';
  for(var i=0;i<=tSteps;i++){
    var t=tMin+(tMax-tMin)*i/tSteps;
    var x=tx(t);
    ctx.fillText(fmtTs(t),x,H-4);
  }
}

function findSnap(t){
  var best=null;
  simSnaps.forEach(function(s){if(!best||Math.abs(s.t-t)<Math.abs(best.t-t))best=s;});
  return best;
}

function noData(){
  ctx.fillStyle='#475569';ctx.font='14px monospace';ctx.textAlign='center';
  ctx.fillText('No data to display',W/2,H/2);
}

// Tooltip
var tipActive=false;
function setupTooltip(tx,vy,tMin,tMax,vMin,vMax,mode){
  canvas.onmousemove=function(e){
    var r=canvas.getBoundingClientRect();
    var mx=e.clientX-r.left,my=e.clientY-r.top;
    if(mx<PAD_L||mx>W-PAD_R||my<PAD_T||my>H-PAD_B){tip.style.display='none';return;}
    var t=tMin+(mx-PAD_L)/(W-PAD_L-PAD_R)*(tMax-tMin);
    var v=vMin+(1-(my-PAD_T)/(H-PAD_T-PAD_B))*(vMax-vMin);

    var lines=fmtTs(t)+'\n';
    if(mode==='price'){
      // find nearest price point
      var best=null,bd=Infinity;
      simPrices.forEach(function(p){var d=Math.abs(p.t-t);if(d<bd){bd=d;best=p;}});
      if(best)lines+='Price: '+fp(best.p)+'\n';
      // nearby entries
      simEntries.forEach(function(e){if(Math.abs(tx(e.t)-mx)<15)lines+='BUY #'+e.id+' @ '+fp(e.e)+' qty='+fp(e.q)+'\n';});
      simSells.forEach(function(s){if(Math.abs(tx(s.t)-mx)<15)lines+='SELL #'+s.bid+' @ '+fp(s.sp)+' net='+fp(s.np)+'\n';});
    } else if(mode==='capital'){
      var snap=findSnap(t);
      if(snap){lines+='Liquid: '+fp(snap.c)+'\nDeployed: '+fp(snap.d)+'\nTotal: '+fp(snap.c+snap.d)+'\nRealized: '+fp(snap.r)+'\nOpen: '+snap.o;}
    }
    tip.textContent=lines;tip.style.display='block';
    var tx2=mx+14,ty=my-10;
    if(tx2+200>W)tx2=mx-200;
    tip.style.left=tx2+'px';tip.style.top=ty+'px';
  };
  canvas.onmouseleave=function(){tip.style.display='none';};
}

resize();
})();
</script>)";

        h << "<br><a class='btn' href='/simulator'>New Simulation</a> "
             "<a class='btn' href='/'>Dashboard</a>";
        res.set_content(html::wrap("Simulation Results", h.str()), "text/html");
    });
}
