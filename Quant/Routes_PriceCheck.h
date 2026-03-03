#pragma once

#include "AppContext.h"
#include "HtmlHelpers.h"
#include <mutex>
#include <algorithm>
#include <chrono>
#include <map>

inline void registerPriceCheckRoutes(httplib::Server& svr, AppContext& ctx)
{
    auto& db = ctx.defaultDb;
    auto& dbMutex = ctx.dbMutex;

    // ========== GET /price-check ==========
    svr.Get("/price-check", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        std::ostringstream h;
        h << std::fixed << std::setprecision(17);
        h << html::msgBanner(req) << html::errBanner(req);
        h << "<h1>Price Check (TP/SL vs Market)</h1>";
        auto trades = db.loadTrades();
        std::vector<std::string> symbols;
        for (const auto& t : trades)
            if (t.type == TradeType::Buy && std::find(symbols.begin(), symbols.end(), t.symbol) == symbols.end())
                symbols.push_back(t.symbol);
        if (symbols.empty()) { h << "<p class='empty'>(no Buy trades)</p>"; }
        else
        {
            h << "<form class='card' method='POST' action='/price-check'><h3>Enter current market prices</h3>";
            for (const auto& sym : symbols)
            {
                double last = ctx.prices.latest(sym);
                h << "<label>" << html::esc(sym) << "</label>"
                     "<input type='number' name='price_" << html::esc(sym) << "' step='any'";
                if (last > 0) h << " value='" << last << "'";
                h << " required><br>";
            }
            h << "<br><button>Check</button></form>";
        }
        res.set_content(html::wrap("Price Check", h.str()), "text/html");
    });

    // ========== POST /price-check ==========
    svr.Post("/price-check", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        auto trades = db.loadTrades();
        std::vector<std::string> symbols;
        for (const auto& t : trades)
            if (t.type == TradeType::Buy && std::find(symbols.begin(), symbols.end(), t.symbol) == symbols.end())
                symbols.push_back(t.symbol);
        auto priceFor = [&](const std::string& sym) -> double { return fd(f, "price_" + sym, 0.0); };

        // Persist entered prices into the shared PriceSeries
        {
            auto now = std::chrono::system_clock::now();
            long long ts = std::chrono::duration_cast<std::chrono::seconds>(
                now.time_since_epoch()).count();
            for (const auto& sym : symbols)
            {
                double p = priceFor(sym);
                if (p > 0) ctx.prices.set(sym, ts, p);
            }
        }
        std::ostringstream h;
        h << std::fixed << std::setprecision(17);
        h << "<h1>Price Check Results</h1>";
        h << "<form class='card' method='POST' action='/price-check'><h3>Update prices</h3>";
        for (const auto& sym : symbols)
        { double p = priceFor(sym); h << "<label>" << html::esc(sym) << "</label><input type='number' name='price_" << html::esc(sym) << "' step='any' value='" << p << "' required><br>"; }
        h << "<br><button>Check</button></form>";
        h << "<table><tr><th>ID</th><th>Symbol</th><th>Entry</th><th>Qty</th><th>Market</th><th>Gross P&amp;L</th><th>Net P&amp;L</th><th>ROI%</th>"
             "<th>TP Price</th><th>TP?</th><th>SL Price</th><th>SL?</th></tr>";
        struct Trigger { int id; std::string sym; double price; double qty; std::string tag; double entryPrice; double buyFee; double totalQty; int levelIndex; };
        std::vector<Trigger> triggers;
        for (const auto& t : trades)
        {
            if (t.type != TradeType::Buy) continue;
            double remaining = t.quantity - db.soldQuantityForParent(t.tradeId);
            if (remaining <= 0) continue;
            double cur = priceFor(t.symbol);
            if (cur <= 0) continue;
            double remainFrac = (t.quantity > 0) ? remaining / t.quantity : 0.0;
            auto pnl = QuantMath::computeProfit(t.value, cur, remaining,
                t.buyFee * remainFrac, 0.0);
            double gross = pnl.gross;
            double net   = pnl.net;
            double roi   = pnl.roiPct;
            h << "<tr><td>" << t.tradeId << "</td><td>" << html::esc(t.symbol) << "</td><td>" << t.value << "</td><td>" << remaining << "</td>"
              << "<td>" << cur << "</td><td class='" << (gross >= 0 ? "buy" : "sell") << "'>" << gross << "</td>"
              << "<td class='" << (net >= 0 ? "buy" : "sell") << "'>" << net << "</td>"
              << "<td class='" << (roi >= 0 ? "buy" : "sell") << "'>" << roi << "</td>"
              << "<td>-</td><td>-</td>"
              << "<td>-</td><td>-</td></tr>";
            auto levels = db.loadHorizonLevels(t.symbol, t.tradeId);
            for (const auto& lv : levels)
            {
                double htp = t.quantity > 0 ? lv.takeProfit / t.quantity : 0;
                double hsl = (t.quantity > 0 && lv.stopLoss > 0) ? lv.stopLoss / t.quantity : 0;
                bool htpHit = (htp > 0 && cur >= htp);
                bool hslHit = (lv.stopLossActive && hsl > 0 && cur <= hsl);
                h << "<tr style='color:#64748b;'><td></td><td>[" << lv.index << "]</td><td></td><td></td><td></td><td></td><td></td><td></td>"
                  << "<td>" << htp << "</td><td class='" << (htpHit ? "buy" : "off") << "'>" << (htpHit ? "HIT" : (htp > 0 ? std::to_string(htp - cur) + " away" : "-")) << "</td>"
                  << "<td>" << hsl << "</td><td class='" << (hslHit ? "sell" : "off") << "'>";
                if (hsl > 0 && lv.stopLossActive) h << (hslHit ? "BREACHED" : "OK");
                else if (hsl > 0) h << "OFF";
                else h << "-";
                h << "</td></tr>";
            }
            // Exit-point triggers: check saved exit points for this trade
            auto exitPts = db.loadExitPointsForTrade(t.tradeId);
            for (const auto& xp : exitPts)
            {
                if (xp.executed) continue;
                bool xpTpHit = (xp.tpPrice > 0 && cur >= xp.tpPrice);
                bool xpSlHit = (xp.slActive && xp.slPrice > 0 && cur <= xp.slPrice);
                double xpTpAway = xp.tpPrice - cur;
                h << "<tr style='color:#94a3b8;'><td></td><td>X[" << xp.levelIndex << "]</td>"
                  << "<td></td><td>" << xp.sellQty << "</td><td></td><td></td><td></td><td></td>"
                  << "<td>" << xp.tpPrice << "</td>"
                  << "<td class='" << (xpTpHit ? "buy" : "off") << "'>"
                  << (xpTpHit ? "HIT" : (xp.tpPrice > 0 ? std::to_string(xpTpAway) + " away" : "-")) << "</td>"
                  << "<td>" << xp.slPrice << "</td>"
                  << "<td class='" << (xpSlHit ? "sell" : "off") << "'>";
                if (xp.slPrice > 0 && xp.slActive) h << (xpSlHit ? "BREACHED" : "OK");
                else if (xp.slPrice > 0) h << "OFF";
                else h << "-";
                h << "</td></tr>";
                if (xpTpHit && xp.sellQty > 0)
                    triggers.push_back({t.tradeId, t.symbol, xp.tpPrice, xp.sellQty, "X-TP", t.value, t.buyFee, t.quantity, xp.levelIndex});
                if (xpSlHit && xp.sellQty > 0)
                    triggers.push_back({t.tradeId, t.symbol, xp.slPrice, xp.sellQty, "X-SL", t.value, t.buyFee, t.quantity, xp.levelIndex});
            }
        }
        h << "</table>";

        auto pending = db.loadPendingExits();
        if (!pending.empty())
        {
            std::vector<TradeDatabase::PendingExit> peTriggered, peWaiting;
            for (const auto& pe : pending)
            { double cur = priceFor(pe.symbol); if (cur > 0 && cur >= pe.triggerPrice) peTriggered.push_back(pe); else peWaiting.push_back(pe); }

            h << "<h2>Pending Exits (" << pending.size() << " total, " << peTriggered.size() << " triggered)</h2>";

            if (!peTriggered.empty())
            {
                h << "<form class='card' method='POST' action='/execute-pending-exits'>";
                h << "<table><tr><th>&#10003;</th><th>Order</th><th>Symbol</th><th>Trade</th><th>Trigger</th><th>Market</th><th>Qty</th><th>Sell Fee</th><th>Status</th></tr>";
                for (const auto& pe : peTriggered)
                {
                    h << "<tr><td><input type='checkbox' name='pe_" << pe.orderId << "' value='1' checked></td>"
                      << "<td>" << pe.orderId << "</td><td>" << html::esc(pe.symbol) << "</td><td>" << pe.tradeId << "</td>"
                      << "<td>" << pe.triggerPrice << "</td><td>" << priceFor(pe.symbol) << "</td><td>" << pe.sellQty << "</td>"
                      << "<td><input type='number' name='pefee_" << pe.orderId << "' step='any' value='0' style='width:80px;'></td>"
                      << "<td class='buy'>TRIGGERED</td></tr>";
                }
                for (const auto& sym : symbols)
                    h << "<input type='hidden' name='price_" << html::esc(sym) << "' value='" << priceFor(sym) << "'>";
                h << "</table><button>Execute Selected Pending Exits</button></form>";
            }

            if (!peWaiting.empty())
            {
                h << "<table><tr><th>Order</th><th>Symbol</th><th>Trade</th><th>Trigger</th><th>Market</th><th>Qty</th><th>Away</th><th>Status</th></tr>";
                for (const auto& pe : peWaiting)
                {
                    double cur = priceFor(pe.symbol);
                    double away = pe.triggerPrice - (cur > 0 ? cur : 0);
                    h << "<tr style='color:#64748b;'><td>" << pe.orderId << "</td><td>" << html::esc(pe.symbol) << "</td><td>" << pe.tradeId << "</td>"
                      << "<td>" << pe.triggerPrice << "</td><td>" << (cur > 0 ? std::to_string(cur) : "-") << "</td><td>" << pe.sellQty << "</td>"
                      << "<td>" << away << "</td><td>WAITING</td></tr>";
                }
                h << "</table>";
            }
        }
        if (!triggers.empty())
        {
            h << "<h2>TP/SL Triggers (" << triggers.size() << ")</h2>"
                 "<form class='card' method='POST' action='/execute-triggered-sells'>";
            h << "<table><tr><th>&#10003;</th><th>Tag</th><th>ID</th><th>Symbol</th><th>Lvl</th><th>Sell Qty</th><th>Sell Price</th><th>Proceeds</th><th>P&amp;L</th><th>Sell Fee</th></tr>";
            for (size_t i = 0; i < triggers.size(); ++i)
            {
                const auto& tr = triggers[i];
                double proceeds = tr.price * tr.qty;
                double pnlAmt = (tr.price - tr.entryPrice) * tr.qty;
                bool isTP = (tr.tag == "TP" || tr.tag == "H-TP" || tr.tag == "X-TP");
                h << "<tr><td><input type='checkbox' name='trig_" << i << "' value='1' checked></td>"
                  << "<td class='" << (isTP ? "buy" : "sell") << "'>" << tr.tag << "</td>"
                  << "<td>" << tr.id << "</td><td>" << html::esc(tr.sym) << "</td>"
                  << "<td>" << (tr.levelIndex >= 0 ? std::to_string(tr.levelIndex) : "-") << "</td>"
                  << "<td>" << tr.qty << "</td>"
                  << "<td>" << tr.price << "</td>"
                  << "<td>" << proceeds << "</td>"
                  << "<td class='" << (pnlAmt >= 0 ? "buy" : "sell") << "'>"
                  << (pnlAmt >= 0 ? "+" : "") << pnlAmt << "</td>"
                  << "<td><input type='number' name='trigfee_" << i << "' step='any' value='0' style='width:80px;'></td></tr>";
                h << "<input type='hidden' name='trigid_" << i << "' value='" << tr.id << "'>"
                  << "<input type='hidden' name='trigsym_" << i << "' value='" << html::esc(tr.sym) << "'>"
                  << "<input type='hidden' name='trigprice_" << i << "' value='" << tr.price << "'>"
                  << "<input type='hidden' name='trigqty_" << i << "' value='" << tr.qty << "'>"
                  << "<input type='hidden' name='trigtag_" << i << "' value='" << tr.tag << "'>";
            }
            h << "<input type='hidden' name='trigcount' value='" << triggers.size() << "'>";
            for (const auto& sym : symbols)
                h << "<input type='hidden' name='price_" << html::esc(sym) << "' value='" << priceFor(sym) << "'>";
            h << "</table><button>Execute Selected Sells</button></form>";
        }
        {
            auto entryPts = db.loadEntryPoints();
            auto chainState = db.loadChainState();
            auto chainMembers = db.loadChainMembers();
            // Build entryId -> cycle map for chain-aware filtering
            std::map<int, int> entryToCycle;
            for (const auto& cm : chainMembers)
                entryToCycle[cm.entryId] = cm.cycle;

            struct TriggeredEP { int entryId; std::string symbol; double entryPrice; double fundingQty; double exitTP; double exitSL; bool isShort; double marketPrice; };
            std::vector<TriggeredEP> hitEntries;
            struct QueuedEP { int entryId; std::string symbol; double entryPrice; double fundingQty; int cycle; };
            std::vector<QueuedEP> queuedEntries;
            struct WaitingEP { int entryId; std::string symbol; double entryPrice; double fundingQty; double away; int cycle; };
            std::vector<WaitingEP> waitingEntries;
            for (const auto& ep : entryPts)
            {
                if (ep.traded) continue;
                double cur = priceFor(ep.symbol);
                bool hit = false;
                if (cur > 0) hit = ep.isShort ? (cur >= ep.entryPrice) : (cur <= ep.entryPrice);
                auto it = entryToCycle.find(ep.entryId);
                int cycle = (it != entryToCycle.end()) ? it->second : -1;
                if (hit)
                {
                    if (cycle >= 0 && chainState.active && cycle > chainState.currentCycle)
                        queuedEntries.push_back({ep.entryId, ep.symbol, ep.entryPrice, ep.fundingQty, cycle});
                    else
                        hitEntries.push_back({ep.entryId, ep.symbol, ep.entryPrice, ep.fundingQty, ep.exitTakeProfit, ep.exitStopLoss, ep.isShort, cur});
                }
                else
                {
                    double away = ep.entryPrice - (cur > 0 ? cur : 0);
                    waitingEntries.push_back({ep.entryId, ep.symbol, ep.entryPrice, ep.fundingQty, away, cycle});
                }
            }

            int totalEP = (int)hitEntries.size() + (int)waitingEntries.size() + (int)queuedEntries.size();
            if (totalEP > 0)
                h << "<h2>Entry Points (" << totalEP << " total, " << hitEntries.size() << " triggered)</h2>";

            if (!hitEntries.empty())
            {
                h << "<form class='card' method='POST' action='/execute-triggered-entries'>"
                     "<table><tr><th>ID</th><th>Symbol</th><th>Entry</th><th>Market</th><th>Qty</th><th>Cost</th><th>TP</th><th>SL</th><th>Buy Fee</th><th>Status</th></tr>";
                for (const auto& te : hitEntries)
                {
                    double cost = te.entryPrice * te.fundingQty;
                    auto cit = entryToCycle.find(te.entryId);
                    std::string cycleTag = (cit != entryToCycle.end()) ? " <span style='color:#c9a44a;'>[C" + std::to_string(cit->second) + "]</span>" : "";
                    h << "<tr><td>" << te.entryId << cycleTag << "</td><td>" << html::esc(te.symbol) << "</td><td>" << te.entryPrice << "</td>"
                      << "<td class='buy'>" << te.marketPrice << "</td><td>" << te.fundingQty << "</td><td>" << cost << "</td>"
                      << "<td>" << te.exitTP << "</td><td>" << te.exitSL << "</td>"
                      << "<td><input type='number' name='fee_" << te.entryId << "' step='any' value='0' style='width:80px;'>"
                      << "<input type='hidden' name='epid_" << te.entryId << "' value='1'></td>"
                      << "<td class='buy'>TRIGGERED</td></tr>";
                }
                h << "</table><button>Execute Triggered Entries</button></form>";
            }

            if (!waitingEntries.empty())
            {
                h << "<table><tr><th>ID</th><th>Symbol</th><th>Entry</th><th>Qty</th><th>Away</th><th>Cycle</th><th>Status</th></tr>";
                for (const auto& we : waitingEntries)
                {
                    std::string cycleTag = (we.cycle >= 0) ? "C" + std::to_string(we.cycle) : "-";
                    h << "<tr style='color:#64748b;'><td>" << we.entryId << "</td><td>" << html::esc(we.symbol) << "</td>"
                      << "<td>" << we.entryPrice << "</td><td>" << we.fundingQty << "</td>"
                      << "<td>" << we.away << "</td>"
                      << "<td>" << cycleTag << "</td>"
                      << "<td>WAITING</td></tr>";
                }
                h << "</table>";
            }

            if (!queuedEntries.empty())
            {
                h << "<p style='color:#64748b;font-size:0.82em;'>Queued entries belong to future chain cycles. "
                     "Complete cycle " << chainState.currentCycle << " first.</p>"
                     "<table><tr><th>ID</th><th>Symbol</th><th>Entry</th><th>Qty</th><th>Cycle</th><th>Status</th></tr>";
                for (const auto& qe : queuedEntries)
                {
                    h << "<tr style='color:#64748b;'><td>" << qe.entryId << "</td><td>" << html::esc(qe.symbol) << "</td>"
                      << "<td>" << qe.entryPrice << "</td><td>" << qe.fundingQty << "</td>"
                      << "<td><span style='color:#c9a44a;'>C" << qe.cycle << "</span></td>"
                      << "<td>QUEUED</td></tr>";
                }
                h << "</table>";
            }

            // Chain cycle status & advance
            if (chainState.active)
            {
                bool anyTraded = false;
                bool allSold = true;
                double cyclePnl = 0;
                auto allTrades = db.loadTrades();

                for (const auto& m : chainMembers)
                {
                    if (m.cycle != chainState.currentCycle) continue;
                    for (const auto& e : entryPts)
                    {
                        if (e.entryId != m.entryId) continue;
                        if (!e.traded || e.linkedTradeId < 0) continue;
                        anyTraded = true;
                        for (const auto& t : allTrades)
                        {
                            if (t.tradeId == e.linkedTradeId && t.type == TradeType::Buy)
                            {
                                double sq = db.soldQuantityForParent(t.tradeId);
                                if (sq < t.quantity - 1e-9) allSold = false;
                                for (const auto& s : allTrades)
                                    if (s.type == TradeType::CoveredSell && s.parentTradeId == t.tradeId)
                                        cyclePnl += QuantMath::netProfit(
                                            QuantMath::grossProfit(t.value, s.value, s.quantity),
                                            0.0, s.sellFee);
                                break;
                            }
                        }
                        break;
                    }
                }

                bool cycleComplete = anyTraded && allSold;
                h << "<h2>Chain Status</h2><div class='row'>"
                     "<div class='stat'><div class='lbl'>Symbol</div><div class='val'>" << html::esc(chainState.symbol) << "</div></div>"
                     "<div class='stat'><div class='lbl'>Cycle</div><div class='val'>" << chainState.currentCycle << "</div></div>"
                     "<div class='stat'><div class='lbl'>Cycle P&amp;L</div><div class='val " << (cyclePnl >= 0 ? "buy" : "sell") << "'>" << cyclePnl << "</div></div>"
                     "<div class='stat'><div class='lbl'>Savings</div><div class='val'>" << chainState.totalSavings << "</div></div>"
                     "<div class='stat'><div class='lbl'>Status</div><div class='val " << (cycleComplete ? "buy" : "sell") << "'>"
                  << (cycleComplete ? "COMPLETE" : (anyTraded ? "OPEN" : "WAITING")) << "</div></div>"
                     "</div>";

                if (cycleComplete)
                {
                    // Determine market price for the chain symbol
                    double chainPrice = priceFor(chainState.symbol);
                    // Pre-fill from param model if available
                    int advLevels = 4; double advRisk = 0.5; double advSteep = 6.0;
                    double advHedge = 1.0; int advSyms = 1; double advSpread = 0;
                    double advDt = 1.0; double advSurplus = 0.02;
                    double advMaxR = 0; double advMinR = 0; bool advSL = false;
                    auto advModels = db.loadParamModels();
                    if (!advModels.empty())
                    {
                        const auto& am = advModels[0];
                        advLevels = am.levels; advRisk = am.risk; advSteep = am.steepness;
                        advHedge = am.feeHedgingCoefficient; advSyms = am.symbolCount;
                        advSpread = am.feeSpread; advDt = am.deltaTime;
                        advSurplus = am.surplusRate; advMaxR = am.maxRisk; advMinR = am.minRisk;
                        advSL = am.generateStopLosses;
                    }
                    h << "<h3 style='color:#c9a44a;'>Advance to Cycle " << (chainState.currentCycle + 1) << "</h3>"
                         "<form class='card' method='POST' action='/execute-chain-advance'>"
                         "<label>Current Price</label><input type='number' name='currentPrice' step='any' value='" << chainPrice << "' required><br>"
                         "<label>Quantity</label><input type='number' name='quantity' step='any' value='1'><br>"
                         "<label>Levels</label><input type='number' name='levels' value='" << advLevels << "'><br>"
                         "<label>Risk</label><input type='number' name='risk' step='any' value='" << advRisk << "'><br>"
                         "<label>Steepness</label><input type='number' name='steepness' step='any' value='" << advSteep << "'><br>"
                         "<label>Fee Hedging</label><input type='number' name='feeHedgingCoefficient' step='any' value='" << advHedge << "'><br>"
                         "<label>Symbol Count</label><input type='number' name='symbolCount' value='" << advSyms << "'><br>"
                         "<label>Fee Spread</label><input type='number' name='feeSpread' step='any' value='" << advSpread << "'><br>"
                         "<label>Delta Time</label><input type='number' name='deltaTime' step='any' value='" << advDt << "'><br>"
                         "<label>Surplus Rate</label><input type='number' name='surplusRate' step='any' value='" << advSurplus << "'><br>"
                         "<label>Max Risk</label><input type='number' name='maxRisk' step='any' value='" << advMaxR << "'><br>"
                         "<label>Min Risk</label><input type='number' name='minRisk' step='any' value='" << advMinR << "'><br>"
                         "<label>Generate SL</label><input type='checkbox' name='generateStopLosses' value='1'" << (advSL ? " checked" : "") << "><br>"
                         "<br><button>Advance Chain</button></form>";
                }
            }
        }
        h << "<br><a class='btn' href='/price-check'>Back</a> <a class='btn' href='/trades'>Trades</a>";
        res.set_content(html::wrap("Price Check Results", h.str()), "text/html");
    });

    // ========== POST /execute-triggered-entries ==========
    svr.Post("/execute-triggered-entries", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        auto entryPts = db.loadEntryPoints();
        std::ostringstream h;
        h << std::fixed << std::setprecision(17);
        h << "<h1>Entry Execution</h1>";
        int executed = 0, failed = 0;
        h << "<table><tr><th>ID</th><th>Symbol</th><th>Entry</th><th>Qty</th><th>Fee</th><th>Trade</th><th>TP</th><th>SL</th><th>Status</th></tr>";
        for (auto& ep : entryPts)
        {
            if (ep.traded) continue;
            if (fv(f, "epid_" + std::to_string(ep.entryId)).empty()) continue;
            double buyFee = fd(f, "fee_" + std::to_string(ep.entryId));
            double cost = ep.entryPrice * ep.fundingQty + buyFee;
            double walBal = db.loadWalletBalance();
            if (cost > walBal)
            {
                h << "<tr><td>" << ep.entryId << "</td><td>" << html::esc(ep.symbol) << "</td><td>" << ep.entryPrice << "</td>"
                  << "<td>" << ep.fundingQty << "</td><td>" << buyFee << "</td><td>-</td><td>-</td><td>-</td>"
                  << "<td class='sell'>INSUFFICIENT (need " << cost << ", have " << walBal << ")</td></tr>";
                ++failed; continue;
            }
            int bid = db.executeBuy(ep.symbol, ep.entryPrice, ep.fundingQty, buyFee);
            ep.traded = true; ep.linkedTradeId = bid;
            h << "<tr><td>" << ep.entryId << "</td><td>" << html::esc(ep.symbol) << "</td><td>" << ep.entryPrice << "</td>"
              << "<td>" << ep.fundingQty << "</td><td>" << buyFee << "</td><td class='buy'>#" << bid << "</td>"
              << "<td>" << ep.exitTakeProfit << "</td><td>" << ep.exitStopLoss << "</td><td class='buy'>OK</td></tr>";
            ++executed;
        }
        h << "</table>";
        if (executed > 0)
        {
            auto trades = db.loadTrades();
            auto existingExits = db.loadExitPoints();
            for (const auto& ep : entryPts)
            {
                if (ep.linkedTradeId < 0) continue;
                auto* tradePtr = db.findTradeById(trades, ep.linkedTradeId);
                if (!tradePtr) continue;
                // Create exit point for TP
                if (ep.exitTakeProfit > 0)
                {
                    TradeDatabase::ExitPoint xp;
                    xp.exitId       = db.nextExitId();
                    xp.tradeId      = tradePtr->tradeId;
                    xp.symbol       = ep.symbol;
                    xp.levelIndex   = ep.levelIndex;
                    xp.tpPrice      = ep.exitTakeProfit;
                    xp.sellQty      = ep.fundingQty;
                    xp.sellFraction = 1.0;
                    xp.slPrice      = ep.exitStopLoss;
                    xp.slActive     = (ep.stopLossFraction > 0.0);
                    xp.executed     = false;
                    xp.linkedSellId = -1;
                    existingExits.push_back(xp);
                }
                // Set trade-level TP to first level (horizon compat)
                tradePtr->takeProfit = ep.exitTakeProfit * tradePtr->quantity;
                tradePtr->takeProfitFraction = (ep.exitTakeProfit > 0) ? 1.0 : 0.0;
                tradePtr->takeProfitActive = (tradePtr->takeProfitFraction > 0.0);
                tradePtr->stopLoss = ep.exitStopLoss * tradePtr->quantity;
                tradePtr->stopLossFraction = ep.stopLossFraction;
                tradePtr->stopLossActive = (ep.stopLossFraction > 0.0);
                db.updateTrade(*tradePtr);
            }
            db.saveExitPoints(existingExits);
        }
        db.saveEntryPoints(entryPts);
        h << "<div class='row'>"
             "<div class='stat'><div class='lbl'>Executed</div><div class='val'>" << executed << "</div></div>"
             "<div class='stat'><div class='lbl'>Failed</div><div class='val'>" << failed << "</div></div>"
             "<div class='stat'><div class='lbl'>Wallet</div><div class='val'>" << db.loadWalletBalance() << "</div></div></div>";
        h << "<br><a class='btn' href='/trades'>Trades</a> <a class='btn' href='/entry-points'>Entry Points</a> <a class='btn' href='/price-check'>Price Check</a>";
        res.set_content(html::wrap("Entry Execution", h.str()), "text/html");
    });

    // ========== POST /execute-triggered-sells ==========
    svr.Post("/execute-triggered-sells", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        int count = fi(f, "trigcount");
        std::ostringstream h;
        h << std::fixed << std::setprecision(17);
        h << "<h1>TP/SL Sell Execution</h1>";
        int executed = 0, failed = 0;
        h << "<table><tr><th>Tag</th><th>Trade</th><th>Symbol</th><th>Qty</th><th>Price</th><th>Fee</th><th>Sell ID</th><th>Status</th></tr>";
        struct ExecutedTrig { int tradeId; double price; double qty; std::string tag; int sellId; };
        std::vector<ExecutedTrig> executedTrigs;
        for (int i = 0; i < count; ++i)
        {
            if (fv(f, "trig_" + std::to_string(i)).empty()) continue;
            int tradeId = fi(f, "trigid_" + std::to_string(i));
            std::string sym = fv(f, "trigsym_" + std::to_string(i));
            double price = fd(f, "trigprice_" + std::to_string(i));
            double qty = fd(f, "trigqty_" + std::to_string(i));
            double fee = fd(f, "trigfee_" + std::to_string(i));
            std::string tag = fv(f, "trigtag_" + std::to_string(i));
            bool isTP = (tag == "TP" || tag == "H-TP" || tag == "X-TP");
            int sid = db.executeSellForTrade(sym, price, qty, fee, tradeId);
            if (sid >= 0)
            {
                h << "<tr><td class='" << (isTP ? "buy" : "sell") << "'>" << tag << "</td>"
                  << "<td>" << tradeId << "</td><td>" << html::esc(sym) << "</td>"
                  << "<td>" << qty << "</td><td>" << price << "</td><td>" << fee << "</td>"
                  << "<td class='buy'>#" << sid << "</td><td class='buy'>OK</td></tr>";
                executedTrigs.push_back({tradeId, price, qty, tag, sid});
                ++executed;
            }
            else
            {
                h << "<tr><td class='" << (isTP ? "buy" : "sell") << "'>" << tag << "</td>"
                  << "<td>" << tradeId << "</td><td>" << html::esc(sym) << "</td>"
                  << "<td>" << qty << "</td><td>" << price << "</td><td>" << fee << "</td>"
                  << "<td>-</td><td class='sell'>FAILED</td></tr>";
                ++failed;
            }
        }
        h << "</table>";
        // Mark exit points as executed
        if (!executedTrigs.empty())
        {
            auto allExits = db.loadExitPoints();
            bool changed = false;
            for (const auto& et : executedTrigs)
            {
                if (et.tag != "X-TP" && et.tag != "X-SL") continue;
                for (auto& xp : allExits)
                {
                    if (xp.executed) continue;
                    if (xp.tradeId != et.tradeId) continue;
                    bool match = (et.tag == "X-TP" && std::abs(xp.tpPrice - et.price) < 1e-9) ||
                                 (et.tag == "X-SL" && std::abs(xp.slPrice - et.price) < 1e-9);
                    if (match && std::abs(xp.sellQty - et.qty) < 1e-9)
                    {
                        xp.executed = true;
                        xp.linkedSellId = et.sellId;
                        changed = true;
                        break;
                    }
                }
            }
            if (changed) db.saveExitPoints(allExits);
        }
        h << "<div class='row'>"
             "<div class='stat'><div class='lbl'>Executed</div><div class='val'>" << executed << "</div></div>"
             "<div class='stat'><div class='lbl'>Failed</div><div class='val'>" << failed << "</div></div>"
             "<div class='stat'><div class='lbl'>Wallet</div><div class='val'>" << db.loadWalletBalance() << "</div></div></div>";
        h << "<br><a class='btn' href='/price-check'>Price Check</a> <a class='btn' href='/trades'>Trades</a>";
        res.set_content(html::wrap("TP/SL Execution", h.str()), "text/html");
    });

    // ========== POST /execute-pending-exits ==========
    svr.Post("/execute-pending-exits", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        auto allPending = db.loadPendingExits();
        std::ostringstream h;
        h << std::fixed << std::setprecision(17);
        h << "<h1>Pending Exit Execution</h1>";
        int executed = 0, failed = 0;
        h << "<table><tr><th>Order</th><th>Trade</th><th>Symbol</th><th>Qty</th><th>Trigger</th><th>Fee</th><th>Sell ID</th><th>Status</th></tr>";
        std::vector<int> executedOrderIds;
        for (const auto& pe : allPending)
        {
            if (fv(f, "pe_" + std::to_string(pe.orderId)).empty()) continue;
            double price = fd(f, "price_" + pe.symbol, pe.triggerPrice);
            double fee = fd(f, "pefee_" + std::to_string(pe.orderId));
            int sid = db.executeSellForTrade(pe.symbol, price, pe.sellQty, fee, pe.tradeId);
            if (sid >= 0)
            {
                h << "<tr><td>" << pe.orderId << "</td><td>" << pe.tradeId << "</td>"
                  << "<td>" << html::esc(pe.symbol) << "</td><td>" << pe.sellQty << "</td>"
                  << "<td>" << pe.triggerPrice << "</td><td>" << fee << "</td>"
                  << "<td class='buy'>#" << sid << "</td><td class='buy'>OK</td></tr>";
                executedOrderIds.push_back(pe.orderId);
                ++executed;
            }
            else
            {
                h << "<tr><td>" << pe.orderId << "</td><td>" << pe.tradeId << "</td>"
                  << "<td>" << html::esc(pe.symbol) << "</td><td>" << pe.sellQty << "</td>"
                  << "<td>" << pe.triggerPrice << "</td><td>" << fee << "</td>"
                  << "<td>-</td><td class='sell'>FAILED</td></tr>";
                ++failed;
            }
        }
        h << "</table>";
        for (int oid : executedOrderIds)
            db.removePendingExit(oid);
        h << "<div class='row'>"
             "<div class='stat'><div class='lbl'>Executed</div><div class='val'>" << executed << "</div></div>"
             "<div class='stat'><div class='lbl'>Failed</div><div class='val'>" << failed << "</div></div>"
             "<div class='stat'><div class='lbl'>Wallet</div><div class='val'>" << db.loadWalletBalance() << "</div></div></div>";
        h << "<br><a class='btn' href='/price-check'>Price Check</a> <a class='btn' href='/trades'>Trades</a>";
        res.set_content(html::wrap("Pending Exit Execution", h.str()), "text/html");
    });

    // ========== POST /execute-chain-advance ==========
    svr.Post("/execute-chain-advance", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        auto state = db.loadChainState();
        if (!state.active)
        {
            res.set_redirect("/price-check?err=No+active+chain", 303);
            return;
        }

        // Verify cycle completion
        auto members = db.loadChainMembers();
        auto allEntries = db.loadEntryPoints();
        auto allTrades = db.loadTrades();

        bool anyTraded = false;
        bool allSold = true;
        double cyclePnl = 0;

        for (const auto& m : members)
        {
            if (m.cycle != state.currentCycle) continue;
            for (const auto& e : allEntries)
            {
                if (e.entryId != m.entryId) continue;
                if (!e.traded || e.linkedTradeId < 0) continue;
                anyTraded = true;
                for (const auto& t : allTrades)
                {
                    if (t.tradeId == e.linkedTradeId && t.type == TradeType::Buy)
                    {
                        double sq = db.soldQuantityForParent(t.tradeId);
                        if (sq < t.quantity - 1e-9) allSold = false;
                        for (const auto& s : allTrades)
                            if (s.type == TradeType::CoveredSell && s.parentTradeId == t.tradeId)
                                cyclePnl += QuantMath::netProfit(
                                    QuantMath::grossProfit(t.value, s.value, s.quantity),
                                    0.0, s.sellFee);
                        break;
                    }
                }
                break;
            }
        }

        if (!anyTraded || !allSold)
        {
            res.set_redirect("/price-check?err=Cycle+not+complete", 303);
            return;
        }

        // Divert savings
        double savingsDiv = (cyclePnl > 0 && state.savingsRate > 0) ? cyclePnl * state.savingsRate : 0;
        if (savingsDiv > 0)
        {
            db.withdraw(savingsDiv);
            state.totalSavings += savingsDiv;
        }

        double currentPrice = fd(f, "currentPrice");
        if (currentPrice <= 0)
        {
            res.set_redirect("/price-check?err=Current+price+required", 303);
            return;
        }

        bool genSL = (fv(f, "generateStopLosses") == "1");

        QuantMath::SerialParams sp;
        sp.currentPrice          = currentPrice;
        sp.quantity              = fd(f, "quantity", 1.0);
        sp.levels                = fi(f, "levels", 4);
        sp.steepness             = fd(f, "steepness", 6.0);
        sp.risk                  = fd(f, "risk", 0.5);
        sp.availableFunds        = db.loadWalletBalance();
        sp.feeSpread             = fd(f, "feeSpread");
        sp.feeHedgingCoefficient = fd(f, "feeHedgingCoefficient", 1.0);
        sp.deltaTime             = fd(f, "deltaTime", 1.0);
        sp.symbolCount           = fi(f, "symbolCount", 1);
        sp.coefficientK          = fd(f, "coefficientK");
        sp.surplusRate           = fd(f, "surplusRate");
        sp.maxRisk               = fd(f, "maxRisk");
        sp.minRisk               = fd(f, "minRisk");
        sp.generateStopLosses    = genSL;

        auto plan = QuantMath::generateSerialPlan(sp);

        int nextCycle = state.currentCycle + 1;
        std::vector<int> newEntryIds;
        auto existingEps = db.loadEntryPoints();

        for (const auto& e : plan.entries)
        {
            if (e.funding <= 0) continue;
            TradeDatabase::EntryPoint ep;
            ep.symbol = state.symbol;
            ep.entryId = db.nextEntryId();
            ep.levelIndex = e.index;
            ep.entryPrice = e.entryPrice;
            ep.breakEven = e.breakEven;
            ep.funding = e.funding;
            ep.fundingQty = e.fundQty;
            ep.effectiveOverhead = plan.effectiveOH;
            ep.exitTakeProfit = e.tpUnit;
            ep.exitStopLoss = genSL ? e.slUnit : 0;
            ep.stopLossFraction = genSL ? plan.slFraction : 0;
            ep.traded = false;
            ep.linkedTradeId = -1;
            existingEps.push_back(ep);
            newEntryIds.push_back(ep.entryId);
        }

        db.saveEntryPoints(existingEps);
        db.addChainMembers(nextCycle, newEntryIds);
        state.currentCycle = nextCycle;
        db.saveChainState(state);

        std::ostringstream h;
        h << std::fixed << std::setprecision(17);
        h << "<h1>Chain Advanced to Cycle " << nextCycle << "</h1>";
        h << "<div class='row'>"
             "<div class='stat'><div class='lbl'>Cycle</div><div class='val'>" << nextCycle << "</div></div>"
             "<div class='stat'><div class='lbl'>New Entries</div><div class='val'>" << newEntryIds.size() << "</div></div>"
             "<div class='stat'><div class='lbl'>Savings Diverted</div><div class='val'>" << savingsDiv << "</div></div>"
             "<div class='stat'><div class='lbl'>Total Savings</div><div class='val'>" << state.totalSavings << "</div></div>"
             "<div class='stat'><div class='lbl'>Wallet</div><div class='val'>" << db.loadWalletBalance() << "</div></div></div>";

        if (!newEntryIds.empty())
        {
            h << "<h2>New Entry Points</h2>"
                 "<table><tr><th>ID</th><th>Lvl</th><th>Entry</th><th>Break Even</th><th>Qty</th><th>Funding</th><th>TP</th><th>SL</th></tr>";
            for (const auto& e : plan.entries)
            {
                if (e.funding <= 0) continue;
                h << "<tr><td>" << e.index << "</td><td>" << e.index << "</td>"
                  << "<td>" << e.entryPrice << "</td><td>" << e.breakEven << "</td>"
                  << "<td>" << e.fundQty << "</td><td>" << e.funding << "</td>"
                  << "<td>" << e.tpUnit << "</td><td>" << (genSL ? e.slUnit : 0) << "</td></tr>";
            }
            h << "</table>";
        }

        h << "<br><a class='btn' href='/price-check'>Price Check</a> <a class='btn' href='/entry-points'>Entry Points</a> <a class='btn' href='/trades'>Trades</a>";
        res.set_content(html::wrap("Chain Advance", h.str()), "text/html");
    });
}
