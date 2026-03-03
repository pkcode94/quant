#pragma once

#include "AppContext.h"
#include "HtmlHelpers.h"
#include "MarketEntryCalculator.h"
#include "QuantMath.h"
#include <mutex>
#include <algorithm>
#include <cmath>

inline void registerEntryRoutes(httplib::Server& svr, AppContext& ctx)
{
    auto& db = ctx.defaultDb;
    auto& dbMutex = ctx.dbMutex;

    // ========== GET /market-entry � form ==========
    svr.Get("/market-entry", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        std::ostringstream h;
        h << std::fixed << std::setprecision(17);
        h << html::msgBanner(req) << html::errBanner(req);
        double walBal = db.loadWalletBalance();
        { bool canH = db.hasBuyTrades(); h << html::workflow(0, canH, canH); }
        h << "<h1>Market Entry Calculator</h1>"
             "<div class='row'><div class='stat'><div class='lbl'>Wallet</div><div class='val'>" << walBal << "</div></div></div><br>"
             "<form class='card' method='POST' action='/market-entry'><h3>Parameters</h3>"
             "<label>Symbol</label><input type='text' name='symbol' required><br>"
             "<label>Current Price</label><input type='number' name='currentPrice' step='any' required><br>"
             "<label>Quantity</label><input type='number' name='quantity' step='any' required><br>"
             "<label>Levels</label><input type='number' name='levels' value='4'><br>"
             "<label>Fee Hedging</label><input type='number' name='feeHedgingCoefficient' step='any' value='1'><br>"
             "<label>Pump</label><input type='number' name='portfolioPump' step='any' value='0'><br>"
             "<label>Symbol Count</label><input type='number' name='symbolCount' value='1'><br>"
             "<label>Coefficient K</label><input type='number' name='coefficientK' step='any' value='0'><br>"
             "<label>Fee Spread</label><input type='number' name='feeSpread' step='any' value='0'><br>"
             "<label>Delta Time</label><input type='number' name='deltaTime' step='any' value='1'><br>"
             "<label>Surplus Rate</label><input type='number' name='surplusRate' step='any' value='0.02'><br>"
             "<label>Max Risk</label><input type='number' name='maxRisk' step='any' value='0'><br>"
             "<label>Min Risk</label><input type='number' name='minRisk' step='any' value='0'><br>"
             "<label>Risk</label><input type='number' name='risk' step='any' value='0.5'><br>"
             "<label>Steepness</label><input type='number' name='steepness' step='any' value='6'><br>"
             "<label>Direction</label><select name='isShort'><option value='0'>LONG</option><option value='1'>SHORT</option></select><br>"
             "<label>Funding</label><select name='fundMode'><option value='1'>Pump only</option><option value='2'>Pump + Wallet</option></select><br>"
             "<label>Range Above</label><input type='number' name='rangeAbove' step='any' value='0'><br>"
             "<label>Range Below</label><input type='number' name='rangeBelow' step='any' value='0'><br>"
             "<button>Calculate</button></form>";
        res.set_content(html::wrap("Market Entry", h.str()), "text/html");
    });

    // ========== POST /market-entry � compute + show results + execute form ==========
    svr.Post("/market-entry", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        std::string sym = normalizeSymbol(fv(f, "symbol"));
        double cur = fd(f, "currentPrice");
        double qty = fd(f, "quantity");
        double risk = fd(f, "risk");
        double steepness = fd(f, "steepness", 6.0);
        bool isShort = (fv(f, "isShort") == "1");
        int fundMode = fi(f, "fundMode", 1);
        double rangeAbove = fd(f, "rangeAbove");
        double rangeBelow = fd(f, "rangeBelow");
        HorizonParams p;
        p.horizonCount = fi(f, "levels", 4);
        p.feeHedgingCoefficient = fd(f, "feeHedgingCoefficient", 1.0);
        p.portfolioPump = fd(f, "portfolioPump");
        p.symbolCount = fi(f, "symbolCount", 1);
        p.coefficientK = fd(f, "coefficientK");
        p.feeSpread = fd(f, "feeSpread");
        p.deltaTime = fd(f, "deltaTime", 1.0);
        p.surplusRate = fd(f, "surplusRate");
        p.maxRisk = fd(f, "maxRisk");
        p.minRisk = fd(f, "minRisk");
        double walBal = db.loadWalletBalance();
        double availableFunds = p.portfolioPump;
        if (fundMode == 2) availableFunds += walBal;
        HorizonParams entryParams = p;
        entryParams.portfolioPump = availableFunds;
        std::ostringstream h;
        h << std::fixed << std::setprecision(17);
        if (sym.empty() || cur <= 0 || qty <= 0)
        { h << "<div class='msg err'>Symbol, price, and quantity are required</div><br><a class='btn' href='/market-entry'>Back</a>";
          res.set_content(html::wrap("Market Entry", h.str()), "text/html"); return; }
        auto levels = MarketEntryCalculator::generate(cur, qty, entryParams, risk, steepness, rangeAbove, rangeBelow);
        double eo = MultiHorizonEngine::effectiveOverhead(cur, qty, p);
        double overhead = MultiHorizonEngine::computeOverhead(cur, qty, p);
        double posDelta = MultiHorizonEngine::positionDelta(cur, qty, p.portfolioPump);
        db.saveParamsSnapshot(TradeDatabase::ParamsRow::from("entry", sym, -1, cur, qty, p, risk));
        h << "<h1>Entry Strategy: " << html::esc(sym) << " @ " << std::setprecision(17) << cur << "</h1>";
        { bool canH = db.hasBuyTrades(); h << html::workflow(0, canH, canH); }
        h << std::fixed << std::setprecision(17);
        h << "<div class='row'>"
             "<div class='stat'><div class='lbl'>Overhead</div><div class='val'>" << (overhead * 100) << "%</div></div>"
             "<div class='stat'><div class='lbl'>Surplus</div><div class='val'>" << (p.surplusRate * 100) << "%</div></div>"
             "<div class='stat'><div class='lbl'>Effective</div><div class='val'>" << (eo * 100) << "%</div></div>"
             "<div class='stat'><div class='lbl'>Pos Delta</div><div class='val'>" << (posDelta * 100) << "%</div></div>"
             "</div>";
        h << std::fixed << std::setprecision(17);
        h << "<div class='row'>"
             "<div class='stat'><div class='lbl'>Pump</div><div class='val'>" << p.portfolioPump << "</div></div>"
             "<div class='stat'><div class='lbl'>Wallet</div><div class='val'>" << walBal << "</div></div>"
             "<div class='stat'><div class='lbl'>Available</div><div class='val'>" << availableFunds << "</div></div>"
             "<div class='stat'><div class='lbl'>Direction</div><div class='val'>" << (isShort ? "SHORT" : "LONG") << "</div></div>"
             "</div>";
        {
            double lvlOh = MultiHorizonEngine::computeOverhead(cur, qty, entryParams);
            std::ostringstream con;
            con << std::fixed << std::setprecision(8);
            con << "<details><summary style='cursor:pointer;color:#c9a44a;font-size:0.85em;margin:8px 0;'>"
                   "&#9654; Calculation Steps</summary><div class='calc-console'>";
            con << html::traceOverhead(cur, qty, entryParams);
            con << "<span class='hd'>Sigmoid Distribution</span>"
                << "steepness = <span class='vl'>" << steepness << "</span>\n"
                << "Level 0 = near 0, Level N-1 = currentPrice, sigmoid-interpolated.\n"
                << "Funding = inverse sigmoid warped by risk (risk=1: most at low prices, risk=0: most at high prices).\n"
                << "BreakEven uses overhead. TP/SL use effective overhead.\n";
            con << "<span class='hd'>Position</span>"
                << "positionDelta = (" << cur << " &times; " << qty << ") / " << p.portfolioPump
                << " = <span class='vl'>" << posDelta << " (" << (posDelta * 100) << "%)</span>\n"
                << "availableFunds = <span class='vl'>" << availableFunds << "</span>\n";
            {
                int cN = p.horizonCount;
                double cSteep = (steepness < 0.1) ? 0.1 : steepness;
                auto norm = QuantMath::sigmoidNormN(cN, cSteep);
                con << "<span class='hd'>Entry Levels (overhead=" << lvlOh << ")</span>";
                for (const auto& el : levels)
                {
                    double ct = (cN > 1) ? static_cast<double>(el.index) / static_cast<double>(cN - 1) : 1.0;
                    double cn = norm[el.index];
                    con << "\n<span class='vl'>Level " << el.index << "</span>\n"
                        << "  t          = " << el.index << " / " << (cN - 1) << " = " << ct << "\n"
                        << "  sigmoid    = <span class='vl'>" << cn << "</span>\n"
                        << "  entryPrice = " << cur << " &times; " << cn << " = <span class='vl'>" << el.entryPrice << "</span>\n"
                        << "  breakEven  = " << el.entryPrice << " &times; (1 + " << lvlOh << ") = <span class='vl'>" << el.breakEven << "</span>\n"
                        << "  fundFrac   = <span class='vl'>" << (el.fundingFraction * 100) << "%</span>"
                        << "  funding = <span class='vl'>" << el.funding << "</span>"
                        << "  qty = <span class='vl'>" << el.fundingQty << "</span>\n"
                        << "  <span class='rs'>potentialNet</span> = (" << cur << " - " << el.entryPrice << ") &times; " << el.fundingQty
                        << " = <span class='rs'>" << el.potentialNet << "</span>\n";
                }
            }
            con << "</div></details>";
            h << con.str();
        }
        double tpRef = (rangeAbove > 0.0 || rangeBelow > 0.0) ? cur + rangeAbove : cur;
        h << "<h2>Entry Levels</h2>"
             "<table><tr><th>Lvl</th><th>Entry Price</th><th>Discount</th>"
             "<th>Break Even</th><th>Net Profit</th><th>Funding</th><th>Fund %</th>"
             "<th>Qty</th><th>Cost</th><th>Coverage</th><th>Exit TP</th><th>Exit SL</th></tr>";
        for (const auto& el : levels)
        {
            double disc = QuantMath::discount(cur, el.entryPrice);
            double exitTP = MultiHorizonEngine::levelTP(el.entryPrice, overhead, eo, p, steepness, el.index, p.horizonCount, isShort, risk, tpRef);
            double exitSL = MultiHorizonEngine::levelSL(el.entryPrice, eo, isShort);
            double cost = el.entryPrice * el.fundingQty;
            h << "<tr><td>" << el.index << "</td><td>" << el.entryPrice << "</td><td>" << disc << "%</td>"
              << "<td>" << el.breakEven << "</td><td>" << el.potentialNet << "</td><td>" << el.funding << "</td>"
              << "<td>" << (el.fundingFraction * 100) << "%</td><td>" << el.fundingQty << "</td><td>" << cost << "</td>"
              << "<td>" << el.costCoverage << "x</td><td class='buy'>" << exitTP << "</td><td class='sell'>" << exitSL << "</td></tr>";
        }
        h << "</table>";
        h << "<h2>Save Entry Strategy</h2>"
             "<form class='card' method='POST' action='/execute-entries'>"
             "<h3>Save as pending entries (buy fees entered when price hits)</h3>"
             "<input type='hidden' name='symbol' value='" << html::esc(sym) << "'>"
             "<input type='hidden' name='currentPrice' value='" << cur << "'>"
             "<input type='hidden' name='quantity' value='" << qty << "'>"
             "<input type='hidden' name='risk' value='" << risk << "'>"
             "<input type='hidden' name='steepness' value='" << steepness << "'>"
             "<input type='hidden' name='isShort' value='" << (isShort ? "1" : "0") << "'>"
             "<input type='hidden' name='fundMode' value='" << fundMode << "'>"
             "<input type='hidden' name='levels' value='" << p.horizonCount << "'>"
             "<input type='hidden' name='feeHedgingCoefficient' value='" << p.feeHedgingCoefficient << "'>"
             "<input type='hidden' name='portfolioPump' value='" << p.portfolioPump << "'>"
             "<input type='hidden' name='symbolCount' value='" << p.symbolCount << "'>"
             "<input type='hidden' name='coefficientK' value='" << p.coefficientK << "'>"
             "<input type='hidden' name='feeSpread' value='" << p.feeSpread << "'>"
             "<input type='hidden' name='deltaTime' value='" << p.deltaTime << "'>"
             "<input type='hidden' name='surplusRate' value='" << p.surplusRate << "'>"
             "<input type='hidden' name='maxRisk' value='" << p.maxRisk << "'>"
             "<input type='hidden' name='minRisk' value='" << p.minRisk << "'>"
             "<input type='hidden' name='rangeAbove' value='" << rangeAbove << "'>"
             "<input type='hidden' name='rangeBelow' value='" << rangeBelow << "'>"
             "<table><tr><th>Lvl</th><th>Entry</th><th>Qty</th><th>Cost</th></tr>";
        for (const auto& el : levels)
        {
            if (el.fundingQty <= 0) continue;
            double cost = el.entryPrice * el.fundingQty;
            h << "<tr><td>" << el.index << "</td><td>" << el.entryPrice << "</td><td>" << el.fundingQty << "</td><td>" << cost << "</td></tr>";
        }
        h << "</table><button>Save Entry Points</button></form>";
        h << "<br><a class='btn' href='/market-entry'>Back</a>";
        res.set_content(html::wrap("Market Entry Results", h.str()), "text/html");
    });

    // ========== POST /execute-entries � save pending entry points ==========
    svr.Post("/execute-entries", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        std::string sym = normalizeSymbol(fv(f, "symbol"));
        double cur = fd(f, "currentPrice");
        double qty = fd(f, "quantity");
        double risk = fd(f, "risk");
        double steepness = fd(f, "steepness", 6.0);
        bool isShort = (fv(f, "isShort") == "1");
        int fundMode = fi(f, "fundMode", 1);
        double rangeAbove = fd(f, "rangeAbove");
        double rangeBelow = fd(f, "rangeBelow");
        HorizonParams p;
        p.horizonCount = fi(f, "levels", 4);
        p.feeHedgingCoefficient = fd(f, "feeHedgingCoefficient", 1.0);
        p.portfolioPump = fd(f, "portfolioPump");
        p.symbolCount = fi(f, "symbolCount", 1);
        p.coefficientK = fd(f, "coefficientK");
        p.feeSpread = fd(f, "feeSpread");
        p.deltaTime = fd(f, "deltaTime", 1.0);
        p.surplusRate = fd(f, "surplusRate");
        p.maxRisk = fd(f, "maxRisk");
        p.minRisk = fd(f, "minRisk");
        double walBal = db.loadWalletBalance();
        double availableFunds = p.portfolioPump;
        if (fundMode == 2) availableFunds += walBal;
        HorizonParams entryParams = p;
        entryParams.portfolioPump = availableFunds;
        if (sym.empty() || cur <= 0 || qty <= 0)
        { res.set_redirect("/market-entry?err=Invalid+parameters", 303); return; }
        auto levels = MarketEntryCalculator::generate(cur, qty, entryParams, risk, steepness, rangeAbove, rangeBelow);
        double eo = MultiHorizonEngine::effectiveOverhead(cur, qty, p);
        double overhead = MultiHorizonEngine::computeOverhead(cur, qty, p);
        if (p.portfolioPump > 0) db.deposit(p.portfolioPump);
        int nextEpId = db.nextEntryId();
        std::vector<TradeDatabase::EntryPoint> entryPoints;
        std::ostringstream h;
        h << std::fixed << std::setprecision(17);
        h << "<h1>Entry Points Saved: " << html::esc(sym) << "</h1>";
        if (p.portfolioPump > 0)
            h << "<div class='msg'>Deposited pump " << p.portfolioPump << " into wallet. Balance: " << db.loadWalletBalance() << "</div>";
        double tpRef = (rangeAbove > 0.0 || rangeBelow > 0.0) ? cur + rangeAbove : cur;
        h << "<table><tr><th>Lvl</th><th>Entry</th><th>Qty</th><th>Cost</th><th>Exit TP</th><th>Exit SL</th><th>Status</th></tr>";
        for (size_t i = 0; i < levels.size(); ++i)
        {
            const auto& el = levels[i];
            if (el.fundingQty <= 0) continue;
            double cost = el.entryPrice * el.fundingQty;
            double exitTP = MultiHorizonEngine::levelTP(el.entryPrice, overhead, eo, p, steepness, el.index, p.horizonCount, isShort, risk, tpRef);
            double exitSL = MultiHorizonEngine::levelSL(el.entryPrice, eo, isShort);
            TradeDatabase::EntryPoint ep;
            ep.symbol = sym; ep.entryId = nextEpId++; ep.levelIndex = el.index;
            ep.entryPrice = el.entryPrice; ep.breakEven = el.breakEven;
            ep.funding = el.funding; ep.fundingQty = el.fundingQty;
            ep.effectiveOverhead = eo; ep.isShort = isShort;
            ep.exitTakeProfit = exitTP; ep.exitStopLoss = exitSL;
            ep.traded = false; ep.linkedTradeId = -1;
            entryPoints.push_back(ep);
            h << "<tr><td>" << el.index << "</td><td>" << el.entryPrice << "</td><td>" << el.fundingQty << "</td>"
              << "<td>" << cost << "</td><td>" << exitTP << "</td><td>" << exitSL << "</td><td class='buy'>PENDING</td></tr>";
        }
        h << "</table>";
        auto existingEp = db.loadEntryPoints();
        for (const auto& ep : entryPoints) existingEp.push_back(ep);
        db.saveEntryPoints(existingEp);
        h << "<div class='row'><div class='stat'><div class='lbl'>Saved</div><div class='val'>" << entryPoints.size() << "</div></div>"
             "<div class='stat'><div class='lbl'>Wallet</div><div class='val'>" << db.loadWalletBalance() << "</div></div></div>";
        h << "<div class='msg'>" << entryPoints.size() << " entry point(s) saved as pending. Use Price Check to execute when price hits entry levels.</div>";
        h << "<br><a class='btn' href='/entry-points'>Entry Points</a> <a class='btn' href='/price-check'>Price Check</a> <a class='btn' href='/market-entry'>New Entry</a>";
        res.set_content(html::wrap("Entry Points Saved", h.str()), "text/html");
    });

    // ========== GET /entry-points ==========
    svr.Get("/entry-points", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        std::ostringstream h;
        h << std::fixed << std::setprecision(17);
        h << html::msgBanner(req) << html::errBanner(req);
        h << "<h1>Entry Points</h1>";
        auto pts = db.loadEntryPoints();
        if (pts.empty()) { h << "<p class='empty'>(no entry points)</p>"; }
        else
        {
            h << "<table><tr><th>ID</th><th>Symbol</th><th>Lvl</th><th>Entry</th><th>BE</th><th>Qty</th><th>Funding</th><th>Dir</th><th>Status</th><th>TP</th><th>SL</th><th>Actions</th></tr>";
            for (const auto& ep : pts)
            {
                h << "<tr><td>" << ep.entryId << "</td><td>" << html::esc(ep.symbol) << "</td><td>" << ep.levelIndex << "</td>"
                  << "<td>" << ep.entryPrice << "</td><td>" << ep.breakEven << "</td><td>" << ep.fundingQty << "</td>"
                  << "<td>" << ep.funding << "</td><td>" << (ep.isShort ? "SHORT" : "LONG") << "</td>"
                  << "<td class='" << (ep.traded ? "buy" : "off") << "'>" << (ep.traded ? "TRADED" : "OPEN") << "</td>"
                  << "<td>" << ep.exitTakeProfit << "</td><td>" << ep.exitStopLoss << "</td><td>";
                if (!ep.traded)
                    h << "<a class='btn btn-sm' href='/edit-entry?id=" << ep.entryId << "'>Edit</a> "
                      << "<form class='iform' method='POST' action='/delete-entry'>"
                      << "<input type='hidden' name='id' value='" << ep.entryId << "'><button class='btn-sm btn-danger'>Del</button></form>";
                h << "</td></tr>";
            }
            h << "</table>";
            std::vector<std::string> openSyms;
            for (const auto& ep : pts)
                if (!ep.traded && ep.funding > 0 && std::find(openSyms.begin(), openSyms.end(), ep.symbol) == openSyms.end())
                    openSyms.push_back(ep.symbol);
            if (!openSyms.empty())
            {
                h << "<h2>Check Price &amp; Execute</h2><form class='card' method='POST' action='/entry-points'>"
                     "<h3>Enter current market prices to find triggered entries</h3>";
                for (const auto& sym : openSyms)
                    h << "<label>" << html::esc(sym) << "</label><input type='number' name='price_" << html::esc(sym) << "' step='any' required><br>";
                h << "<br><button>Check Triggers</button></form>";
            }
        }
        h << "<div class='row'><div class='stat'><div class='lbl'>Wallet</div><div class='val'>" << db.loadWalletBalance() << "</div></div></div>";
        res.set_content(html::wrap("Entry Points", h.str()), "text/html");
    });

    // ========== POST /entry-points � show triggered entries for execution ==========
    svr.Post("/entry-points", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        auto pts = db.loadEntryPoints();
        auto priceFor = [&](const std::string& sym) -> double { return fd(f, "price_" + sym, 0.0); };
        std::ostringstream h;
        h << std::fixed << std::setprecision(17);
        h << "<h1>Entry Points � Trigger Check</h1>";
        std::vector<std::string> openSyms;
        for (const auto& ep : pts)
            if (!ep.traded && ep.funding > 0 && std::find(openSyms.begin(), openSyms.end(), ep.symbol) == openSyms.end())
                openSyms.push_back(ep.symbol);
        h << "<form class='card' method='POST' action='/entry-points'><h3>Update prices</h3>";
        for (const auto& sym : openSyms)
        { double p = priceFor(sym); h << "<label>" << html::esc(sym) << "</label><input type='number' name='price_" << html::esc(sym) << "' step='any' value='" << p << "' required><br>"; }
        h << "<br><button>Check Triggers</button></form>";
        h << "<table><tr><th>ID</th><th>Symbol</th><th>Lvl</th><th>Entry</th><th>Market</th><th>Qty</th><th>Dir</th><th>Status</th><th>Trigger</th></tr>";
        for (const auto& ep : pts)
        {
            if (ep.traded || ep.funding <= 0) continue;
            double cur = priceFor(ep.symbol);
            bool hit = false;
            if (cur >= 0) hit = ep.isShort ? (cur >= ep.entryPrice) : (cur <= ep.entryPrice);
            h << "<tr><td>" << ep.entryId << "</td><td>" << html::esc(ep.symbol) << "</td><td>" << ep.levelIndex << "</td>"
              << "<td>" << ep.entryPrice << "</td><td>" << cur << "</td><td>" << ep.fundingQty << "</td>"
              << "<td>" << (ep.isShort ? "SHORT" : "LONG") << "</td><td class='off'>OPEN</td>"
              << "<td class='" << (hit ? "buy" : "off") << "'>" << (hit ? "HIT" : "BELOW") << "</td></tr>";
        }
        h << "</table>";
        struct Triggered { int entryId; std::string symbol; double entryPrice; double fundingQty; double exitTP; double exitSL; bool isShort; double marketPrice; };
        std::vector<Triggered> hits;
        for (const auto& ep : pts)
        {
            if (ep.traded || ep.funding <= 0) continue;
            double cur = priceFor(ep.symbol);
            if (cur < 0) continue;
            double eqty = (ep.fundingQty > 0) ? ep.fundingQty : (ep.entryPrice > 0) ? ep.funding / ep.entryPrice : 0;
            bool hit = ep.isShort ? (cur >= ep.entryPrice) : (cur <= ep.entryPrice);
            if (hit) hits.push_back({ep.entryId, ep.symbol, ep.entryPrice, eqty, ep.exitTakeProfit, ep.exitStopLoss, ep.isShort, cur});
        }
        if (!hits.empty())
        {
            double walBal = db.loadWalletBalance();
            h << "<h2>Triggered Entries (" << hits.size() << ")</h2><form class='card' method='POST' action='/execute-entry-points'>"
                 "<table><tr><th>ID</th><th>Symbol</th><th>Entry</th><th>Market</th><th>Qty</th><th>Cost</th><th>TP</th><th>SL</th><th>Buy Fee</th></tr>";
            double totalCost = 0;
            for (const auto& te : hits)
            {
                double cost = te.entryPrice * te.fundingQty; totalCost += cost;
                h << "<tr><td>" << te.entryId << "</td><td>" << html::esc(te.symbol) << "</td><td>" << te.entryPrice << "</td>"
                  << "<td class='buy'>" << te.marketPrice << "</td><td>" << te.fundingQty << "</td><td>" << cost << "</td>"
                  << "<td>" << te.exitTP << "</td><td>" << te.exitSL << "</td>"
                  << "<td><input type='number' name='fee_" << te.entryId << "' step='any' value='0' style='width:80px;'>"
                  << "<input type='hidden' name='exec_" << te.entryId << "' value='1'></td></tr>";
            }
            h << "</table><div class='row'>"
                 "<div class='stat'><div class='lbl'>Total Cost</div><div class='val'>" << totalCost << "</div></div>"
                 "<div class='stat'><div class='lbl'>Wallet</div><div class='val'>" << walBal << "</div></div>"
                 "<div class='stat'><div class='lbl'>After</div><div class='val'>" << (walBal - totalCost) << "</div></div></div>";
            h << "<br><button>Execute " << hits.size() << " Triggered Entries</button></form>";
        }
        else { h << "<div class='msg'>No entries triggered at current prices</div>"; }
        h << "<div class='row'><div class='stat'><div class='lbl'>Wallet</div><div class='val'>" << db.loadWalletBalance() << "</div></div></div>";
        h << "<br><a class='btn' href='/entry-points'>Back</a>";
        res.set_content(html::wrap("Entry Triggers", h.str()), "text/html");
    });

    // ========== POST /execute-entry-points ==========
    svr.Post("/execute-entry-points", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        auto entryPts = db.loadEntryPoints();
        int executed = 0, failed = 0;
        for (auto& ep : entryPts)
        {
            if (ep.traded) continue;
            if (fv(f, "exec_" + std::to_string(ep.entryId)).empty()) continue;
            if (ep.funding <= 0) continue;
            double execQty = (ep.fundingQty > 0) ? ep.fundingQty : QuantMath::fundedQty(ep.entryPrice, ep.funding);
            if (execQty <= 0) continue;
            double buyFee = fd(f, "fee_" + std::to_string(ep.entryId));
            double execCost = QuantMath::cost(ep.entryPrice, execQty) + buyFee;
            double walBal = db.loadWalletBalance();
            if (execCost > walBal) { ++failed; continue; }
            int bid = db.executeBuy(ep.symbol, ep.entryPrice, execQty, buyFee);
            ep.traded = true; ep.linkedTradeId = bid; ++executed;
        }
        if (executed > 0)
        {
            // Create exit points for each executed entry
            auto exits = db.loadExitPoints();
            for (const auto& ep : entryPts)
            {
                if (ep.linkedTradeId < 0) continue;
                if (ep.exitTakeProfit <= 0 && ep.exitStopLoss <= 0) continue;
                TradeDatabase::ExitPoint xp;
                xp.exitId    = db.nextExitId();
                xp.tradeId   = ep.linkedTradeId;
                xp.symbol    = ep.symbol;
                xp.levelIndex = ep.levelIndex;
                xp.tpPrice   = ep.exitTakeProfit;
                xp.slPrice   = ep.exitStopLoss;
                xp.sellQty   = ep.fundingQty;
                xp.slActive  = (ep.exitStopLoss > 0);
                exits.push_back(xp);
            }
            db.saveExitPoints(exits);
        }
        db.saveEntryPoints(entryPts);
        if (failed > 0)
            res.set_redirect("/entry-points?msg=" + std::to_string(executed) + "+executed,+" + std::to_string(failed) + "+failed+(insufficient+funds)", 303);
        else
            res.set_redirect("/entry-points?msg=" + std::to_string(executed) + "+entries+executed+as+trades", 303);
    });

    // ========== GET /edit-entry ==========
    svr.Get("/edit-entry", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        std::ostringstream h;
        h << std::fixed << std::setprecision(17);
        h << html::msgBanner(req) << html::errBanner(req);
        int id = 0;
        try { id = std::stoi(req.get_param_value("id")); } catch (...) {}
        auto pts = db.loadEntryPoints();
        TradeDatabase::EntryPoint* found = nullptr;
        for (auto& ep : pts) if (ep.entryId == id) { found = &ep; break; }
        if (!found || found->traded)
        { h << "<h1>Edit Entry</h1><div class='msg err'>Entry not found or already traded</div>"; }
        else
        {
            auto& ep = *found;
            double eo = ep.effectiveOverhead;
            h << "<h1>Edit Entry #" << ep.entryId << " " << html::esc(ep.symbol) << "</h1>"
                 "<form class='card' method='POST' action='/edit-entry'>"
                 "<input type='hidden' name='id' value='" << ep.entryId << "'>"
                 "<label>Entry Price</label><input type='number' name='entryPrice' step='any' value='" << ep.entryPrice << "'><br>"
                 "<label>Funding</label><input type='number' name='funding' step='any' value='" << ep.funding << "'><br>"
                 "<label>Qty</label><input type='number' name='fundingQty' step='any' value='" << ep.fundingQty << "'>"
                 "<div style='color:#475569;font-size:0.78em;'>Leave 0 to auto-compute from funding/price</div>"
                 "<label>TP/unit</label><input type='number' name='exitTP' step='any' value='" << ep.exitTakeProfit << "'><br>"
                 "<label>SL/unit</label><input type='number' name='exitSL' step='any' value='" << ep.exitStopLoss << "'><br>"
                 "<label>Break Even</label><input type='number' name='breakEven' step='any' value='" << ep.breakEven << "'><br>"
                 "<div style='color:#475569;font-size:0.78em;margin:8px 0;'>Effective overhead: " << (eo * 100) << "% &mdash; for auto TP/SL set price and leave TP/SL at 0</div>"
                 "<button>Save Changes</button></form>";
        }
        h << "<br><a class='btn' href='/entry-points'>Back</a>";
        res.set_content(html::wrap("Edit Entry", h.str()), "text/html");
    });

    // ========== POST /edit-entry ==========
    svr.Post("/edit-entry", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        int id = fi(f, "id");
        auto pts = db.loadEntryPoints();
        TradeDatabase::EntryPoint* found = nullptr;
        for (auto& ep : pts) if (ep.entryId == id) { found = &ep; break; }
        if (!found || found->traded) { res.set_redirect("/entry-points?err=Entry+not+found+or+already+traded", 303); return; }
        auto& ep = *found;
        double newPrice = fd(f, "entryPrice", ep.entryPrice);
        double newFunding = fd(f, "funding", ep.funding);
        double newQty = fd(f, "fundingQty");
        double newTP = fd(f, "exitTP");
        double newSL = fd(f, "exitSL");
        double newBE = fd(f, "breakEven");
        if (newPrice > 0) ep.entryPrice = newPrice;
        if (newFunding > 0) ep.funding = newFunding;
        if (newQty > 0) ep.fundingQty = newQty;
        else if (ep.entryPrice > 0) ep.fundingQty = QuantMath::fundedQty(ep.entryPrice, ep.funding);
        double eo = ep.effectiveOverhead;
        if (newTP > 0) ep.exitTakeProfit = newTP;
        else if (ep.entryPrice > 0) ep.exitTakeProfit = QuantMath::breakEven(ep.entryPrice, ep.isShort ? -eo : eo);
        if (newSL > 0) ep.exitStopLoss = newSL;
        else if (ep.entryPrice > 0) ep.exitStopLoss = QuantMath::levelSL(ep.entryPrice, eo, ep.isShort);
        if (newBE > 0) ep.breakEven = newBE;
        else if (ep.entryPrice > 0) ep.breakEven = QuantMath::breakEven(ep.entryPrice, eo);
        db.saveEntryPoints(pts);
        res.set_redirect("/entry-points?msg=Entry+" + std::to_string(id) + "+updated", 303);
    });

    // ========== POST /delete-entry ==========
    svr.Post("/delete-entry", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        int id = fi(f, "id");
        auto pts = db.loadEntryPoints();
        std::vector<TradeDatabase::EntryPoint> remaining;
        for (const auto& ep : pts) if (ep.entryId != id) remaining.push_back(ep);
        db.saveEntryPoints(remaining);
        res.set_redirect("/entry-points?msg=Entry+" + std::to_string(id) + "+deleted", 303);
    });
}
