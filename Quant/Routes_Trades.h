#pragma once

#include "AppContext.h"
#include "HtmlHelpers.h"
#include "ProfitCalculator.h"
#include <mutex>

inline void registerTradeRoutes(httplib::Server& svr, AppContext& ctx)
{
    auto& db = ctx.defaultDb;
    auto& dbMutex = ctx.dbMutex;

    // ========== GET /trades � Trades list + forms ==========
    svr.Get("/trades", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        std::ostringstream h;
        h << std::fixed << std::setprecision(17);
        h << html::msgBanner(req) << html::errBanner(req);
        h << "<h1>Trades</h1>";
        auto trades = db.loadTrades();
        if (trades.empty()) { h << "<p class='empty'>(no trades)</p>"; }
        else
        {
            // collect parent buys, then render each with children indented below
            std::vector<const Trade*> parents;
            std::vector<const Trade*> orphanSells;
            for (const auto& t : trades)
            {
                if (t.type == TradeType::Buy)
                    parents.push_back(&t);
                else if (t.parentTradeId < 0)
                    orphanSells.push_back(&t);
            }

            h << "<table><tr>"
                 "<th>ID</th><th>Date</th><th>Symbol</th><th>Type</th><th>Price</th><th>Qty</th>"
                 "<th>Cost</th><th>Fees</th>"
                 "<th>Sold</th><th>Rem</th><th>Realized</th><th>Actions</th>"
                 "</tr>";

            for (const auto* bp : parents)
            {
                const Trade& b = *bp;
                double sold = db.soldQuantityForParent(b.tradeId);
                double remaining = b.quantity - sold;
                double grossCost = b.value * b.quantity;
                double totalFees = b.buyFee + b.sellFee;
                // sum realized from children
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

                // ---- parent row ----
                h << "<tr>"
                  << "<td><a href='/horizons?tradeId=" << b.tradeId << "'><strong>" << b.tradeId << "</strong></a></td>"
                  << "<td style='font-size:0.78em;'>" << html::fmtTime(b.timestamp) << "</td>"
                  << "<td><strong>" << html::esc(b.symbol) << "</strong></td>"
                  << "<td class='buy'>BUY</td>"
                  << "<td>" << b.value << "</td><td>" << b.quantity << "</td>"
                  << "<td>" << grossCost << "</td>"
                  << "<td>" << totalFees << "</td>"
                  << "<td>" << sold << "</td><td>" << remaining << "</td>"
                  << "<td class='" << (realized >= 0 ? "buy" : "sell") << "'>" << realized << "</td>"
                  << "<td>"
                  << "<a class='btn btn-sm' href='/edit-trade?id=" << b.tradeId << "'>Edit</a> "
                  << "<form class='iform' method='POST' action='/delete-trade'>"
                  << "<input type='hidden' name='id' value='" << b.tradeId << "'>"
                  << "<button class='btn-sm btn-danger'>Del</button></form>"
                  << "</td></tr>";

                // ---- exit point rows (editable TP/SL per level) ----
                auto exitPts = db.loadExitPointsForTrade(b.tradeId);
                for (const auto& xp : exitPts)
                {
                    std::string xpStatus = xp.executed ? "DONE" : "PENDING";
                    std::string xpClass = xp.executed ? "off" : "buy";
                    h << "<tr class='child-row' style='color:#64748b;font-size:0.85em;'>"
                      << "<td><span class='child-indent'>&#9500;</span>X[" << xp.levelIndex << "]</td>"
                      << "<td></td><td></td><td style='font-size:0.78em;'>EXIT</td>";
                    if (!xp.executed)
                    {
                        // Editable TP
                        h << "<td><form class='iform' method='POST' action='/set-exit-tp'>"
                          << "<input type='hidden' name='exitId' value='" << xp.exitId << "'>"
                          << "<input type='number' name='tp' step='any' value='" << xp.tpPrice << "' style='width:70px;'>"
                          << "<button class='btn-sm' title='Set TP'>&#10003;</button></form></td>"
                          // Editable sell qty
                          << "<td><form class='iform' method='POST' action='/set-exit-qty'>"
                          << "<input type='hidden' name='exitId' value='" << xp.exitId << "'>"
                          << "<input type='number' name='qty' step='any' value='" << xp.sellQty << "' style='width:55px;'>"
                          << "<button class='btn-sm' title='Set Qty'>&#10003;</button></form></td>"
                          << "<td></td>"
                          // Editable SL
                          << "<td><form class='iform' method='POST' action='/set-exit-sl'>"
                          << "<input type='hidden' name='exitId' value='" << xp.exitId << "'>"
                          << "<input type='number' name='sl' step='any' value='" << xp.slPrice << "' style='width:70px;'>"
                          << "<button class='btn-sm' title='Set SL'>&#10003;</button></form></td>"
                          // SL active toggle + status + delete
                          << "<td></td><td></td>"
                          << "<td class='" << xpClass << "'>" << xpStatus
                          << " <form class='iform' style='display:inline;' method='POST' action='/set-exit-sl-active'>"
                          << "<input type='hidden' name='exitId' value='" << xp.exitId << "'>"
                          << "<input type='hidden' name='active' value='" << (xp.slActive ? "0" : "1") << "'>"
                          << "<button class='btn-sm" << (xp.slActive ? "" : " btn-danger") << "' title='Toggle SL'>SL:" << (xp.slActive ? "ON" : "OFF") << "</button></form></td>"
                          << "<td>"
                          << "<form class='iform' method='POST' action='/delete-exit'>"
                          << "<input type='hidden' name='exitId' value='" << xp.exitId << "'>"
                          << "<button class='btn-sm btn-danger'>Del</button></form>";
                    }
                    else
                    {
                        h << "<td>" << xp.tpPrice << "</td>"
                          << "<td>" << xp.sellQty << "</td>"
                          << "<td></td>"
                          << "<td>" << xp.slPrice << "</td>"
                          << "<td></td><td></td>"
                          << "<td class='" << xpClass << "'>" << xpStatus << "</td>"
                          << "<td>#" << xp.linkedSellId;
                    }
                    h << "</td></tr>";
                }
                // ---- add exit point button ----
                h << "<tr class='child-row' style='font-size:0.82em;'>"
                  << "<td colspan='12'>"
                  << "<form class='iform' method='POST' action='/add-exit'>"
                  << "<input type='hidden' name='tradeId' value='" << b.tradeId << "'>"
                  << "<input type='hidden' name='symbol' value='" << html::esc(b.symbol) << "'>"
                  << "<input type='number' name='tp' step='any' placeholder='TP price' style='width:70px;' required>"
                  << "<input type='number' name='sl' step='any' placeholder='SL price' style='width:70px;' value='0'>"
                  << "<input type='number' name='qty' step='any' placeholder='Sell qty' style='width:60px;' required>"
                  << "<button class='btn-sm' title='Add exit point'>+ Exit</button></form>"
                  << "</td></tr>";

                // ---- child rows (indented) ----
                for (const auto* cp : children)
                {
                    const Trade& c = *cp;
                    auto profit = ProfitCalculator::childProfit(c, b.value);
                    double cGross = c.value * c.quantity;
                    h << "<tr class='child-row'>"
                      << "<td><span class='child-indent'>&#9492;&#9472;</span>"
                      << "<a href='/edit-trade?id=" << c.tradeId << "'>" << c.tradeId << "</a></td>"
                      << "<td style='font-size:0.78em;'>" << html::fmtTime(c.timestamp) << "</td>"
                      << "<td>" << html::esc(c.symbol) << "</td>"
                      << "<td class='sell'>SELL</td>"
                      << "<td>" << c.value << "</td><td>" << c.quantity << "</td>"
                      << "<td>" << cGross << "</td>"
                      << "<td>" << c.sellFee << "</td>"
                      << "<td>-</td><td>-</td>"
                      << "<td class='" << (profit.netProfit >= 0 ? "buy" : "sell") << "'>"
                      << profit.netProfit << " (" << std::setprecision(2) << profit.roi << "%)" << std::setprecision(17) << "</td>"
                      << "<td>"
                      << "<a class='btn btn-sm' href='/edit-trade?id=" << c.tradeId << "'>Edit</a> "
                      << "<form class='iform' method='POST' action='/delete-trade'>"
                      << "<input type='hidden' name='id' value='" << c.tradeId << "'>"
                      << "<button class='btn-sm btn-danger'>Del</button></form>"
                      << "</td></tr>";
                }
            }

            // orphan sells (no valid parent)
            for (const auto* sp : orphanSells)
            {
                const Trade& s = *sp;
                double cGross = s.value * s.quantity;
                h << "<tr>"
                  << "<td>" << s.tradeId << "</td>"
                  << "<td style='font-size:0.78em;'>" << html::fmtTime(s.timestamp) << "</td>"
                  << "<td>" << html::esc(s.symbol) << "</td>"
                  << "<td class='sell'>SELL</td>"
                  << "<td>" << s.value << "</td><td>" << s.quantity << "</td>"
                  << "<td>" << cGross << "</td>"
                  << "<td>" << s.sellFee << "</td>"
                  << "<td>-</td><td>-</td><td>-</td>"
                  << "<td>"
                  << "<a class='btn btn-sm' href='/edit-trade?id=" << s.tradeId << "'>Edit</a> "
                  << "<form class='iform' method='POST' action='/delete-trade'>"
                  << "<input type='hidden' name='id' value='" << s.tradeId << "'>"
                  << "<button class='btn-sm btn-danger'>Del</button></form>"
                  << "</td></tr>";
            }

            h << "</table>";
        }
        h << "<div class='forms-row'>";
        h << "<form class='card' method='POST' action='/execute-buy'>"
             "<h3>Execute Buy</h3>"
             "<div style='color:#64748b;font-size:0.78em;margin-bottom:8px;'>Creates trade &amp; debits wallet</div>"
             "<label>Symbol</label><input type='text' name='symbol' required><br>"
             "<label>Date</label><input type='datetime-local' name='timestamp'><br>"
             "<label>Price</label><input type='number' name='price' step='any' required><br>"
             "<label>Quantity</label><input type='number' name='quantity' step='any' required><br>"
             "<label>Buy Fee</label><input type='number' name='buyFee' step='any' value='0'><br>"
             "<div style='color:#64748b;font-size:0.78em;margin-top:6px;'>Add exit strategies (TP/SL) after creating the trade</div>"
             "<button>Execute Buy</button></form>";
        h << "<form class='card' method='POST' action='/execute-sell'>"
             "<h3>Execute Sell</h3>"
             "<div style='color:#64748b;font-size:0.78em;margin-bottom:8px;'>Deducts from symbol holdings &amp; credits wallet</div>"
             "<label>Symbol</label><input type='text' name='symbol' required><br>"
             "<label>Date</label><input type='datetime-local' name='timestamp'><br>"
             "<label>Price</label><input type='number' name='price' step='any' required><br>"
             "<label>Quantity</label><input type='number' name='quantity' step='any' required><br>"
             "<label>Sell Fee</label><input type='number' name='sellFee' step='any' value='0'><br>"
             "<button>Execute Sell</button></form>";
        h << "<form class='card' method='POST' action='/add-trade'>"
             "<h3>Import Buy</h3>"
             "<div style='color:#64748b;font-size:0.78em;margin-bottom:8px;'>Record only &mdash; no wallet movement</div>"
             "<input type='hidden' name='type' value='Buy'>"
             "<label>Symbol</label><input type='text' name='symbol' required><br>"
             "<label>Date</label><input type='datetime-local' name='timestamp'><br>"
             "<label>Price</label><input type='number' name='price' step='any' required><br>"
             "<label>Quantity</label><input type='number' name='quantity' step='any' required><br>"
             "<label>Buy Fee</label><input type='number' name='buyFee' step='any' value='0'><br>"
             "<div style='color:#64748b;font-size:0.78em;margin-top:6px;'>Add exit strategies (TP/SL) after creating the trade</div>"
             "<button>Import Buy</button></form>";
        h << "<form class='card' method='POST' action='/add-trade'>"
             "<h3>Import Sell</h3>"
             "<div style='color:#64748b;font-size:0.78em;margin-bottom:8px;'>Record only &mdash; no wallet movement</div>"
             "<input type='hidden' name='type' value='CoveredSell'>"
             "<label>Symbol</label><input type='text' name='symbol' required><br>"
             "<label>Date</label><input type='datetime-local' name='timestamp'><br>"
             "<label>Parent Buy ID</label><input type='number' name='parentTradeId' required><br>"
             "<label>Price</label><input type='number' name='price' step='any' required><br>"
             "<label>Quantity</label><input type='number' name='quantity' step='any' required><br>"
             "<label>Sell Fee</label><input type='number' name='sellFee' step='any' value='0'><br>"
             "<button>Import Sell</button></form>";
        h << "</div>";
        res.set_content(html::wrap("Trades", h.str()), "text/html");
    });

    // ========== POST /add-trade ==========
    svr.Post("/add-trade", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        if (!ctx.canAddTrade(req))
        {
            res.set_redirect("/trades?err=Trade+limit+reached.+Upgrade+to+Premium+for+unlimited+trades.", 303);
            return;
        }
        auto f = parseForm(req.body);
        Trade t;
        t.tradeId = db.nextTradeId();
        t.symbol = normalizeSymbol(fv(f, "symbol"));
        t.type = (fv(f, "type") == "CoveredSell") ? TradeType::CoveredSell : TradeType::Buy;
        t.value = fd(f, "price");
        t.quantity = fd(f, "quantity");
        t.shortEnabled = false;
        t.timestamp = html::parseDatetimeLocal(fv(f, "timestamp"));
        if (t.timestamp <= 0) t.timestamp = static_cast<long long>(std::time(nullptr));
        if (t.value <= 0 || t.quantity <= 0) { res.set_redirect("/trades?err=Price+and+quantity+must+be+positive", 303); return; }
        if (t.type == TradeType::Buy)
        {
            t.parentTradeId = -1;
            t.buyFee = fd(f, "buyFee");
            t.sellFee = 0.0;
        }
        else
        {
            t.parentTradeId = fi(f, "parentTradeId", -1);
            auto trades = db.loadTrades();
            auto* parent = db.findTradeById(trades, t.parentTradeId);
            if (!parent || parent->type != TradeType::Buy)
            {
                res.set_redirect("/trades?err=Parent+must+be+an+existing+Buy+trade", 303);
                return;
            }
            t.buyFee = 0.0;
            t.sellFee = fd(f, "sellFee");
        }
        db.addTrade(t);
        ctx.symbols.getOrCreate(t.symbol);
        res.set_redirect("/trades?msg=Trade+" + std::to_string(t.tradeId) + "+created", 303);
    });

    // ========== POST /delete-trade ==========
    svr.Post("/delete-trade", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        int id = fi(f, "id");
        auto trades = db.loadTrades();
        if (!db.findTradeById(trades, id)) { res.set_redirect("/trades?err=Trade+not+found", 303); return; }
        db.removeTrade(id);
        res.set_redirect("/trades?msg=Trade+" + std::to_string(id) + "+deleted", 303);
    });

    // ========== POST /set-horizon-tp ==========
    svr.Post("/set-horizon-tp", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        int tradeId = fi(f, "tradeId");
        std::string sym = fv(f, "symbol");
        int idx = fi(f, "index");
        double tp = fd(f, "tp");
        auto levels = db.loadHorizonLevels(sym, tradeId);
        for (auto& lv : levels)
            if (lv.index == idx) { lv.takeProfit = tp; break; }
        db.saveHorizonLevels(sym, tradeId, levels);
        res.set_redirect("/trades?msg=Horizon+TP+[" + std::to_string(idx) + "]+set+for+" + std::to_string(tradeId), 303);
    });

    // ========== POST /set-horizon-sl ==========
    svr.Post("/set-horizon-sl", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        int tradeId = fi(f, "tradeId");
        std::string sym = fv(f, "symbol");
        int idx = fi(f, "index");
        double sl = fd(f, "sl");
        auto levels = db.loadHorizonLevels(sym, tradeId);
        for (auto& lv : levels)
            if (lv.index == idx) { lv.stopLoss = sl; break; }
        db.saveHorizonLevels(sym, tradeId, levels);
        res.set_redirect("/trades?msg=Horizon+SL+[" + std::to_string(idx) + "]+set+for+" + std::to_string(tradeId), 303);
    });

    // ========== POST /set-horizon-sl-active ==========
    svr.Post("/set-horizon-sl-active", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        int tradeId = fi(f, "tradeId");
        std::string sym = fv(f, "symbol");
        int idx = fi(f, "index");
        bool active = (fv(f, "active") == "1");
        auto levels = db.loadHorizonLevels(sym, tradeId);
        for (auto& lv : levels)
            if (lv.index == idx) { lv.stopLossActive = active; break; }
        db.saveHorizonLevels(sym, tradeId, levels);
        std::string state = active ? "ON" : "OFF";
        res.set_redirect("/trades?msg=Horizon+SL+[" + std::to_string(idx) + "]+" + state + "+for+" + std::to_string(tradeId), 303);
    });

    // ========== POST /set-exit-tp ==========
    svr.Post("/set-exit-tp", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        int exitId = fi(f, "exitId");
        double tp = fd(f, "tp");
        auto exits = db.loadExitPoints();
        for (auto& xp : exits)
            if (xp.exitId == exitId) { xp.tpPrice = tp; break; }
        db.saveExitPoints(exits);
        res.set_redirect("/trades?msg=Exit+TP+set+for+X" + std::to_string(exitId), 303);
    });

    // ========== POST /set-exit-sl ==========
    svr.Post("/set-exit-sl", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        int exitId = fi(f, "exitId");
        double sl = fd(f, "sl");
        auto exits = db.loadExitPoints();
        for (auto& xp : exits)
            if (xp.exitId == exitId) { xp.slPrice = sl; break; }
        db.saveExitPoints(exits);
        res.set_redirect("/trades?msg=Exit+SL+set+for+X" + std::to_string(exitId), 303);
    });

    // ========== POST /set-exit-qty ==========
    svr.Post("/set-exit-qty", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        int exitId = fi(f, "exitId");
        double qty = fd(f, "qty");
        auto exits = db.loadExitPoints();
        for (auto& xp : exits)
            if (xp.exitId == exitId) { xp.sellQty = qty; break; }
        db.saveExitPoints(exits);
        res.set_redirect("/trades?msg=Exit+qty+set+for+X" + std::to_string(exitId), 303);
    });

    // ========== POST /set-exit-sl-active ==========
    svr.Post("/set-exit-sl-active", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        int exitId = fi(f, "exitId");
        bool active = (fv(f, "active") == "1");
        auto exits = db.loadExitPoints();
        for (auto& xp : exits)
            if (xp.exitId == exitId) { xp.slActive = active; break; }
        db.saveExitPoints(exits);
        std::string state = active ? "ON" : "OFF";
        res.set_redirect("/trades?msg=Exit+SL+" + state + "+for+X" + std::to_string(exitId), 303);
    });

    // ========== POST /delete-exit ==========
    svr.Post("/delete-exit", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        int exitId = fi(f, "exitId");
        auto exits = db.loadExitPoints();
        std::erase_if(exits, [exitId](const TradeDatabase::ExitPoint& xp) { return xp.exitId == exitId; });
        db.saveExitPoints(exits);
        db.releaseExitId(exitId);
        res.set_redirect("/trades?msg=Exit+X" + std::to_string(exitId) + "+deleted", 303);
    });

    // ========== POST /add-exit ==========
    svr.Post("/add-exit", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        int tradeId = fi(f, "tradeId");
        std::string sym = fv(f, "symbol");
        double tp = fd(f, "tp");
        double sl = fd(f, "sl");
        double qty = fd(f, "qty");
        if (tp <= 0 || qty <= 0) { res.set_redirect("/trades?err=TP+and+qty+required", 303); return; }
        auto exits = db.loadExitPoints();
        // determine next level index for this trade
        int maxIdx = -1;
        for (const auto& xp : exits)
            if (xp.tradeId == tradeId && xp.levelIndex > maxIdx) maxIdx = xp.levelIndex;
        TradeDatabase::ExitPoint xp;
        xp.exitId       = db.nextExitId();
        xp.tradeId      = tradeId;
        xp.symbol       = sym;
        xp.levelIndex   = maxIdx + 1;
        xp.tpPrice      = tp;
        xp.slPrice      = sl;
        xp.sellQty      = qty;
        xp.sellFraction = 0.0;
        xp.slActive     = (sl > 0);
        xp.executed     = false;
        xp.linkedSellId = -1;
        exits.push_back(xp);
        db.saveExitPoints(exits);
        res.set_redirect("/trades?msg=Exit+X" + std::to_string(xp.exitId) + "+added+for+trade+" + std::to_string(tradeId), 303);
    });

    // ========== POST /execute-buy ==========
    svr.Post("/execute-buy", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        if (!ctx.canAddTrade(req))
        {
            res.set_redirect("/trades?err=Trade+limit+reached.+Upgrade+to+Premium+for+unlimited+trades.", 303);
            return;
        }
        auto f = parseForm(req.body);
        std::string sym = normalizeSymbol(fv(f, "symbol"));
        double price = fd(f, "price");
        double qty = fd(f, "quantity");
        double fee = fd(f, "buyFee");
        long long ts = html::parseDatetimeLocal(fv(f, "timestamp"));
        if (sym.empty() || price <= 0 || qty <= 0) { res.set_redirect("/trades?err=Invalid+buy+parameters", 303); return; }
        double walBal = db.loadWalletBalance();
        double needed = price * qty + fee;
        if (needed > walBal) { res.set_redirect("/trades?err=Insufficient+funds", 303); return; }
        int bid = db.executeBuy(sym, price, qty, fee);
        if (ts > 0)
        {
            auto trades = db.loadTrades();
            auto* tp2 = db.findTradeById(trades, bid);
            if (tp2) { tp2->timestamp = ts; db.updateTrade(*tp2); }
        }
        res.set_redirect("/trades?msg=Buy+" + std::to_string(bid) + "+executed", 303);
    });

    // ========== POST /execute-sell ==========
    svr.Post("/execute-sell", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        if (!ctx.canAddTrade(req))
        {
            res.set_redirect("/trades?err=Trade+limit+reached.+Upgrade+to+Premium+for+unlimited+trades.", 303);
            return;
        }
        auto f = parseForm(req.body);
        std::string sym = normalizeSymbol(fv(f, "symbol"));
        double price = fd(f, "price");
        double qty = fd(f, "quantity");
        double fee = fd(f, "sellFee");
        long long ts = html::parseDatetimeLocal(fv(f, "timestamp"));
        if (sym.empty() || price <= 0 || qty <= 0) { res.set_redirect("/trades?err=Invalid+sell+parameters", 303); return; }
        int sid = db.executeSell(sym, price, qty, fee);
        if (sid < 0) { res.set_redirect("/trades?err=Sell+failed+(insufficient+holdings)", 303); return; }
        if (ts > 0)
        {
            auto trades = db.loadTrades();
            auto* tp = db.findTradeById(trades, sid);
            if (tp) { tp->timestamp = ts; db.updateTrade(*tp); }
        }
        res.set_redirect("/trades?msg=Sell+" + std::to_string(sid) + "+executed", 303);
    });

    // ========== GET /edit-trade ==========
    svr.Get("/edit-trade", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        std::ostringstream h;
        h << std::fixed << std::setprecision(17);
        h << html::msgBanner(req) << html::errBanner(req);
        int id = 0;
        try { id = std::stoi(req.get_param_value("id")); } catch (...) {}
        auto trades = db.loadTrades();
        auto* tp = db.findTradeById(trades, id);
        if (!tp) { h << "<h1>Edit Trade</h1><div class='msg err'>Trade not found</div>"; }
        else
        {
            bool isBuy = (tp->type == TradeType::Buy);
            h << "<h1>Edit Trade #" << tp->tradeId << "</h1>"
                 "<form class='card' method='POST' action='/edit-trade'>"
                 "<input type='hidden' name='id' value='" << tp->tradeId << "'>"
                 "<label>Symbol</label><input type='text' name='symbol' value='" << html::esc(tp->symbol) << "'><br>"
                 "<label>Date</label><input type='datetime-local' name='timestamp' value='" << html::fmtDatetimeLocal(tp->timestamp) << "'><br>"
                 "<label>Type</label><select name='type'>"
                 "<option value='Buy'" << (isBuy ? " selected" : "") << ">Buy</option>"
                 "<option value='CoveredSell'" << (!isBuy ? " selected" : "") << ">CoveredSell</option>"
                 "</select><br>"
                 "<label>Price</label><input type='number' name='price' step='any' value='" << tp->value << "'><br>"
                 "<label>Quantity</label><input type='number' name='quantity' step='any' value='" << tp->quantity << "'><br>"
                 "<label>Parent Buy ID</label><input type='number' name='parentTradeId' value='" << tp->parentTradeId << "'>"
                 "<div style='color:#64748b;font-size:0.78em;'>-1 = none (for Buy trades)</div><br>"
                 "<label>Buy Fee</label><input type='number' name='buyFee' step='any' value='" << tp->buyFee << "'><br>"
                 "<label>Sell Fee</label><input type='number' name='sellFee' step='any' value='" << tp->sellFee << "'><br>"
                 "<div style='color:#64748b;font-size:0.78em;margin-top:6px;'>Manage TP/SL via exit strategies on the Trades page</div>"
                 "<br><button>Save Changes</button></form>";
        }
        h << "<br><a class='btn' href='/trades'>Back to Trades</a>";
        res.set_content(html::wrap("Edit Trade", h.str()), "text/html");
    });

    // ========== POST /edit-trade ==========
    svr.Post("/edit-trade", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        int id = fi(f, "id");
        auto trades = db.loadTrades();
        auto* tp = db.findTradeById(trades, id);
        if (!tp) { res.set_redirect("/trades?err=Trade+not+found", 303); return; }
        auto sym = fv(f, "symbol");
        if (!sym.empty()) tp->symbol = normalizeSymbol(sym);
        auto typeStr = fv(f, "type");
        if (!typeStr.empty()) tp->type = (typeStr == "CoveredSell") ? TradeType::CoveredSell : TradeType::Buy;
        double price = fd(f, "price");
        if (price > 0) tp->value = price;
        double qty = fd(f, "quantity");
        if (qty > 0) tp->quantity = qty;
        int pid = fi(f, "parentTradeId", tp->parentTradeId);
        if (tp->type == TradeType::Buy)
        {
            tp->parentTradeId = -1;
        }
        else
        {
            if (pid >= 0)
            {
                auto* parent = db.findTradeById(trades, pid);
                if (!parent || parent->type != TradeType::Buy)
                {
                    res.set_redirect("/trades?err=Parent+must+be+an+existing+Buy+trade", 303);
                    return;
                }
            }
            tp->parentTradeId = pid;
        }
        tp->buyFee = fd(f, "buyFee", tp->buyFee);
        tp->sellFee = fd(f, "sellFee", tp->sellFee);
        auto tsStr = fv(f, "timestamp");
        if (!tsStr.empty()) tp->timestamp = html::parseDatetimeLocal(tsStr);
        db.updateTrade(*tp);
        res.set_redirect("/trades?msg=Trade+" + std::to_string(id) + "+updated", 303);
    });
}
