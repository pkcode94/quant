#pragma once

#include "AppContext.h"
#include "HtmlHelpers.h"
#include "ProfitCalculator.h"
#include <mutex>
#include <algorithm>

inline void registerCoreRoutes(httplib::Server& svr, AppContext& ctx)
{
    auto& db = ctx.defaultDb;
    auto& dbMutex = ctx.dbMutex;

    // ========== GET / — Dashboard ==========
    svr.Get("/", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        std::ostringstream h;
        h << std::fixed << std::setprecision(17);
        h << html::msgBanner(req) << html::errBanner(req);
        h << "<h1>Quant Trade Manager</h1>";

        double wal = db.loadWalletBalance();
        double dep = db.deployedCapital();
        auto trades = db.loadTrades();
        int buys = 0, sells = 0;
        double totalRealized = 0;
        for (const auto& t : trades)
        {
            if (t.type == TradeType::Buy) ++buys;
            else
            {
                ++sells;
                if (t.parentTradeId >= 0)
                {
                    for (const auto& p : trades)
                        if (p.tradeId == t.parentTradeId)
                        {
                            auto cp = ProfitCalculator::childProfit(t, p.value);
                            totalRealized += cp.netProfit;
                            break;
                        }
                }
            }
        }

        h << "<div class='row'>"
             "<div class='stat'><div class='lbl'>Liquid</div><div class='val'>" << wal << "</div></div>"
             "<div class='stat'><div class='lbl'>Deployed</div><div class='val'>" << dep << "</div></div>"
             "<div class='stat'><div class='lbl'>Total</div><div class='val'>" << (wal + dep) << "</div></div>"
             "<div class='stat'><div class='lbl'>Buys</div><div class='val'>" << buys << "</div></div>"
             "<div class='stat'><div class='lbl'>Sells</div><div class='val'>" << sells << "</div></div>"
             "<div class='stat'><div class='lbl'>Realized P&amp;L</div><div class='val "
          << (totalRealized >= 0 ? "buy" : "sell") << "'>" << totalRealized << "</div></div>"
             "</div>";

        if (!trades.empty())
        {
            // collect parents, then render tree
            std::vector<const Trade*> parents;
            for (const auto& t : trades)
                if (t.type == TradeType::Buy) parents.push_back(&t);

            h << "<h2>Trades</h2><table><tr>"
                 "<th>ID</th><th>Symbol</th><th>Type</th><th>Price</th><th>Qty</th>"
                 "<th>Cost</th><th>Fees</th><th>Exits</th>"
                 "<th>Sold</th><th>Rem</th><th>Realized</th>"
                 "</tr>";

            for (const auto* bp : parents)
            {
                const Trade& b = *bp;
                double sold = db.soldQuantityForParent(b.tradeId);
                double remaining = b.quantity - sold;
                double grossCost = b.value * b.quantity;
                double totalFees = b.buyFee + b.sellFee;
                double realized = 0;
                std::vector<const Trade*> children;
                for (const auto& c : trades)
                {
                    if (c.type == TradeType::CoveredSell && c.parentTradeId == b.tradeId)
                    {
                        children.push_back(&c);
                        auto cp = ProfitCalculator::childProfit(c, b.value);
                        realized += cp.netProfit;
                    }
                }

                auto exitPts = db.loadExitPointsForTrade(b.tradeId);
                int activeExits = 0;
                for (const auto& xp : exitPts)
                    if (!xp.executed) ++activeExits;

                h << "<tr>"
                  << "<td><strong>" << b.tradeId << "</strong></td>"
                  << "<td><strong>" << html::esc(b.symbol) << "</strong></td>"
                  << "<td class='buy'>BUY</td>"
                  << "<td>" << b.value << "</td><td>" << b.quantity << "</td>"
                  << "<td>" << grossCost << "</td>"
                  << "<td>" << totalFees << "</td>"
                  << "<td>" << activeExits << "</td>"
                  << "<td>" << sold << "</td><td>" << remaining << "</td>"
                  << "<td class='" << (realized >= 0 ? "buy" : "sell") << "'>" << realized << "</td>"
                  << "</tr>";

                for (const auto* cp : children)
                {
                    const Trade& c = *cp;
                    auto profit = ProfitCalculator::childProfit(c, b.value);
                    h << "<tr class='child-row'>"
                      << "<td><span class='child-indent'>&#9492;&#9472;</span>" << c.tradeId << "</td>"
                      << "<td>" << html::esc(c.symbol) << "</td>"
                      << "<td class='sell'>SELL</td>"
                      << "<td>" << c.value << "</td><td>" << c.quantity << "</td>"
                      << "<td>" << (c.value * c.quantity) << "</td>"
                      << "<td>" << c.sellFee << "</td>"
                      << "<td>-</td>"
                      << "<td>-</td><td>-</td>"
                      << "<td class='" << (profit.netProfit >= 0 ? "buy" : "sell") << "'>"
                      << profit.netProfit << "</td></tr>";
                }
            }
            h << "</table>";
        }
        res.set_content(html::wrap("Dashboard", h.str()), "text/html");
    });

    // ========== GET /wallet ==========
    svr.Get("/wallet", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        std::ostringstream h;
        h << std::fixed << std::setprecision(17);
        h << html::msgBanner(req) << html::errBanner(req);
        h << "<h1>Wallet</h1>";
        double bal = db.loadWalletBalance();
        double dep = db.deployedCapital();
        h << "<div class='row'>"
             "<div class='stat'><div class='lbl'>Liquid</div><div class='val'>" << bal << "</div></div>"
             "<div class='stat'><div class='lbl'>Deployed</div><div class='val'>" << dep << "</div></div>"
             "<div class='stat'><div class='lbl'>Total</div><div class='val'>" << (bal + dep) << "</div></div>"
             "</div>";
        h << "<div class='forms-row'>"
             "<form class='card' method='POST' action='/deposit'><h3>Deposit</h3>"
             "<label>Amount</label><input type='number' name='amount' step='any' required> "
             "<button>Deposit</button></form>"
             "<form class='card' method='POST' action='/withdraw'><h3>Withdraw</h3>"
             "<label>Amount</label><input type='number' name='amount' step='any' required> "
             "<button class='btn-warn'>Withdraw</button></form>"
             "</div>";
        {
            auto trades = db.loadTrades();
            std::vector<std::string> syms;
            for (const auto& t : trades)
                if (std::find(syms.begin(), syms.end(), t.symbol) == syms.end())
                    syms.push_back(t.symbol);
            if (!syms.empty())
            {
                h << "<h2>Allocated (In Trades)</h2>"
                     "<table><tr><th>Symbol</th><th>Qty</th><th>Avg Entry</th>"
                     "<th>Value</th><th>Trades</th></tr>";
                for (const auto& sym : syms)
                {
                    double allocQty = 0, allocValue = 0;
                    int tradeCount = 0;
                    for (const auto& t : trades)
                    {
                        if (t.symbol != sym || t.type != TradeType::Buy) continue;
                        double sold = db.soldQuantityForParent(t.tradeId);
                        double released = db.releasedForTrade(t.tradeId);
                        double rem = t.quantity - sold - released;
                        if (rem <= 0) continue;
                        allocQty += rem;
                        allocValue += t.value * rem;
                        ++tradeCount;
                    }
                    if (allocQty <= 0) continue;
                    double avgEntry = allocValue / allocQty;
                    h << "<tr><td>" << html::esc(sym) << "</td>"
                      << "<td>" << allocQty << "</td>"
                      << "<td>" << avgEntry << "</td>"
                      << "<td>" << allocValue << "</td>"
                      << "<td>" << tradeCount << "</td></tr>";
                }
                h << "</table>";
                h << "<h2>Not Allocated (Free Holdings)</h2>"
                     "<table><tr><th>Symbol</th><th>Net Holdings</th>"
                     "<th>In Trades</th><th>Free</th></tr>";
                for (const auto& sym : syms)
                {
                    double net = db.holdingsForSymbol(sym);
                    double allocQty = 0;
                    for (const auto& t : trades)
                    {
                        if (t.symbol != sym || t.type != TradeType::Buy) continue;
                        double sold = db.soldQuantityForParent(t.tradeId);
                        double released = db.releasedForTrade(t.tradeId);
                        double rem = t.quantity - sold - released;
                        if (rem > 0) allocQty += rem;
                    }
                    double free = net - allocQty;
                    h << "<tr><td>" << html::esc(sym) << "</td>"
                      << "<td>" << net << "</td>"
                      << "<td>" << allocQty << "</td>"
                      << "<td>" << free << "</td></tr>";
                }
                h << "</table>";
                h << "<h2>Deallocate</h2>"
                     "<form class='card' method='POST' action='/deallocate'>"
                     "<h3>Release holdings from a trade to free pool</h3>"
                     "<label>Trade ID</label><input type='number' name='tradeId' required><br>"
                     "<label>Quantity</label><input type='number' name='quantity' step='any' required><br>"
                     "<button class='btn-warn'>Deallocate</button></form>";
            }
        }
        res.set_content(html::wrap("Wallet", h.str()), "text/html");
    });

    // ========== POST /deposit ==========
    svr.Post("/deposit", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        double amt = fd(f, "amount");
        if (amt <= 0) { res.set_redirect("/wallet?err=Amount+must+be+positive", 303); return; }
        db.deposit(amt);
        res.set_redirect("/wallet?msg=Deposited+successfully", 303);
    });

    // ========== POST /withdraw ==========
    svr.Post("/withdraw", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        double amt = fd(f, "amount");
        if (amt <= 0) { res.set_redirect("/wallet?err=Amount+must+be+positive", 303); return; }
        double bal = db.loadWalletBalance();
        if (amt > bal) { res.set_redirect("/wallet?err=Insufficient+balance", 303); return; }
        db.withdraw(amt);
        res.set_redirect("/wallet?msg=Withdrawn+successfully", 303);
    });

    // ========== POST /deallocate ==========
    svr.Post("/deallocate", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        int id = fi(f, "tradeId");
        double qty = fd(f, "quantity");
        if (qty <= 0) { res.set_redirect("/wallet?err=Quantity+must+be+positive", 303); return; }
        if (!db.releaseFromTrade(id, qty))
        {
            res.set_redirect("/wallet?err=Deallocate+failed+(invalid+trade+or+qty+exceeds+allocated)", 303);
            return;
        }
        res.set_redirect("/wallet?msg=Deallocated+from+trade+" + std::to_string(id), 303);
    });

    // ========== GET /portfolio ==========
    svr.Get("/portfolio", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        std::ostringstream h;
        h << std::fixed << std::setprecision(17);
        h << html::msgBanner(req);
        h << "<h1>Portfolio</h1>";
        auto trades = db.loadTrades();
        double totalCost = 0, totalRemCost = 0, totalRealized = 0;
        int buyCount = 0, sellCount = 0;
        std::vector<std::string> seen;
        h << "<table><tr><th>Symbol</th><th>Buys</th><th>Sells</th>"
             "<th>Bought Qty</th><th>Bought Cost</th>"
             "<th>Rem Qty</th><th>Rem Cost</th><th>Avg Entry (Rem)</th>"
             "<th>Realized</th></tr>";
        for (const auto& t : trades)
        {
            if (std::find(seen.begin(), seen.end(), t.symbol) != seen.end()) continue;
            seen.push_back(t.symbol);
            double boughtQty = 0, boughtCost = 0;
            double remQty = 0, remCost = 0;
            double symRealized = 0;
            int b = 0, s = 0;
            for (const auto& u : trades)
            {
                if (u.symbol != t.symbol) continue;
                if (u.type == TradeType::Buy)
                {
                    boughtQty += u.quantity;
                    boughtCost += u.value * u.quantity;
                    double sold = db.soldQuantityForParent(u.tradeId);
                    double rem = u.quantity - sold;
                    if (rem > 0) { remQty += rem; remCost += u.value * rem; }
                    ++b;
                }
                else
                {
                    ++s;
                    if (u.parentTradeId >= 0)
                    {
                        for (const auto& p : trades)
                            if (p.tradeId == u.parentTradeId)
                            {
                                auto cp = ProfitCalculator::childProfit(u, p.value);
                                symRealized += cp.netProfit;
                                break;
                            }
                    }
                }
            }
            double avgRem = (remQty > 0) ? remCost / remQty : 0;
            h << "<tr><td>" << html::esc(t.symbol) << "</td><td>" << b << "</td><td>" << s
              << "</td><td>" << boughtQty << "</td><td>" << boughtCost
              << "</td><td>" << remQty << "</td><td>" << remCost
              << "</td><td>" << avgRem << "</td>"
              << "<td class='" << (symRealized >= 0 ? "buy" : "sell") << "'>" << symRealized << "</td></tr>";
            totalCost += boughtCost; totalRemCost += remCost; totalRealized += symRealized;
            buyCount += b; sellCount += s;
        }
        h << "</table>";
        double wal = db.loadWalletBalance();
        h << "<div class='row'>"
             "<div class='stat'><div class='lbl'>Total Bought</div><div class='val'>" << totalCost << "</div></div>"
             "<div class='stat'><div class='lbl'>Remaining</div><div class='val'>" << totalRemCost << "</div></div>"
             "<div class='stat'><div class='lbl'>Realized</div><div class='val "
          << (totalRealized >= 0 ? "buy" : "sell") << "'>" << totalRealized << "</div></div>"
             "<div class='stat'><div class='lbl'>Buys</div><div class='val'>" << buyCount << "</div></div>"
             "<div class='stat'><div class='lbl'>Sells</div><div class='val'>" << sellCount << "</div></div>"
             "<div class='stat'><div class='lbl'>Wallet</div><div class='val'>" << wal << "</div></div>"
             "<div class='stat'><div class='lbl'>Total (Rem+Wal)</div><div class='val'>" << (wal + totalRemCost) << "</div></div>"
             "</div>";
        res.set_content(html::wrap("Portfolio", h.str()), "text/html");
    });

    // ========== GET /dca ==========
    svr.Get("/dca", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        std::ostringstream h;
        h << std::fixed << std::setprecision(17);
        h << "<h1>DCA Tracker</h1>";
        auto trades = db.loadTrades();
        std::vector<std::string> seen;
        h << "<table><tr><th>Symbol</th><th>Buys</th>"
             "<th>Bought Qty</th><th>Bought Cost</th><th>Avg (Bought)</th>"
             "<th>Rem Qty</th><th>Rem Cost</th><th>Avg (Rem)</th>"
             "<th>Low</th><th>High</th><th>Spread</th></tr>";
        for (const auto& t : trades)
        {
            if (t.type != TradeType::Buy) continue;
            if (std::find(seen.begin(), seen.end(), t.symbol) != seen.end()) continue;
            seen.push_back(t.symbol);
            double cost = 0, qty = 0, lo = 1e18, hi = 0; int cnt = 0;
            double remQty = 0, remCost = 0;
            for (const auto& u : trades)
            {
                if (u.symbol != t.symbol || u.type != TradeType::Buy) continue;
                cost += u.value * u.quantity; qty += u.quantity;
                if (u.value < lo) lo = u.value;
                if (u.value > hi) hi = u.value;
                ++cnt;
                double sold = db.soldQuantityForParent(u.tradeId);
                double rem = u.quantity - sold;
                if (rem > 0) { remQty += rem; remCost += u.value * rem; }
            }
            double avg = qty != 0 ? cost / qty : 0;
            double avgRem = remQty > 0 ? remCost / remQty : 0;
            h << "<tr><td>" << html::esc(t.symbol) << "</td><td>" << cnt
              << "</td><td>" << qty << "</td><td>" << cost << "</td><td>" << avg
              << "</td><td>" << remQty << "</td><td>" << remCost << "</td><td>" << avgRem
              << "</td><td>" << lo << "</td><td>" << hi << "</td><td>" << (hi - lo) << "</td></tr>";
        }
        h << "</table>";
        if (seen.empty()) h << "<p class='empty'>(no buy trades)</p>";
        res.set_content(html::wrap("DCA", h.str()), "text/html");
    });

    // ========== GET /pnl — P&L Curve ==========
    svr.Get("/pnl", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto pnl = db.loadPnl();
        // also build synthetic entries from existing child sells if ledger is empty
        auto trades = db.loadTrades();

        std::ostringstream pg;
        pg << "<!DOCTYPE html><html><head><meta charset='utf-8'>"
              "<meta name='viewport' content='width=device-width,initial-scale=1'>"
              "<title>P&amp;L - Quant</title>" << html::css()
           << "<style>"
              ".pnl-wrap{position:relative;width:100%;height:500px;margin:16px 0;background:#0f1b2d;"
              "border:1px solid #1a2744;border-radius:8px;overflow:hidden;}"
              ".pnl-wrap canvas{display:block;width:100%;height:100%;}"
              ".pnl-tip{position:absolute;background:#0f1b2dee;border:1px solid #1a2744;border-radius:6px;"
              "padding:6px 10px;font-size:0.78em;color:#cbd5e1;pointer-events:none;display:none;white-space:pre;z-index:10;}"
              "</style></head><body>" << html::nav()
           << "<div class='container'>"
              << html::msgBanner(req) << html::errBanner(req)
              << "<h1>P&amp;L Curve</h1>";

        // stats
        double totalNet = 0, totalGross = 0, totalFees = 0;
        double bestTrade = 0, worstTrade = 0;
        int tradeCount = 0, wins = 0, losses = 0;
        if (!pnl.empty())
        {
            totalNet = pnl.back().cumProfit;
            for (const auto& e : pnl)
            {
                totalGross += e.grossProfit;
                totalFees += (e.grossProfit - e.netProfit);
                ++tradeCount;
                if (e.netProfit >= 0) ++wins; else ++losses;
                if (e.netProfit > bestTrade) bestTrade = e.netProfit;
                if (e.netProfit < worstTrade) worstTrade = e.netProfit;
            }
        }
        double winRate = tradeCount > 0 ? (static_cast<double>(wins) / tradeCount * 100.0) : 0;
        double avgNet = tradeCount > 0 ? totalNet / tradeCount : 0;
        pg << std::fixed << std::setprecision(2);
        pg << "<div class='row'>"
              "<div class='stat'><div class='lbl'>Total Net P&amp;L</div>"
              "<div class='val " << (totalNet >= 0 ? "buy" : "sell") << "'>" << totalNet << "</div></div>"
              "<div class='stat'><div class='lbl'>Total Gross</div><div class='val'>" << totalGross << "</div></div>"
              "<div class='stat'><div class='lbl'>Total Fees</div><div class='val'>" << totalFees << "</div></div>"
              "<div class='stat'><div class='lbl'>Trades</div><div class='val'>" << tradeCount << "</div></div>"
              "<div class='stat'><div class='lbl'>Win Rate</div><div class='val " << (winRate >= 50 ? "buy" : "sell") << "'>"
           << winRate << "%</div></div>"
              "<div class='stat'><div class='lbl'>W / L</div><div class='val'>"
           << wins << " / " << losses << "</div></div>"
              "<div class='stat'><div class='lbl'>Avg Net</div><div class='val "
           << (avgNet >= 0 ? "buy" : "sell") << "'>" << avgNet << "</div></div>"
              "<div class='stat'><div class='lbl'>Best</div><div class='val buy'>" << bestTrade << "</div></div>"
              "<div class='stat'><div class='lbl'>Worst</div><div class='val sell'>" << worstTrade << "</div></div>"
              "</div>";

        // backfill button
        {
            int childSells = 0;
            for (const auto& t : trades)
                if (t.type == TradeType::CoveredSell && t.parentTradeId >= 0) ++childSells;
            if (childSells > 0 && pnl.empty())
            {
                pg << "<form class='card' method='POST' action='/pnl-backfill' style='display:inline-block;'>"
                      "<div style='color:#64748b;font-size:0.78em;margin-bottom:4px;'>"
                   << childSells << " child sells found with no P&amp;L entries &mdash; backfill from existing trades?</div>"
                      "<button class='btn-warn'>Backfill P&amp;L Ledger</button></form>";
            }
        }

        pg << "<div class='pnl-wrap' id='pnlWrap'>"
              "<canvas id='pnlCanvas'></canvas>"
              "<div class='pnl-tip' id='pnlTip'></div></div>";

        // trade log table
        if (!pnl.empty())
        {
            pg << std::fixed << std::setprecision(17);
            pg << "<h2>Realized Trades</h2>"
                  "<table><tr><th>Time</th><th>Symbol</th><th>Sell</th><th>Parent</th>"
                  "<th>Entry</th><th>Exit</th><th>Qty</th><th>Gross</th><th>Net</th><th>Cumulative</th></tr>";
            for (const auto& e : pnl)
            {
                // format timestamp
                std::time_t tt = static_cast<std::time_t>(e.timestamp);
                std::tm tm;
#ifdef _WIN32
                localtime_s(&tm, &tt);
#else
                localtime_r(&tt, &tm);
#endif
                char tbuf[32];
                std::strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M", &tm);
                pg << "<tr><td>" << tbuf << "</td>"
                   << "<td>" << html::esc(e.symbol) << "</td>"
                   << "<td>" << e.sellTradeId << "</td><td>" << e.parentTradeId << "</td>"
                   << "<td>" << e.entryPrice << "</td><td>" << e.sellPrice << "</td>"
                   << "<td>" << e.quantity << "</td>"
                   << "<td>" << e.grossProfit << "</td>"
                   << "<td class='" << (e.netProfit >= 0 ? "buy" : "sell") << "'>" << e.netProfit << "</td>"
                   << "<td class='" << (e.cumProfit >= 0 ? "buy" : "sell") << "'>" << e.cumProfit << "</td></tr>";
            }
            pg << "</table>";
        }
        else
        {
            pg << "<p class='empty'>(no realized P&amp;L yet &mdash; execute sells from the exit strategy to record P&amp;L)</p>";
        }

        pg << "</div>"; // container

        // JavaScript chart
        pg << "<script>\n(function(){\n'use strict';\n";
        // embed data
        pg << "var data=[";
        {
            bool first = true;
            pg << std::fixed << std::setprecision(8);
            for (const auto& e : pnl)
            {
                if (!first) pg << ",";
                first = false;
                pg << "{ts:" << e.timestamp
                   << ",sym:'" << e.symbol << "'"
                   << ",net:" << e.netProfit
                   << ",cum:" << e.cumProfit
                   << ",qty:" << e.quantity
                   << ",entry:" << e.entryPrice
                   << ",sell:" << e.sellPrice
                   << "}";
            }
        }
        pg << "];\n";

        pg << "var canvas=document.getElementById('pnlCanvas');\n"
              "var ctx=canvas.getContext('2d');\n"
              "var wrap=document.getElementById('pnlWrap');\n"
              "var tip=document.getElementById('pnlTip');\n"
              "var dpr=window.devicePixelRatio||1;\n"
              "var W,H;\n"
              "var pad={t:30,r:30,b:50,l:80};\n";

        pg << "function resize(){\n"
              "  W=wrap.clientWidth;H=wrap.clientHeight;\n"
              "  canvas.width=W*dpr;canvas.height=H*dpr;\n"
              "  canvas.style.width=W+'px';canvas.style.height=H+'px';\n"
              "  ctx.setTransform(dpr,0,0,dpr,0,0);\n"
              "  draw();\n"
              "}\n";

        pg << "function draw(){\n"
              "  ctx.clearRect(0,0,W,H);\n"
              "  if(data.length<1){ctx.fillStyle='#475569';ctx.font='14px monospace';"
              "ctx.fillText('No P&L data yet',W/2-60,H/2);return;}\n"
              "  var pw=W-pad.l-pad.r, ph=H-pad.t-pad.b;\n"
              // time range
              "  var tsMin=data[0].ts,tsMax=data[data.length-1].ts;\n"
              "  if(tsMax===tsMin) tsMax=tsMin+1;\n"
              // value range — always include 0
              "  var vMin=0,vMax=0;\n"
              "  for(var i=0;i<data.length;i++){var c=data[i].cum;if(c<vMin)vMin=c;if(c>vMax)vMax=c;}\n"
              "  var vPad=(vMax-vMin)*0.1||1;\n"
              "  vMin-=vPad;vMax+=vPad;\n"
              // mapping functions
              "  function tx(ts){return pad.l+((ts-tsMin)/(tsMax-tsMin))*pw;}\n"
              "  function ty(v){return pad.t+ph-((v-vMin)/(vMax-vMin))*ph;}\n"
              // grid
              "  ctx.strokeStyle='#152238';ctx.lineWidth=1;\n"
              "  var ySteps=6;\n"
              "  for(var i=0;i<=ySteps;i++){\n"
              "    var v=vMin+(vMax-vMin)*(i/ySteps);\n"
              "    var y=ty(v);\n"
              "    ctx.beginPath();ctx.moveTo(pad.l,y);ctx.lineTo(W-pad.r,y);ctx.stroke();\n"
              "    ctx.fillStyle='#64748b';ctx.font='11px monospace';ctx.textAlign='right';\n"
              "    ctx.fillText(v.toFixed(2),pad.l-6,y+4);\n"
              "  }\n"
              // time labels
              "  var xSteps=Math.min(data.length,8);\n"
              "  ctx.textAlign='center';\n"
              "  for(var i=0;i<xSteps;i++){\n"
              "    var idx=Math.floor(i*(data.length-1)/(xSteps-1||1));\n"
              "    var d=new Date(data[idx].ts*1000);\n"
              "    var lbl=d.toLocaleDateString(undefined,{month:'short',day:'numeric'});\n"
              "    var x=tx(data[idx].ts);\n"
              "    ctx.beginPath();ctx.moveTo(x,pad.t);ctx.lineTo(x,H-pad.b);ctx.strokeStyle='#152238';ctx.stroke();\n"
              "    ctx.fillStyle='#64748b';ctx.fillText(lbl,x,H-pad.b+16);\n"
              "  }\n"
              // zero line
              "  if(vMin<0&&vMax>0){\n"
              "    ctx.strokeStyle='#475569';ctx.lineWidth=1;ctx.setLineDash([4,4]);\n"
              "    ctx.beginPath();ctx.moveTo(pad.l,ty(0));ctx.lineTo(W-pad.r,ty(0));ctx.stroke();\n"
              "    ctx.setLineDash([]);\n"
              "  }\n"
              // area fill
              "  ctx.beginPath();ctx.moveTo(tx(data[0].ts),ty(0));\n"
              "  for(var i=0;i<data.length;i++) ctx.lineTo(tx(data[i].ts),ty(data[i].cum));\n"
              "  ctx.lineTo(tx(data[data.length-1].ts),ty(0));ctx.closePath();\n"
              "  var last=data[data.length-1].cum;\n"
              "  ctx.fillStyle=last>=0?'rgba(34,197,94,0.12)':'rgba(239,68,68,0.12)';\n"
              "  ctx.fill();\n"
              // line
              "  ctx.beginPath();\n"
              "  for(var i=0;i<data.length;i++){\n"
              "    var x=tx(data[i].ts),y=ty(data[i].cum);\n"
              "    if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);\n"
              "  }\n"
              "  ctx.strokeStyle=last>=0?'#22c55e':'#ef4444';ctx.lineWidth=2;ctx.stroke();\n"
              // dots
              "  for(var i=0;i<data.length;i++){\n"
              "    var x=tx(data[i].ts),y=ty(data[i].cum);\n"
              "    ctx.beginPath();ctx.arc(x,y,4,0,Math.PI*2);\n"
              "    ctx.fillStyle=data[i].net>=0?'#22c55e':'#ef4444';ctx.fill();\n"
              "    ctx.strokeStyle='#0b1426';ctx.lineWidth=1;ctx.stroke();\n"
              "  }\n"
              // axis labels
              "  ctx.save();ctx.translate(14,pad.t+ph/2);ctx.rotate(-Math.PI/2);\n"
              "  ctx.fillStyle='#64748b';ctx.font='12px monospace';ctx.textAlign='center';\n"
              "  ctx.fillText('Cumulative P&L',0,0);ctx.restore();\n"
              "  ctx.fillStyle='#64748b';ctx.font='12px monospace';ctx.textAlign='center';\n"
              "  ctx.fillText('Time',pad.l+pw/2,H-4);\n"
              // title
              "  ctx.fillStyle='#c9a44a';ctx.font='bold 14px monospace';ctx.textAlign='left';\n"
              "  ctx.fillText('P&L: '+(last>=0?'+':'')+last.toFixed(2),pad.l,18);\n"
              "}\n";

        // tooltip
        pg << "canvas.addEventListener('mousemove',function(ev){\n"
              "  if(data.length<1){tip.style.display='none';return;}\n"
              "  var rect=canvas.getBoundingClientRect();\n"
              "  var mx=ev.clientX-rect.left,my=ev.clientY-rect.top;\n"
              "  var pw=W-pad.l-pad.r;\n"
              "  var tsMin=data[0].ts,tsMax=data[data.length-1].ts;\n"
              "  if(tsMax===tsMin)tsMax=tsMin+1;\n"
              "  var best=-1,bestD=1e9;\n"
              "  for(var i=0;i<data.length;i++){\n"
              "    var x=pad.l+((data[i].ts-tsMin)/(tsMax-tsMin))*pw;\n"
              "    var d=Math.abs(x-mx);if(d<bestD){bestD=d;best=i;}\n"
              "  }\n"
              "  if(best<0||bestD>30){tip.style.display='none';return;}\n"
              "  var e=data[best];\n"
              "  var d=new Date(e.ts*1000);\n"
              "  tip.textContent=d.toLocaleString()+'\\n'"
              "+e.sym+' | '+e.qty.toFixed(4)+' qty\\n'"
              "+'Entry: '+e.entry.toFixed(4)+'  Exit: '+e.sell.toFixed(4)+'\\n'"
              "+'Net: '+(e.net>=0?'+':'')+e.net.toFixed(4)+'\\n'"
              "+'Cumulative: '+(e.cum>=0?'+':'')+e.cum.toFixed(4);\n"
              "  tip.style.display='block';\n"
              "  tip.style.left=(mx+14)+'px';tip.style.top=(my-10)+'px';\n"
              "});\n"
              "canvas.addEventListener('mouseleave',function(){tip.style.display='none';});\n";

        pg << "window.addEventListener('resize',resize);\n"
              "resize();\n"
              "})();\n</script></body></html>";

        res.set_content(pg.str(), "text/html");
    });

    // ========== POST /pnl-backfill ==========
    svr.Post("/pnl-backfill", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto trades = db.loadTrades();
        int filled = 0;
        for (const auto& t : trades)
        {
            if (t.type != TradeType::CoveredSell || t.parentTradeId < 0) continue;
            const Trade* parent = nullptr;
            for (const auto& p : trades)
                if (p.tradeId == t.parentTradeId) { parent = &p; break; }
            if (!parent) continue;
            double gp = (t.value - parent->value) * t.quantity;
            double np = gp - t.sellFee;
            db.recordPnl(t.symbol, t.tradeId, t.parentTradeId,
                         parent->value, t.value, t.quantity, gp, np);
            ++filled;
        }
        res.set_redirect("/pnl?msg=Backfilled+" + std::to_string(filled) + "+P%26L+entries", 303);
    });

    // ========== GET /wipe ==========
    svr.Get("/wipe", [&](const httplib::Request& req, httplib::Response& res) {
        std::ostringstream h;
        h << html::msgBanner(req);
        h << "<h1>Wipe Database</h1>"
             "<p style='color:#ef4444;'>This will delete ALL trades, wallet balance, "
             "pending exits, entry points, and history.</p>"
             "<form class='card' method='POST' action='/do-wipe'>"
             "<button class='btn-danger'>Wipe Everything</button></form>"
             "<br><a class='btn' href='/'>Cancel</a>";
        res.set_content(html::wrap("Wipe", h.str()), "text/html");
    });

    // ========== POST /do-wipe ==========
    svr.Post("/do-wipe", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        db.clearAll();
        res.set_redirect("/?msg=Database+wiped", 303);
    });
}
