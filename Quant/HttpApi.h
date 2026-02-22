#pragma once

#include "Trade.h"
#include "ProfitCalculator.h"
#include "MultiHorizonEngine.h"
#include "MarketEntryCalculator.h"
#include "ExitStrategyCalculator.h"
#include "TradeDatabase.h"
#include "D:\Quant\Quant\cpp-httplib-master\httplib.h"

#include <string>
#include <sstream>
#include <iomanip>
#include <mutex>
#include <thread>
#include <algorithm>
#include <map>

// ---- HTML helpers ----
namespace html {

inline std::string esc(const std::string& s)
{
    std::string o;
    for (char c : s)
    {
        if (c == '<') o += "&lt;";
        else if (c == '>') o += "&gt;";
        else if (c == '&') o += "&amp;";
        else if (c == '"') o += "&quot;";
        else o += c;
    }
    return o;
}

inline std::string css()
{
    return
        "<style>"
        "*{box-sizing:border-box;}"
        "body{font-family:'Segoe UI',monospace;background:#0d1117;color:#c9d1d9;margin:0;padding:0;}"
        "nav{background:#161b22;padding:10px 20px;border-bottom:1px solid #30363d;display:flex;gap:8px;flex-wrap:wrap;}"
        "nav a{color:#58a6ff;text-decoration:none;font-size:0.85em;padding:4px 8px;border-radius:4px;}"
        "nav a:hover{background:#21262d;}"
        ".container{max-width:1200px;margin:0 auto;padding:20px;}"
        "h1{color:#58a6ff;margin:0 0 12px 0;}"
        "h2{color:#8b949e;border-bottom:1px solid #21262d;padding-bottom:5px;}"
        "table{border-collapse:collapse;width:100%;margin:8px 0 18px 0;}"
        "th,td{border:1px solid #21262d;padding:5px 8px;text-align:right;font-size:0.85em;}"
        "th{background:#161b22;color:#f0883e;text-align:left;}"
        "td{background:#0d1117;}tr:hover td{background:#161b22;}"
        ".buy{color:#3fb950;font-weight:bold;}"
        ".sell{color:#f85149;font-weight:bold;}"
        ".on{color:#3fb950;}.off{color:#484f58;}"
        "form.card{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:12px;margin:8px 0;}"
        "form.card h3{margin:0 0 8px 0;color:#f0883e;font-size:0.95em;}"
        "label{display:inline-block;min-width:110px;color:#8b949e;font-size:0.82em;}"
        "input,select{background:#0d1117;border:1px solid #30363d;color:#c9d1d9;padding:4px 6px;"
        "border-radius:4px;margin:2px 4px 2px 0;font-family:inherit;font-size:0.85em;}"
        "input:focus,select:focus{border-color:#58a6ff;outline:none;}"
        "button,.btn{background:#238636;color:#fff;border:none;padding:5px 12px;border-radius:4px;"
        "cursor:pointer;font-size:0.82em;font-family:inherit;text-decoration:none;display:inline-block;}"
        "button:hover,.btn:hover{background:#2ea043;}"
        ".btn-danger{background:#da3633;}.btn-danger:hover{background:#f85149;}"
        ".btn-sm{padding:2px 7px;font-size:0.78em;}"
        ".btn-warn{background:#d29922;}.btn-warn:hover{background:#e3b341;}"
        ".row{display:flex;gap:12px;flex-wrap:wrap;}"
        ".stat{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:10px 16px;min-width:140px;}"
        ".stat .lbl{color:#8b949e;font-size:0.78em;}"
        ".stat .val{font-size:1.3em;color:#f0883e;}"
        ".msg{background:#1f6feb22;border:1px solid #1f6feb;padding:6px 10px;border-radius:4px;margin:8px 0;color:#58a6ff;font-size:0.9em;}"
        ".err{background:#f8514922;border:1px solid #f85149;color:#f85149;}"
        ".iform{display:inline;}"
        "input[type=number]{width:90px;}"
        "input[type=text]{width:120px;}"
        ".forms-row{display:flex;gap:10px;flex-wrap:wrap;}"
        ".forms-row form.card{flex:1;min-width:280px;}"
        "p.empty{color:#484f58;font-style:italic;}"
        "</style>";
}

inline std::string nav()
{
    return
        "<nav>"
        "<a href='/'>Dashboard</a>"
        "<a href='/trades'>Trades</a>"
        "<a href='/wallet'>Wallet</a>"
        "<a href='/portfolio'>Portfolio</a>"
        "<a href='/dca'>DCA</a>"
        "<a href='/profit'>Profit</a>"
        "<a href='/generate-horizons'>Horizons Gen</a>"
        "<a href='/price-check'>Price Check</a>"
        "<a href='/market-entry'>Entry Calc</a>"
        "<a href='/exit-strategy'>Exit Calc</a>"
        "<a href='/pending-exits'>Pending Exits</a>"
        "<a href='/entry-points'>Entry Points</a>"
        "<a href='/profit-history'>Profit History</a>"
        "<a href='/params-history'>Params</a>"
        "<a href='/wipe' style='color:#f85149;'>Wipe</a>"
        "</nav>";
}

inline std::string wrap(const std::string& title, const std::string& body)
{
    return "<!DOCTYPE html><html><head><meta charset='utf-8'>"
           "<meta name='viewport' content='width=device-width,initial-scale=1'>"
           "<title>" + esc(title) + " - Quant</title>" + css() +
           "</head><body>" + nav() +
           "<div class='container'>" + body + "</div></body></html>";
}

inline std::string msgBanner(const httplib::Request& req)
{
    if (!req.has_param("msg")) return "";
    return "<div class='msg'>" + esc(req.get_param_value("msg")) + "</div>";
}

inline std::string errBanner(const httplib::Request& req)
{
    if (!req.has_param("err")) return "";
    return "<div class='msg err'>" + esc(req.get_param_value("err")) + "</div>";
}

} // namespace html

// ---- Form body parser ----
inline std::string urlDec(const std::string& s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '+') o += ' ';
        else if (s[i] == '%' && i + 2 < s.size())
        {
            auto hv = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return 0;
            };
            o += (char)(hv(s[i + 1]) * 16 + hv(s[i + 2]));
            i += 2;
        }
        else o += s[i];
    }
    return o;
}

inline std::map<std::string, std::string> parseForm(const std::string& body)
{
    std::map<std::string, std::string> m;
    std::istringstream ss(body);
    std::string pair;
    while (std::getline(ss, pair, '&'))
    {
        auto eq = pair.find('=');
        if (eq != std::string::npos)
            m[urlDec(pair.substr(0, eq))] = urlDec(pair.substr(eq + 1));
    }
    return m;
}

inline std::string fv(const std::map<std::string, std::string>& f, const std::string& k, const std::string& d = "")
{
    auto it = f.find(k);
    return it != f.end() ? it->second : d;
}

inline double fd(const std::map<std::string, std::string>& f, const std::string& k, double d = 0.0)
{
    auto s = fv(f, k);
    if (s.empty()) return d;
    try { return std::stod(s); } catch (...) { return d; }
}

inline int fi(const std::map<std::string, std::string>& f, const std::string& k, int d = 0)
{
    auto s = fv(f, k);
    if (s.empty()) return d;
    try { return std::stoi(s); } catch (...) { return d; }
}

// ---- HTTP server ----
inline void startHttpApi(TradeDatabase& db, int port, std::mutex& dbMutex)
{
    httplib::Server svr;

    // ========== GET / — Dashboard ==========
    svr.Get("/", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        std::ostringstream h;
        h << std::fixed << std::setprecision(2);
        h << html::msgBanner(req) << html::errBanner(req);
        h << "<h1>Quant Trade Manager</h1>";

        double wal = db.loadWalletBalance();
        double dep = db.deployedCapital();
        auto trades = db.loadTrades();
        int buys = 0, sells = 0;
        for (const auto& t : trades) { if (t.type == TradeType::Buy) ++buys; else ++sells; }

        h << "<div class='row'>"
             "<div class='stat'><div class='lbl'>Liquid</div><div class='val'>" << wal << "</div></div>"
             "<div class='stat'><div class='lbl'>Deployed</div><div class='val'>" << dep << "</div></div>"
             "<div class='stat'><div class='lbl'>Total</div><div class='val'>" << (wal + dep) << "</div></div>"
             "<div class='stat'><div class='lbl'>Buys</div><div class='val'>" << buys << "</div></div>"
             "<div class='stat'><div class='lbl'>Sells</div><div class='val'>" << sells << "</div></div>"
             "</div>";

        if (!trades.empty())
        {
            h << "<h2>Trades</h2><table><tr><th>ID</th><th>Symbol</th><th>Type</th>"
                 "<th>Price</th><th>Qty</th><th>Cost</th><th>TP</th><th>SL</th><th>SL?</th>"
                 "<th>Sold</th><th>Rem</th></tr>";
            for (const auto& t : trades)
            {
                double sold = db.soldQuantityForParent(t.tradeId);
                bool isBuy = (t.type == TradeType::Buy);
                h << "<tr><td>" << t.tradeId << "</td>"
                  << "<td>" << html::esc(t.symbol) << "</td>"
                  << "<td class='" << (isBuy ? "buy" : "sell") << "'>" << (isBuy ? "BUY" : "SELL") << "</td>"
                  << "<td>" << t.value << "</td><td>" << t.quantity << "</td>"
                  << "<td>" << (t.value * t.quantity) << "</td>"
                  << "<td>" << t.takeProfit << "</td><td>" << t.stopLoss << "</td>"
                  << "<td class='" << (t.stopLossActive ? "on" : "off") << "'>"
                  << (t.stopLossActive ? "ON" : "OFF") << "</td>"
                  << "<td>" << sold << "</td><td>" << (t.quantity - sold) << "</td></tr>";
            }
            h << "</table>";
        }
        res.set_content(html::wrap("Dashboard", h.str()), "text/html");
    });

    // ========== GET /trades — Trades list + forms ==========
    svr.Get("/trades", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        std::ostringstream h;
        h << std::fixed << std::setprecision(2);
        h << html::msgBanner(req) << html::errBanner(req);
        h << "<h1>Trades</h1>";

        auto trades = db.loadTrades();
        if (trades.empty())
        {
            h << "<p class='empty'>(no trades)</p>";
        }
        else
        {
            h << "<table><tr><th>ID</th><th>Symbol</th><th>Type</th>"
                 "<th>Price</th><th>Qty</th><th>Cost</th><th>TP</th><th>SL</th><th>SL?</th>"
                 "<th>Sold</th><th>Rem</th><th>Actions</th></tr>";
            for (const auto& t : trades)
            {
                double sold = db.soldQuantityForParent(t.tradeId);
                bool isBuy = (t.type == TradeType::Buy);
                h << "<tr><td><a href='/horizons?tradeId=" << t.tradeId << "'>" << t.tradeId << "</a></td>"
                  << "<td>" << html::esc(t.symbol) << "</td>"
                  << "<td class='" << (isBuy ? "buy" : "sell") << "'>" << (isBuy ? "BUY" : "SELL") << "</td>"
                  << "<td>" << t.value << "</td><td>" << t.quantity << "</td>"
                  << "<td>" << (t.value * t.quantity) << "</td>"
                  << "<td>" << t.takeProfit << "</td><td>" << t.stopLoss << "</td>"
                  << "<td class='" << (t.stopLossActive ? "on" : "off") << "'>"
                  << (t.stopLossActive ? "ON" : "OFF") << "</td>"
                  << "<td>" << sold << "</td><td>" << (t.quantity - sold) << "</td>"
                  << "<td>"
                  << "<a class='btn btn-sm' href='/edit-trade?id=" << t.tradeId << "'>Edit</a> "
                  << "<form class='iform' method='POST' action='/toggle-sl'>"
                  << "<input type='hidden' name='id' value='" << t.tradeId << "'>"
                  << "<button class='btn-sm btn-warn'>SL</button></form> "
                  << "<form class='iform' method='POST' action='/delete-trade'>"
                  << "<input type='hidden' name='id' value='" << t.tradeId << "'>"
                  << "<button class='btn-sm btn-danger'>Del</button></form>"
                  << "</td></tr>";
            }
            h << "</table>";
        }

        h << "<div class='forms-row'>";

        // Add trade form
        h << "<form class='card' method='POST' action='/add-trade'>"
             "<h3>Add Trade</h3>"
             "<label>Symbol</label><input type='text' name='symbol' required><br>"
             "<label>Type</label><select name='type'><option value='Buy'>Buy</option>"
             "<option value='CoveredSell'>CoveredSell</option></select><br>"
             "<label>Price</label><input type='number' name='price' step='any' required><br>"
             "<label>Quantity</label><input type='number' name='quantity' step='any' required><br>"
             "<label>Parent ID</label><input type='number' name='parentTradeId' value='-1'><br>"
             "<button>Add Trade</button></form>";

        // Execute Buy form
        h << "<form class='card' method='POST' action='/execute-buy'>"
             "<h3>Execute Buy</h3>"
             "<label>Symbol</label><input type='text' name='symbol' required><br>"
             "<label>Price</label><input type='number' name='price' step='any' required><br>"
             "<label>Quantity</label><input type='number' name='quantity' step='any' required><br>"
             "<label>Buy Fee</label><input type='number' name='buyFee' step='any' value='0'><br>"
             "<button>Execute Buy</button></form>";

        // Execute Sell form
        h << "<form class='card' method='POST' action='/execute-sell'>"
             "<h3>Execute Sell</h3>"
             "<label>Trade ID</label><input type='number' name='tradeId' required><br>"
             "<label>Price</label><input type='number' name='price' step='any' required><br>"
             "<label>Quantity</label><input type='number' name='quantity' step='any' required><br>"
             "<button>Execute Sell</button></form>";

        h << "</div>";
        res.set_content(html::wrap("Trades", h.str()), "text/html");
    });

    // ========== POST /add-trade ==========
    svr.Post("/add-trade", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        Trade t;
        t.tradeId = db.nextTradeId();
        t.symbol = normalizeSymbol(fv(f, "symbol"));
        t.type = (fv(f, "type") == "CoveredSell") ? TradeType::CoveredSell : TradeType::Buy;
        t.value = fd(f, "price");
        t.quantity = fd(f, "quantity");
        t.parentTradeId = fi(f, "parentTradeId", -1);
        t.stopLossActive = false;
        t.shortEnabled = false;
        if (t.value <= 0 || t.quantity <= 0) { res.set_redirect("/trades?err=Price+and+quantity+must+be+positive", 303); return; }
        db.addTrade(t);
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

    // ========== POST /toggle-sl ==========
    svr.Post("/toggle-sl", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        int id = fi(f, "id");
        auto trades = db.loadTrades();
        auto* tp = db.findTradeById(trades, id);
        if (!tp) { res.set_redirect("/trades?err=Trade+not+found", 303); return; }
        tp->stopLossActive = !tp->stopLossActive;
        db.updateTrade(*tp);
        std::string state = tp->stopLossActive ? "ON" : "OFF";
        res.set_redirect("/trades?msg=SL+now+" + state + "+for+" + std::to_string(id), 303);
    });

    // ========== POST /execute-buy ==========
    svr.Post("/execute-buy", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        std::string sym = normalizeSymbol(fv(f, "symbol"));
        double price = fd(f, "price");
        double qty = fd(f, "quantity");
        double fee = fd(f, "buyFee");
        if (sym.empty() || price <= 0 || qty <= 0) { res.set_redirect("/trades?err=Invalid+buy+parameters", 303); return; }
        double walBal = db.loadWalletBalance();
        double needed = price * qty + fee;
        if (needed > walBal) { res.set_redirect("/trades?err=Insufficient+funds", 303); return; }
        int bid = db.executeBuy(sym, price, qty);
        if (fee > 0) db.withdraw(fee);
        res.set_redirect("/trades?msg=Buy+" + std::to_string(bid) + "+executed", 303);
    });

    // ========== POST /execute-sell ==========
    svr.Post("/execute-sell", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        int parentId = fi(f, "tradeId");
        double price = fd(f, "price");
        double qty = fd(f, "quantity");
        int sid = db.executeSell(parentId, price, qty);
        if (sid < 0) { res.set_redirect("/trades?err=Sell+failed", 303); return; }
        res.set_redirect("/trades?msg=Sell+" + std::to_string(sid) + "+executed", 303);
    });

    // ========== GET /wallet ==========
    svr.Get("/wallet", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        std::ostringstream h;
        h << std::fixed << std::setprecision(2);
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

    // ========== GET /portfolio ==========
    svr.Get("/portfolio", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        std::ostringstream h;
        h << std::fixed << std::setprecision(2);
        h << html::msgBanner(req);
        h << "<h1>Portfolio</h1>";
        auto trades = db.loadTrades();
        double totalCost = 0;
        int buyCount = 0, sellCount = 0;
        std::vector<std::string> seen;
        h << "<table><tr><th>Symbol</th><th>Buys</th><th>Sells</th>"
             "<th>Qty</th><th>Cost</th></tr>";
        for (const auto& t : trades)
        {
            if (std::find(seen.begin(), seen.end(), t.symbol) != seen.end()) continue;
            seen.push_back(t.symbol);
            double sc = 0, sq = 0; int b = 0, s = 0;
            for (const auto& u : trades)
            {
                if (u.symbol != t.symbol) continue;
                if (u.type == TradeType::Buy) { sc += u.value * u.quantity; sq += u.quantity; ++b; }
                else ++s;
            }
            h << "<tr><td>" << html::esc(t.symbol) << "</td><td>" << b << "</td><td>" << s
              << "</td><td>" << sq << "</td><td>" << sc << "</td></tr>";
            totalCost += sc; buyCount += b; sellCount += s;
        }
        h << "</table>";
        double wal = db.loadWalletBalance();
        h << "<div class='row'>"
             "<div class='stat'><div class='lbl'>Total Cost</div><div class='val'>" << totalCost << "</div></div>"
             "<div class='stat'><div class='lbl'>Buys</div><div class='val'>" << buyCount << "</div></div>"
             "<div class='stat'><div class='lbl'>Sells</div><div class='val'>" << sellCount << "</div></div>"
             "<div class='stat'><div class='lbl'>Wallet</div><div class='val'>" << wal << "</div></div>"
             "<div class='stat'><div class='lbl'>Total</div><div class='val'>" << (wal + totalCost) << "</div></div>"
             "</div>";
        res.set_content(html::wrap("Portfolio", h.str()), "text/html");
    });

    // ========== GET /dca ==========
    svr.Get("/dca", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        std::ostringstream h;
        h << std::fixed << std::setprecision(2);
        h << "<h1>DCA Tracker</h1>";
        auto trades = db.loadTrades();
        std::vector<std::string> seen;
        h << "<table><tr><th>Symbol</th><th>Buys</th><th>Qty</th><th>Cost</th>"
             "<th>Avg Entry</th><th>Low</th><th>High</th><th>Spread</th></tr>";
        for (const auto& t : trades)
        {
            if (t.type != TradeType::Buy) continue;
            if (std::find(seen.begin(), seen.end(), t.symbol) != seen.end()) continue;
            seen.push_back(t.symbol);
            double cost = 0, qty = 0, lo = 1e18, hi = 0; int cnt = 0;
            for (const auto& u : trades)
            {
                if (u.symbol != t.symbol || u.type != TradeType::Buy) continue;
                cost += u.value * u.quantity; qty += u.quantity;
                if (u.value < lo) lo = u.value;
                if (u.value > hi) hi = u.value;
                ++cnt;
            }
            double avg = qty != 0 ? cost / qty : 0;
            h << "<tr><td>" << html::esc(t.symbol) << "</td><td>" << cnt
              << "</td><td>" << qty << "</td><td>" << cost << "</td><td>" << avg
              << "</td><td>" << lo << "</td><td>" << hi << "</td><td>" << (hi - lo) << "</td></tr>";
        }
        h << "</table>";
        if (seen.empty()) h << "<p class='empty'>(no buy trades)</p>";
        res.set_content(html::wrap("DCA", h.str()), "text/html");
    });

    // ========== GET /profit — Profit calculator form ==========
    svr.Get("/profit", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        std::ostringstream h;
        h << std::fixed << std::setprecision(2);
        h << html::msgBanner(req) << html::errBanner(req);
        h << "<h1>Profit Calculator</h1>"
             "<form class='card' method='POST' action='/profit'><h3>Calculate</h3>"
             "<label>Trade ID</label><input type='number' name='tradeId' required><br>"
             "<label>Current Price</label><input type='number' name='currentPrice' step='any' required><br>"
             "<label>Buy Fees</label><input type='number' name='buyFees' step='any' value='0'><br>"
             "<label>Sell Fees</label><input type='number' name='sellFees' step='any' value='0'><br>"
             "<button>Calculate</button></form>";
        // show buy trades for reference
        auto trades = db.loadTrades();
        bool any = false;
        for (const auto& t : trades) if (t.type == TradeType::Buy) { any = true; break; }
        if (any)
        {
            h << "<h2>Buy Trades</h2><table><tr><th>ID</th><th>Symbol</th><th>Price</th><th>Qty</th></tr>";
            for (const auto& t : trades)
                if (t.type == TradeType::Buy)
                    h << "<tr><td>" << t.tradeId << "</td><td>" << html::esc(t.symbol)
                      << "</td><td>" << t.value << "</td><td>" << t.quantity << "</td></tr>";
            h << "</table>";
        }
        res.set_content(html::wrap("Profit", h.str()), "text/html");
    });

    // ========== POST /profit — Calculate and show result ==========
    svr.Post("/profit", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        int id = fi(f, "tradeId");
        double cur = fd(f, "currentPrice");
        double buyFees = fd(f, "buyFees");
        double sellFees = fd(f, "sellFees");
        auto trades = db.loadTrades();
        auto* tp = db.findTradeById(trades, id);

        std::ostringstream h;
        h << std::fixed << std::setprecision(2);

        if (!tp) { h << "<div class='msg err'>Trade not found</div>"; }
        else
        {
            auto r = ProfitCalculator::calculate(*tp, cur, buyFees, sellFees);
            db.saveProfitSnapshot(tp->symbol, id, cur, r);
            h << "<h1>Profit Result</h1>"
                 "<div class='row'>"
                 "<div class='stat'><div class='lbl'>Trade</div><div class='val'>#" << id << " " << html::esc(tp->symbol) << "</div></div>"
                 "<div class='stat'><div class='lbl'>Entry</div><div class='val'>" << tp->value << "</div></div>"
                 "<div class='stat'><div class='lbl'>Current</div><div class='val'>" << cur << "</div></div>"
                 "<div class='stat'><div class='lbl'>Gross</div><div class='val'>" << r.grossProfit << "</div></div>"
                 "<div class='stat'><div class='lbl'>Net</div><div class='val'>" << r.netProfit << "</div></div>"
                 "<div class='stat'><div class='lbl'>ROI</div><div class='val'>" << r.roi << "%</div></div>"
                 "</div>"
                 "<div class='msg'>Snapshot saved</div>";
        }

        h << "<br><form class='card' method='POST' action='/profit'><h3>Calculate Again</h3>"
             "<label>Trade ID</label><input type='number' name='tradeId' value='" << id << "' required><br>"
             "<label>Current Price</label><input type='number' name='currentPrice' step='any' value='" << cur << "' required><br>"
             "<label>Buy Fees</label><input type='number' name='buyFees' step='any' value='" << buyFees << "'><br>"
             "<label>Sell Fees</label><input type='number' name='sellFees' step='any' value='" << sellFees << "'><br>"
             "<button>Calculate</button></form>"
             "<a class='btn' href='/profit'>Back</a>";
        res.set_content(html::wrap("Profit Result", h.str()), "text/html");
    });

    // ========== GET /pending-exits ==========
    svr.Get("/pending-exits", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        std::ostringstream h;
        h << std::fixed << std::setprecision(2);
        h << html::msgBanner(req) << html::errBanner(req);
        h << "<h1>Pending Exits</h1>";
        auto orders = db.loadPendingExits();
        if (orders.empty()) { h << "<p class='empty'>(no pending exits)</p>"; }
        else
        {
            h << "<table><tr><th>Order</th><th>Symbol</th><th>Trade</th>"
                 "<th>Trigger</th><th>Qty</th><th>Level</th><th>Action</th></tr>";
            for (const auto& pe : orders)
            {
                h << "<tr><td>" << pe.orderId << "</td>"
                  << "<td>" << html::esc(pe.symbol) << "</td><td>" << pe.tradeId << "</td>"
                  << "<td>" << pe.triggerPrice << "</td><td>" << pe.sellQty << "</td>"
                  << "<td>" << pe.levelIndex << "</td>"
                  << "<td><form class='iform' method='POST' action='/remove-pending'>"
                  << "<input type='hidden' name='id' value='" << pe.orderId << "'>"
                  << "<button class='btn-sm btn-danger'>Remove</button></form></td></tr>";
            }
            h << "</table>";
        }
        res.set_content(html::wrap("Pending Exits", h.str()), "text/html");
    });

    // ========== POST /remove-pending ==========
    svr.Post("/remove-pending", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        int id = fi(f, "id");
        db.removePendingExit(id);
        res.set_redirect("/pending-exits?msg=Order+" + std::to_string(id) + "+removed", 303);
    });

    // ========== GET /entry-points ==========
    svr.Get("/entry-points", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        std::ostringstream h;
        h << std::fixed << std::setprecision(2);
        h << "<h1>Entry Points</h1>";
        auto pts = db.loadEntryPoints();
        if (pts.empty()) { h << "<p class='empty'>(no entry points)</p>"; }
        else
        {
            h << "<table><tr><th>ID</th><th>Symbol</th><th>Lvl</th><th>Entry</th>"
                 "<th>BE</th><th>Qty</th><th>Dir</th><th>Status</th><th>TP</th><th>SL</th></tr>";
            for (const auto& ep : pts)
            {
                h << "<tr><td>" << ep.entryId << "</td>"
                  << "<td>" << html::esc(ep.symbol) << "</td><td>" << ep.levelIndex << "</td>"
                  << "<td>" << ep.entryPrice << "</td><td>" << ep.breakEven << "</td>"
                  << "<td>" << ep.fundingQty << "</td>"
                  << "<td>" << (ep.isShort ? "SHORT" : "LONG") << "</td>"
                  << "<td>" << (ep.traded ? "TRADED" : "OPEN") << "</td>"
                  << "<td>" << ep.exitTakeProfit << "</td><td>" << ep.exitStopLoss << "</td></tr>";
            }
            h << "</table>";
        }
        res.set_content(html::wrap("Entry Points", h.str()), "text/html");
    });

    // ========== GET /profit-history ==========
    svr.Get("/profit-history", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        std::ostringstream h;
        h << std::fixed << std::setprecision(2);
        h << "<h1>Profit History</h1>";
        auto rows = db.loadProfitHistory();
        if (rows.empty()) { h << "<p class='empty'>(no history)</p>"; }
        else
        {
            h << "<table><tr><th>Trade</th><th>Symbol</th><th>Price</th>"
                 "<th>Gross</th><th>Net</th><th>ROI</th></tr>";
            for (const auto& r : rows)
            {
                h << "<tr><td>" << r.tradeId << "</td><td>" << html::esc(r.symbol)
                  << "</td><td>" << r.currentPrice << "</td><td>" << r.grossProfit
                  << "</td><td>" << r.netProfit << "</td><td>" << r.roi << "%</td></tr>";
            }
            h << "</table>";
        }
        res.set_content(html::wrap("Profit History", h.str()), "text/html");
    });

    // ========== GET /params-history ==========
    svr.Get("/params-history", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        std::ostringstream h;
        h << std::fixed << std::setprecision(4);
        h << "<h1>Parameter History</h1>";
        auto rows = db.loadParamsHistory();
        if (rows.empty()) { h << "<p class='empty'>(no history)</p>"; }
        else
        {
            h << "<table><tr><th>Type</th><th>Symbol</th><th>Trade</th>"
                 "<th>Price</th><th>Qty</th><th>Levels</th><th>BuyFee</th><th>SellFee</th>"
                 "<th>Hedging</th><th>Pump</th><th>Symbols</th><th>K</th>"
                 "<th>Spread</th><th>dT</th><th>Surplus</th><th>Risk</th></tr>";
            for (const auto& r : rows)
            {
                h << "<tr><td>" << html::esc(r.calcType) << "</td>"
                  << "<td>" << html::esc(r.symbol) << "</td>"
                  << "<td>" << r.tradeId << "</td>"
                  << "<td>" << r.currentPrice << "</td><td>" << r.quantity << "</td>"
                  << "<td>" << r.horizonCount << "</td>"
                  << "<td>" << r.buyFees << "</td><td>" << r.sellFees << "</td>"
                  << "<td>" << r.feeHedgingCoefficient << "</td>"
                  << "<td>" << r.portfolioPump << "</td><td>" << r.symbolCount << "</td>"
                  << "<td>" << r.coefficientK << "</td>"
                  << "<td>" << r.feeSpread << "</td><td>" << r.deltaTime << "</td>"
                  << "<td>" << r.surplusRate << "</td><td>" << r.riskCoefficient << "</td></tr>";
            }
            h << "</table>";
        }
        res.set_content(html::wrap("Params History", h.str()), "text/html");
    });

    // ========== GET /horizons ==========
    svr.Get("/horizons", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        std::ostringstream h;
        h << std::fixed << std::setprecision(2);
        h << "<h1>Horizon Levels</h1>"
             "<form class='card' method='GET' action='/horizons'><h3>View Horizons</h3>"
             "<label>Trade ID</label><input type='number' name='tradeId' value='"
          << req.get_param_value("tradeId") << "' required> "
             "<button>View</button></form>";

        if (req.has_param("tradeId"))
        {
            int id = 0;
            try { id = std::stoi(req.get_param_value("tradeId")); } catch (...) {}
            auto trades = db.loadTrades();
            auto* tp = db.findTradeById(trades, id);
            if (!tp) { h << "<div class='msg err'>Trade not found</div>"; }
            else
            {
                auto levels = db.loadHorizonLevels(tp->symbol, id);
                h << "<h2>" << html::esc(tp->symbol) << " #" << id
                  << " (price=" << tp->value << " qty=" << tp->quantity << ")</h2>";
                if (levels.empty()) { h << "<p class='empty'>(no horizons)</p>"; }
                else
                {
                    h << "<table><tr><th>Index</th><th>Take Profit</th><th>TP/unit</th>"
                         "<th>Stop Loss</th><th>SL/unit</th><th>SL?</th></tr>";
                    for (const auto& lv : levels)
                    {
                        double tpu = tp->quantity > 0 ? lv.takeProfit / tp->quantity : 0;
                        double slu = (tp->quantity > 0 && lv.stopLoss > 0) ? lv.stopLoss / tp->quantity : 0;
                        h << "<tr><td>" << lv.index << "</td>"
                          << "<td>" << lv.takeProfit << "</td><td>" << tpu << "</td>"
                          << "<td>" << lv.stopLoss << "</td><td>" << slu << "</td>"
                          << "<td class='" << (lv.stopLossActive ? "on" : "off") << "'>"
                          << (lv.stopLossActive ? "ON" : "OFF") << "</td></tr>";
                    }
                    h << "</table>";
                }
            }
        }
        res.set_content(html::wrap("Horizons", h.str()), "text/html");
    });

    // ========== GET /market-entry — form ==========
    svr.Get("/market-entry", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        std::ostringstream h;
        h << std::fixed << std::setprecision(2);
        h << html::msgBanner(req) << html::errBanner(req);
        double walBal = db.loadWalletBalance();
        h << "<h1>Market Entry Calculator</h1>"
             "<div class='row'>"
             "<div class='stat'><div class='lbl'>Wallet</div><div class='val'>" << walBal << "</div></div>"
             "</div><br>"
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
             "<label>Risk</label><input type='number' name='risk' step='any' value='0.5'><br>"
             "<label>Direction</label><select name='isShort'>"
             "<option value='0'>LONG</option><option value='1'>SHORT</option></select><br>"
             "<label>Funding</label><select name='fundMode'>"
             "<option value='1'>Pump only</option><option value='2'>Pump + Wallet</option></select><br>"
             "<button>Calculate</button></form>";
        res.set_content(html::wrap("Market Entry", h.str()), "text/html");
    });

    // ========== POST /market-entry — compute + show results + execute form ==========
    svr.Post("/market-entry", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        std::string sym = normalizeSymbol(fv(f, "symbol"));
        double cur = fd(f, "currentPrice");
        double qty = fd(f, "quantity");
        double risk = fd(f, "risk");
        bool isShort = (fv(f, "isShort") == "1");
        int fundMode = fi(f, "fundMode", 1);
        HorizonParams p;
        p.horizonCount = fi(f, "levels", 4);
        p.feeHedgingCoefficient = fd(f, "feeHedgingCoefficient", 1.0);
        p.portfolioPump = fd(f, "portfolioPump");
        p.symbolCount = fi(f, "symbolCount", 1);
        p.coefficientK = fd(f, "coefficientK");
        p.feeSpread = fd(f, "feeSpread");
        p.deltaTime = fd(f, "deltaTime", 1.0);
        p.surplusRate = fd(f, "surplusRate");

        double walBal = db.loadWalletBalance();
        double availableFunds = p.portfolioPump;
        if (fundMode == 2) availableFunds += walBal;

        HorizonParams entryParams = p;
        entryParams.portfolioPump = availableFunds;

        std::ostringstream h;
        h << std::fixed << std::setprecision(4);
        if (sym.empty() || cur <= 0 || qty <= 0)
        {
            h << "<div class='msg err'>Symbol, price, and quantity are required</div>";
            h << "<br><a class='btn' href='/market-entry'>Back</a>";
            res.set_content(html::wrap("Market Entry", h.str()), "text/html");
            return;
        }

        auto levels = MarketEntryCalculator::generate(cur, qty, entryParams, risk);
        double eo = MultiHorizonEngine::effectiveOverhead(cur, qty, p);
        double overhead = MultiHorizonEngine::computeOverhead(cur, qty, p);
        double posDelta = MultiHorizonEngine::positionDelta(cur, qty, p.portfolioPump);

        db.saveParamsSnapshot(
            TradeDatabase::ParamsRow::from("entry", sym, -1, cur, qty, p, risk));

        h << "<h1>Entry Strategy: " << html::esc(sym) << " @ " << std::setprecision(2) << cur << "</h1>";

        h << std::fixed << std::setprecision(4);
        h << "<div class='row'>"
             "<div class='stat'><div class='lbl'>Overhead</div><div class='val'>" << (overhead * 100) << "%</div></div>"
             "<div class='stat'><div class='lbl'>Surplus</div><div class='val'>" << (p.surplusRate * 100) << "%</div></div>"
             "<div class='stat'><div class='lbl'>Effective</div><div class='val'>" << (eo * 100) << "%</div></div>"
             "<div class='stat'><div class='lbl'>Pos Delta</div><div class='val'>" << (posDelta * 100) << "%</div></div>"
             "</div>";

        h << std::fixed << std::setprecision(2);
        h << "<div class='row'>"
             "<div class='stat'><div class='lbl'>Pump</div><div class='val'>" << p.portfolioPump << "</div></div>"
             "<div class='stat'><div class='lbl'>Wallet</div><div class='val'>" << walBal << "</div></div>"
             "<div class='stat'><div class='lbl'>Available</div><div class='val'>" << availableFunds << "</div></div>"
             "<div class='stat'><div class='lbl'>Direction</div><div class='val'>" << (isShort ? "SHORT" : "LONG") << "</div></div>"
             "</div>";

        // entry levels table with TP/SL
        h << "<h2>Entry Levels</h2>"
             "<table><tr><th>Lvl</th><th>Entry Price</th><th>Discount</th>"
             "<th>Break Even</th><th>Net Profit</th><th>Funding</th><th>Fund %</th>"
             "<th>Qty</th><th>Cost</th><th>Coverage</th>"
             "<th>Exit TP</th><th>Exit SL</th></tr>";
        for (const auto& el : levels)
        {
            double disc = cur > 0 ? ((cur - el.entryPrice) / cur * 100) : 0;
            double exitTP, exitSL;
            if (isShort) { exitTP = el.entryPrice * (1.0 - eo); exitSL = el.entryPrice * (1.0 + eo); }
            else         { exitTP = el.entryPrice * (1.0 + eo); exitSL = el.entryPrice * (1.0 - eo); }
            double cost = el.entryPrice * el.fundingQty;

            h << "<tr><td>" << el.index << "</td>"
              << "<td>" << el.entryPrice << "</td>"
              << "<td>" << disc << "%</td>"
              << "<td>" << el.breakEven << "</td>"
              << "<td>" << el.potentialNet << "</td>"
              << "<td>" << el.funding << "</td>"
              << "<td>" << (el.fundingFraction * 100) << "%</td>"
              << "<td>" << el.fundingQty << "</td>"
              << "<td>" << cost << "</td>"
              << "<td>" << el.costCoverage << "x</td>"
              << "<td class='buy'>" << exitTP << "</td>"
              << "<td class='sell'>" << exitSL << "</td></tr>";
        }
        h << "</table>";

        // execute form — hidden params + per-level buy fee inputs
        h << "<h2>Execute Entry Strategy</h2>"
             "<form class='card' method='POST' action='/execute-entries'>"
             "<h3>Enter buy fees per level, then confirm</h3>"
             "<input type='hidden' name='symbol' value='" << html::esc(sym) << "'>"
             "<input type='hidden' name='currentPrice' value='" << cur << "'>"
             "<input type='hidden' name='quantity' value='" << qty << "'>"
             "<input type='hidden' name='risk' value='" << risk << "'>"
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
             "<table><tr><th>Lvl</th><th>Entry</th><th>Qty</th><th>Cost</th><th>Buy Fee</th></tr>";
        for (const auto& el : levels)
        {
            if (el.fundingQty <= 0) continue;
            double cost = el.entryPrice * el.fundingQty;
            h << "<tr><td>" << el.index << "</td>"
              << "<td>" << el.entryPrice << "</td>"
              << "<td>" << el.fundingQty << "</td>"
              << "<td>" << cost << "</td>"
              << "<td><input type='number' name='fee_" << el.index << "' step='any' value='0' style='width:80px;'></td></tr>";
        }
        h << "</table>"
             "<button>Execute All Entries</button>"
             "</form>";

        h << "<br><a class='btn' href='/market-entry'>Back</a>";
        res.set_content(html::wrap("Market Entry Results", h.str()), "text/html");
    });

    // ========== POST /execute-entries — execute buy orders from market entry ==========
    svr.Post("/execute-entries", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        std::string sym = normalizeSymbol(fv(f, "symbol"));
        double cur = fd(f, "currentPrice");
        double qty = fd(f, "quantity");
        double risk = fd(f, "risk");
        bool isShort = (fv(f, "isShort") == "1");
        int fundMode = fi(f, "fundMode", 1);
        HorizonParams p;
        p.horizonCount = fi(f, "levels", 4);
        p.feeHedgingCoefficient = fd(f, "feeHedgingCoefficient", 1.0);
        p.portfolioPump = fd(f, "portfolioPump");
        p.symbolCount = fi(f, "symbolCount", 1);
        p.coefficientK = fd(f, "coefficientK");
        p.feeSpread = fd(f, "feeSpread");
        p.deltaTime = fd(f, "deltaTime", 1.0);
        p.surplusRate = fd(f, "surplusRate");

        double walBal = db.loadWalletBalance();
        double availableFunds = p.portfolioPump;
        if (fundMode == 2) availableFunds += walBal;

        HorizonParams entryParams = p;
        entryParams.portfolioPump = availableFunds;

        if (sym.empty() || cur <= 0 || qty <= 0)
        {
            res.set_redirect("/market-entry?err=Invalid+parameters", 303);
            return;
        }

        auto levels = MarketEntryCalculator::generate(cur, qty, entryParams, risk);
        double eo = MultiHorizonEngine::effectiveOverhead(cur, qty, p);

        // deposit pump capital into wallet
        if (p.portfolioPump > 0)
        {
            db.deposit(p.portfolioPump);
        }

        // build entry points and execute buys
        int nextEpId = db.nextEntryId();
        std::vector<TradeDatabase::EntryPoint> entryPoints;

        std::ostringstream h;
        h << std::fixed << std::setprecision(2);
        h << "<h1>Entry Execution: " << html::esc(sym) << "</h1>";
        if (p.portfolioPump > 0)
            h << "<div class='msg'>Deposited pump " << p.portfolioPump
              << " into wallet. Balance: " << db.loadWalletBalance() << "</div>";

        int executed = 0, skipped = 0;
        h << "<table><tr><th>Lvl</th><th>Entry</th><th>Qty</th><th>Fee</th>"
             "<th>Trade</th><th>Exit TP</th><th>Exit SL</th><th>Status</th></tr>";

        for (size_t i = 0; i < levels.size(); ++i)
        {
            const auto& el = levels[i];
            double buyQty = el.fundingQty;
            if (buyQty <= 0) continue;

            double buyFee = fd(f, "fee_" + std::to_string(el.index));
            double cost = el.entryPrice * buyQty;

            double exitTP, exitSL;
            if (isShort) { exitTP = el.entryPrice * (1.0 - eo); exitSL = el.entryPrice * (1.0 + eo); }
            else         { exitTP = el.entryPrice * (1.0 + eo); exitSL = el.entryPrice * (1.0 - eo); }

            TradeDatabase::EntryPoint ep;
            ep.symbol = sym;
            ep.entryId = nextEpId++;
            ep.levelIndex = el.index;
            ep.entryPrice = el.entryPrice;
            ep.breakEven = el.breakEven;
            ep.funding = el.funding;
            ep.fundingQty = el.fundingQty;
            ep.effectiveOverhead = eo;
            ep.isShort = isShort;
            ep.exitTakeProfit = exitTP;
            ep.exitStopLoss = exitSL;

            double curBal = db.loadWalletBalance();
            double totalNeeded = cost + buyFee;

            if (totalNeeded > curBal)
            {
                double maxQty = (el.entryPrice > 0) ? (curBal - buyFee) / el.entryPrice : 0;
                if (maxQty <= 0)
                {
                    h << "<tr><td>" << el.index << "</td><td>" << el.entryPrice
                      << "</td><td>" << buyQty << "</td><td>" << buyFee
                      << "</td><td>-</td><td>-</td><td>-</td>"
                      << "<td class='sell'>SKIPPED (need " << totalNeeded
                      << ", have " << curBal << ")</td></tr>";
                    ++skipped;
                    entryPoints.push_back(ep);
                    continue;
                }
                buyQty = maxQty;
                cost = el.entryPrice * buyQty;
            }

            int bid = db.executeBuy(sym, el.entryPrice, buyQty);
            if (buyFee > 0) db.withdraw(buyFee);

            ep.traded = true;
            ep.linkedTradeId = bid;

            // set TP/SL on the created trade
            auto trades = db.loadTrades();
            auto* tradePtr = db.findTradeById(trades, bid);
            if (tradePtr)
            {
                tradePtr->takeProfit = exitTP * tradePtr->quantity;
                tradePtr->stopLoss = exitSL * tradePtr->quantity;
                tradePtr->stopLossActive = false;
                db.updateTrade(*tradePtr);
            }

            h << "<tr><td>" << el.index << "</td><td>" << el.entryPrice
              << "</td><td>" << buyQty << "</td><td>" << buyFee
              << "</td><td class='buy'>#" << bid << "</td>"
              << "<td>" << exitTP << "</td><td>" << exitSL << "</td>"
              << "<td class='buy'>OK</td></tr>";
            ++executed;
            entryPoints.push_back(ep);
        }
        h << "</table>";

        // save all entry points
        auto existingEp = db.loadEntryPoints();
        for (const auto& ep : entryPoints) existingEp.push_back(ep);
        db.saveEntryPoints(existingEp);

        h << "<div class='row'>"
             "<div class='stat'><div class='lbl'>Executed</div><div class='val'>" << executed << "</div></div>"
             "<div class='stat'><div class='lbl'>Skipped</div><div class='val'>" << skipped << "</div></div>"
             "<div class='stat'><div class='lbl'>Entry Points</div><div class='val'>" << entryPoints.size() << "</div></div>"
             "<div class='stat'><div class='lbl'>Wallet</div><div class='val'>" << db.loadWalletBalance() << "</div></div>"
             "</div>";
        h << "<div class='msg'>" << executed << " buy order(s) executed. "
          << entryPoints.size() << " entry point(s) saved.</div>";

        h << "<br><a class='btn' href='/trades'>View Trades</a> "
             "<a class='btn' href='/entry-points'>Entry Points</a> "
             "<a class='btn' href='/market-entry'>New Entry</a>";
        res.set_content(html::wrap("Entry Executed", h.str()), "text/html");
    });

    // ========== GET /exit-strategy — form ==========
    svr.Get("/exit-strategy", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        std::ostringstream h;
        h << std::fixed << std::setprecision(2);
        h << html::msgBanner(req) << html::errBanner(req);
        h << "<h1>Exit Strategy Calculator</h1>"
             "<form class='card' method='POST' action='/exit-strategy'><h3>Parameters</h3>"
             "<label>Trade IDs</label><input type='text' name='tradeIds' placeholder='1,2,3' required><br>"
             "<label>Risk</label><input type='number' name='risk' step='any' value='0.5'><br>"
             "<label>Exit Fraction</label><input type='number' name='exitFraction' step='any' value='1.0'><br>"
             "<label>Steepness</label><input type='number' name='steepness' step='any' value='4.0'><br>"
             "<label>Fee Hedging</label><input type='number' name='feeHedgingCoefficient' step='any' value='1'><br>"
             "<label>Symbol Count</label><input type='number' name='symbolCount' value='1'><br>"
             "<label>Coefficient K</label><input type='number' name='coefficientK' step='any' value='0'><br>"
             "<label>Fee Spread</label><input type='number' name='feeSpread' step='any' value='0'><br>"
             "<label>Delta Time</label><input type='number' name='deltaTime' step='any' value='1'><br>"
             "<label>Surplus Rate</label><input type='number' name='surplusRate' step='any' value='0.02'><br>"
             "<button>Calculate</button></form>";

        auto trades = db.loadTrades();
        bool any = false;
        for (const auto& t : trades) if (t.type == TradeType::Buy) { any = true; break; }
        if (any)
        {
            h << "<h2>Buy Trades</h2><table><tr><th>ID</th><th>Symbol</th><th>Price</th>"
                 "<th>Qty</th><th>Sold</th><th>Pending</th><th>Remaining</th></tr>";
            auto pending = db.loadPendingExits();
            for (const auto& t : trades)
            {
                if (t.type != TradeType::Buy) continue;
                double sold = db.soldQuantityForParent(t.tradeId);
                double pQty = 0;
                for (const auto& pe : pending)
                    if (pe.tradeId == t.tradeId) pQty += pe.sellQty;
                h << "<tr><td>" << t.tradeId << "</td><td>" << html::esc(t.symbol) << "</td>"
                  << "<td>" << t.value << "</td><td>" << t.quantity << "</td>"
                  << "<td>" << sold << "</td><td>" << pQty << "</td>"
                  << "<td>" << (t.quantity - sold - pQty) << "</td></tr>";
            }
            h << "</table>";
        }
        res.set_content(html::wrap("Exit Strategy", h.str()), "text/html");
    });

    // ========== POST /exit-strategy — show results + confirm form ==========
    svr.Post("/exit-strategy", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        std::string idsStr = fv(f, "tradeIds");
        double risk = fd(f, "risk");
        double exitFrac = fd(f, "exitFraction", 1.0);
        double steep = fd(f, "steepness", 4.0);
        HorizonParams p;
        p.horizonCount = 1;
        p.feeHedgingCoefficient = fd(f, "feeHedgingCoefficient", 1.0);
        p.symbolCount = fi(f, "symbolCount", 1);
        p.coefficientK = fd(f, "coefficientK");
        p.feeSpread = fd(f, "feeSpread");
        p.deltaTime = fd(f, "deltaTime", 1.0);
        p.surplusRate = fd(f, "surplusRate");

        // parse trade IDs
        std::vector<int> ids;
        {
            std::istringstream ss(idsStr);
            std::string tok;
            while (std::getline(ss, tok, ','))
            {
                try { ids.push_back(std::stoi(tok)); } catch (...) {}
            }
        }

        std::ostringstream h;
        h << std::fixed << std::setprecision(2);

        if (ids.empty()) { h << "<div class='msg err'>No trade IDs entered</div>"; }
        else
        {
            auto trades = db.loadTrades();
            auto existingPending = db.loadPendingExits();

            // levelCounter for unique hidden field naming across all trades
            int levelCounter = 0;
            bool anyLevels = false;
            std::vector<std::string> exitSymbols;

            h << "<h1>Exit Strategy</h1>";

            // confirm form wraps everything — hidden params + per-level fee inputs
            h << "<form class='card' method='POST' action='/confirm-exits'>"
                 "<input type='hidden' name='tradeIds' value='" << html::esc(idsStr) << "'>"
                 "<input type='hidden' name='risk' value='" << risk << "'>"
                 "<input type='hidden' name='exitFraction' value='" << exitFrac << "'>"
                 "<input type='hidden' name='steepness' value='" << steep << "'>"
                 "<input type='hidden' name='feeHedgingCoefficient' value='" << p.feeHedgingCoefficient << "'>"
                 "<input type='hidden' name='symbolCount' value='" << p.symbolCount << "'>"
                 "<input type='hidden' name='coefficientK' value='" << p.coefficientK << "'>"
                 "<input type='hidden' name='feeSpread' value='" << p.feeSpread << "'>"
                 "<input type='hidden' name='deltaTime' value='" << p.deltaTime << "'>"
                 "<input type='hidden' name='surplusRate' value='" << p.surplusRate << "'>";

            for (int id : ids)
            {
                auto* tp = db.findTradeById(trades, id);
                if (!tp || tp->type != TradeType::Buy)
                {
                    h << "<div class='msg err'>#" << id << " is not a Buy trade</div>";
                    continue;
                }

                double sold = db.soldQuantityForParent(tp->tradeId);
                double pendingQty = 0;
                for (const auto& pe : existingPending)
                    if (pe.tradeId == tp->tradeId) pendingQty += pe.sellQty;
                double remaining = tp->quantity - sold - pendingQty;

                if (remaining <= 0)
                {
                    h << "<div class='msg err'>#" << id << " " << html::esc(tp->symbol)
                      << " fully committed (sold=" << sold << " pending=" << pendingQty << ")</div>";
                    continue;
                }

                Trade tempTrade = *tp;
                tempTrade.quantity = remaining;
                auto levels = ExitStrategyCalculator::generate(tempTrade, p, risk, exitFrac, steep);
                double clampedFrac = (exitFrac < 0) ? 0 : (exitFrac > 1) ? 1 : exitFrac;
                double sellableQty = remaining * clampedFrac;

                h << "<h2>Exit #" << tp->tradeId << " " << html::esc(tp->symbol)
                  << " (entry=" << tp->value << ")</h2>"
                     "<div class='row'>"
                     "<div class='stat'><div class='lbl'>Total</div><div class='val'>" << tp->quantity << "</div></div>"
                     "<div class='stat'><div class='lbl'>Sold</div><div class='val'>" << sold << "</div></div>"
                     "<div class='stat'><div class='lbl'>Pending</div><div class='val'>" << pendingQty << "</div></div>"
                     "<div class='stat'><div class='lbl'>Available</div><div class='val'>" << remaining << "</div></div>"
                     "<div class='stat'><div class='lbl'>Selling</div><div class='val'>" << sellableQty << "</div></div>"
                     "</div>";

                h << "<table><tr><th>Lvl</th><th>TP Price</th><th>Sell Qty</th>"
                     "<th>Fraction</th><th>Value</th><th>Gross</th>"
                     "<th>Buy Fee</th><th>Sell Fee</th></tr>";
                std::ostringstream hiddenFields;
                hiddenFields << std::fixed << std::setprecision(10);
                for (const auto& el : levels)
                {
                    if (el.sellQty <= 0) continue;
                    double pct = tp->value > 0 ? ((el.tpPrice - tp->value) / tp->value * 100) : 0;
                    h << "<tr><td>" << el.index << "</td>"
                      << "<td>" << el.tpPrice << " (+" << pct << "%)</td>"
                      << "<td>" << el.sellQty << "</td>"
                      << "<td>" << (el.sellFraction * 100) << "%</td>"
                      << "<td>" << el.sellValue << "</td>"
                      << "<td>" << el.grossProfit << "</td>"
                      << "<td><input type='number' name='buyFee_" << levelCounter
                      << "' step='any' value='0' style='width:80px;'></td>"
                      << "<td><input type='number' name='sellFee_" << levelCounter
                      << "' step='any' value='0' style='width:80px;'></td></tr>";
                    // hidden fields emitted after the table to keep HTML valid
                    hiddenFields << "<input type='hidden' name='tid_" << levelCounter << "' value='" << tp->tradeId << "'>"
                      << "<input type='hidden' name='sym_" << levelCounter << "' value='" << html::esc(tp->symbol) << "'>"
                      << "<input type='hidden' name='tp_" << levelCounter << "' value='" << el.tpPrice << "'>"
                      << "<input type='hidden' name='qty_" << levelCounter << "' value='" << el.sellQty << "'>"
                      << "<input type='hidden' name='gross_" << levelCounter << "' value='" << el.grossProfit << "'>"
                      << "<input type='hidden' name='lvl_" << levelCounter << "' value='" << el.index << "'>";
                    ++levelCounter;
                    anyLevels = true;
                }
                h << "</table>";
                h << hiddenFields.str();
                if (std::find(exitSymbols.begin(), exitSymbols.end(), tp->symbol) == exitSymbols.end())
                    exitSymbols.push_back(tp->symbol);
                h << "<div style='color:#484f58;font-size:0.82em;'>Remaining after exit: "
                  << (remaining - sellableQty) << "</div>";

                db.saveParamsSnapshot(
                    TradeDatabase::ParamsRow::from("exit", tp->symbol, tp->tradeId,
                                                   tp->value, tp->quantity, p, risk));
            }

            h << "<input type='hidden' name='levelCount' value='" << levelCounter << "'>";

            if (anyLevels)
            {
                h << "<br><h3 style='color:#f0883e;'>Confirm Exits</h3>";
                for (const auto& es : exitSymbols)
                {
                    h << "<label>" << html::esc(es) << " Price</label>"
                      << "<input type='number' name='price_" << html::esc(es)
                      << "' step='any' required><br>";
                }
                h << "<br><button>Confirm &amp; Execute</button>";
            }
            h << "</form>";
        }
        h << "<br><a class='btn' href='/exit-strategy'>Back</a>";
        res.set_content(html::wrap("Exit Results", h.str()), "text/html");
    });

    // ========== POST /confirm-exits — execute triggered + save pending ==========
    svr.Post("/confirm-exits", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        int levelCount = fi(f, "levelCount");

        // per-symbol price lookup
        auto priceFor = [&](const std::string& sym) -> double {
            return fd(f, "price_" + sym, 0.0);
        };

        std::ostringstream h;
        h << std::fixed << std::setprecision(2);
        h << "<h1>Exit Execution</h1>";

        if (levelCount <= 0)
        {
            h << "<div class='msg err'>Invalid parameters</div>";
            h << "<br><a class='btn' href='/exit-strategy'>Back</a>";
            res.set_content(html::wrap("Exit Execution", h.str()), "text/html");
            return;
        }

        // collect all exit orders with per-level fees
        struct ExitOrder {
            int tradeId; std::string symbol; double triggerPrice; double sellQty;
            double grossProfit; double buyFee; double sellFee; double netProfit; int levelIndex;
        };
        std::vector<ExitOrder> allOrders;
        double totalNet = 0;

        for (int i = 0; i < levelCount; ++i)
        {
            std::string si = std::to_string(i);
            ExitOrder eo;
            eo.tradeId = fi(f, "tid_" + si);
            eo.symbol = fv(f, "sym_" + si);
            eo.triggerPrice = fd(f, "tp_" + si);
            eo.sellQty = fd(f, "qty_" + si);
            eo.grossProfit = fd(f, "gross_" + si);
            eo.buyFee = fd(f, "buyFee_" + si);
            eo.sellFee = fd(f, "sellFee_" + si);
            eo.levelIndex = fi(f, "lvl_" + si);
            eo.netProfit = eo.grossProfit - eo.buyFee - eo.sellFee;
            totalNet += eo.netProfit;
            allOrders.push_back(eo);
        }

        // net profit summary
        h << "<h2>Fee Summary</h2>"
             "<table><tr><th>Trade</th><th>Symbol</th><th>Lvl</th><th>Trigger</th>"
             "<th>Qty</th><th>Gross</th><th>Buy Fee</th><th>Sell Fee</th><th>Net</th></tr>";
        for (const auto& eo : allOrders)
        {
            h << "<tr><td>" << eo.tradeId << "</td>"
              << "<td>" << html::esc(eo.symbol) << "</td>"
              << "<td>" << eo.levelIndex << "</td>"
              << "<td>" << eo.triggerPrice << "</td>"
              << "<td>" << eo.sellQty << "</td>"
              << "<td>" << eo.grossProfit << "</td>"
              << "<td>" << eo.buyFee << "</td>"
              << "<td>" << eo.sellFee << "</td>"
              << "<td class='" << (eo.netProfit >= 0 ? "buy" : "sell") << "'>" << eo.netProfit << "</td></tr>";
        }
        h << "</table>";
        // collect unique symbols and display prices
        std::vector<std::string> exitSyms;
        for (const auto& eo : allOrders)
            if (std::find(exitSyms.begin(), exitSyms.end(), eo.symbol) == exitSyms.end())
                exitSyms.push_back(eo.symbol);

        h << "<div class='row'>"
             "<div class='stat'><div class='lbl'>Total Net</div><div class='val'>"
          << totalNet << "</div></div>";
        for (const auto& es : exitSyms)
        {
            h << "<div class='stat'><div class='lbl'>" << html::esc(es)
              << "</div><div class='val'>" << priceFor(es) << "</div></div>";
        }
        h << "</div>";

        // split into triggered (symbol price >= trigger) vs pending
        std::vector<ExitOrder> hitOrders, pendingOrders;
        for (const auto& eo : allOrders)
        {
            double cp = priceFor(eo.symbol);
            if (cp > 0 && cp >= eo.triggerPrice)
                hitOrders.push_back(eo);
            else
                pendingOrders.push_back(eo);
        }

        // execute triggered exits immediately
        int executed = 0;
        if (!hitOrders.empty())
        {
            h << "<h2>Executed (" << hitOrders.size() << " at/above trigger)</h2>"
                 "<table><tr><th>Trade</th><th>Symbol</th><th>Trigger</th>"
                 "<th>Qty</th><th>Sell ID</th><th>Status</th></tr>";
            for (const auto& eo : hitOrders)
            {
                int sid = db.executeSell(eo.tradeId, eo.triggerPrice, eo.sellQty);
                if (sid >= 0)
                {
                    h << "<tr><td>" << eo.tradeId << "</td>"
                      << "<td>" << html::esc(eo.symbol) << "</td>"
                      << "<td>" << eo.triggerPrice << "</td>"
                      << "<td>" << eo.sellQty << "</td>"
                      << "<td class='buy'>#" << sid << "</td>"
                      << "<td class='buy'>OK</td></tr>";
                    ++executed;
                }
                else
                {
                    h << "<tr><td>" << eo.tradeId << "</td>"
                      << "<td>" << html::esc(eo.symbol) << "</td>"
                      << "<td>" << eo.triggerPrice << "</td>"
                      << "<td>" << eo.sellQty << "</td>"
                      << "<td>-</td>"
                      << "<td class='sell'>FAILED</td></tr>";
                }
            }
            h << "</table>";
        }

        // save remaining as pending exits
        int saved = 0;
        if (!pendingOrders.empty())
        {
            int nextId = db.nextPendingId();
            std::vector<TradeDatabase::PendingExit> peList;
            for (const auto& eo : pendingOrders)
            {
                TradeDatabase::PendingExit pe;
                pe.symbol = eo.symbol;
                pe.orderId = nextId++;
                pe.tradeId = eo.tradeId;
                pe.triggerPrice = eo.triggerPrice;
                pe.sellQty = eo.sellQty;
                pe.levelIndex = eo.levelIndex;
                peList.push_back(pe);
            }
            db.addPendingExits(peList);
            saved = (int)peList.size();

            h << "<h2>Pending (" << saved << " below trigger)</h2>"
                 "<table><tr><th>Order</th><th>Trade</th><th>Symbol</th>"
                 "<th>Trigger</th><th>Qty</th><th>Away</th></tr>";
            for (const auto& pe : peList)
            {
                h << "<tr><td>" << pe.orderId << "</td>"
                  << "<td>" << pe.tradeId << "</td>"
                  << "<td>" << html::esc(pe.symbol) << "</td>"
                  << "<td>" << pe.triggerPrice << "</td>"
                  << "<td>" << pe.sellQty << "</td>"
                  << "<td>" << (pe.triggerPrice - priceFor(pe.symbol)) << "</td></tr>";
            }
            h << "</table>";
        }

        h << "<div class='row'>"
             "<div class='stat'><div class='lbl'>Executed</div><div class='val'>" << executed << "</div></div>"
             "<div class='stat'><div class='lbl'>Pending</div><div class='val'>" << saved << "</div></div>"
             "<div class='stat'><div class='lbl'>Wallet</div><div class='val'>" << db.loadWalletBalance() << "</div></div>"
             "</div>";
        h << "<div class='msg'>" << executed << " exit(s) executed, "
          << saved << " pending exit(s) saved.</div>";

        h << "<br><a class='btn' href='/trades'>Trades</a> "
             "<a class='btn' href='/pending-exits'>Pending Exits</a> "
             "<a class='btn' href='/exit-strategy'>New Exit</a>";
        res.set_content(html::wrap("Exit Execution", h.str()), "text/html");
    });

    // ========== GET /edit-trade ==========
    svr.Get("/edit-trade", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        std::ostringstream h;
        h << std::fixed << std::setprecision(2);
        h << html::msgBanner(req) << html::errBanner(req);
        int id = 0;
        try { id = std::stoi(req.get_param_value("id")); } catch (...) {}
        auto trades = db.loadTrades();
        auto* tp = db.findTradeById(trades, id);
        if (!tp) { h << "<h1>Edit Trade</h1><div class='msg err'>Trade not found</div>"; }
        else
        {
            h << "<h1>Edit Trade #" << tp->tradeId << "</h1>"
                 "<form class='card' method='POST' action='/edit-trade'>"
                 "<input type='hidden' name='id' value='" << tp->tradeId << "'>"
                 "<label>Symbol</label><input type='text' name='symbol' value='" << html::esc(tp->symbol) << "'><br>"
                 "<label>Type</label><select name='type'>"
                 "<option value='Buy'" << (tp->type == TradeType::Buy ? " selected" : "") << ">Buy</option>"
                 "<option value='CoveredSell'" << (tp->type == TradeType::CoveredSell ? " selected" : "") << ">CoveredSell</option>"
                 "</select><br>"
                 "<label>Price</label><input type='number' name='price' step='any' value='" << tp->value << "'><br>"
                 "<label>Quantity</label><input type='number' name='quantity' step='any' value='" << tp->quantity << "'><br>"
                 "<label>Parent ID</label><input type='number' name='parentTradeId' value='" << tp->parentTradeId << "'><br>"
                 "<label>Take Profit</label><input type='number' name='takeProfit' step='any' value='" << tp->takeProfit << "'><br>"
                 "<label>Stop Loss</label><input type='number' name='stopLoss' step='any' value='" << tp->stopLoss << "'><br>"
                 "<label>SL Active</label><select name='stopLossActive'>"
                 "<option value='0'" << (!tp->stopLossActive ? " selected" : "") << ">OFF</option>"
                 "<option value='1'" << (tp->stopLossActive ? " selected" : "") << ">ON</option>"
                 "</select><br><br>"
                 "<button>Save Changes</button></form>";
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
        tp->parentTradeId = pid;
        tp->takeProfit = fd(f, "takeProfit", tp->takeProfit);
        tp->stopLoss = fd(f, "stopLoss", tp->stopLoss);
        auto slStr = fv(f, "stopLossActive");
        if (!slStr.empty()) tp->stopLossActive = (slStr == "1");
        db.updateTrade(*tp);
        res.set_redirect("/trades?msg=Trade+" + std::to_string(id) + "+updated", 303);
    });

    // ========== GET /generate-horizons ==========
    svr.Get("/generate-horizons", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        std::ostringstream h;
        h << std::fixed << std::setprecision(2);
        h << html::msgBanner(req) << html::errBanner(req);
        h << "<h1>Generate TP/SL Horizons</h1>";

        auto trades = db.loadTrades();
        bool any = false;
        for (const auto& t : trades) if (t.type == TradeType::Buy) { any = true; break; }

        h << "<form class='card' method='POST' action='/generate-horizons'><h3>Shared Parameters</h3>"
             "<label>Symbol</label><input type='text' name='symbol' required><br>"
             "<label>Trade IDs</label><input type='text' name='tradeIds' placeholder='1,2,3 or 0=all' value='0'><br>"
             "<label>Fee Hedging</label><input type='number' name='feeHedgingCoefficient' step='any' value='1'><br>"
             "<label>Pump</label><input type='number' name='portfolioPump' step='any' value='0'><br>"
             "<label>Symbol Count</label><input type='number' name='symbolCount' value='1'><br>"
             "<label>Coefficient K</label><input type='number' name='coefficientK' step='any' value='0'><br>"
             "<label>Fee Spread</label><input type='number' name='feeSpread' step='any' value='0'><br>"
             "<label>Delta Time</label><input type='number' name='deltaTime' step='any' value='1'><br>"
             "<label>Surplus Rate</label><input type='number' name='surplusRate' step='any' value='0.02'><br>"
             "<label>Stop Losses</label><select name='generateStopLosses'>"
             "<option value='0'>No</option><option value='1'>Yes</option></select><br>";

        if (any)
        {
            h << "<h3 style='margin-top:12px;'>Per-Trade Fees</h3>"
                 "<table><tr><th>ID</th><th>Symbol</th><th>Price</th>"
                 "<th>Qty</th><th>Cost</th><th>TP</th><th>SL</th>"
                 "<th>Buy Fee</th><th>Sell Fee</th></tr>";
            for (const auto& t : trades)
            {
                if (t.type != TradeType::Buy) continue;
                h << "<tr><td>" << t.tradeId << "</td><td>" << html::esc(t.symbol)
                  << "</td><td>" << t.value << "</td><td>" << t.quantity
                  << "</td><td>" << (t.value * t.quantity) << "</td><td>" << t.takeProfit
                  << "</td><td>" << t.stopLoss << "</td>"
                  << "<td><input type='number' name='buyFee_" << t.tradeId
                  << "' step='any' value='0' style='width:80px;'></td>"
                  << "<td><input type='number' name='sellFee_" << t.tradeId
                  << "' step='any' value='0' style='width:80px;'></td></tr>";
            }
            h << "</table>";
        }
        h << "<button>Generate</button></form>";
        res.set_content(html::wrap("Generate Horizons", h.str()), "text/html");
    });

    // ========== POST /generate-horizons ==========
    svr.Post("/generate-horizons", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        std::string sym = normalizeSymbol(fv(f, "symbol"));
        std::string idsStr = fv(f, "tradeIds", "0");
        HorizonParams baseP;
        baseP.horizonCount = 1;
        baseP.feeHedgingCoefficient = fd(f, "feeHedgingCoefficient", 1.0);
        baseP.portfolioPump = fd(f, "portfolioPump");
        baseP.symbolCount = fi(f, "symbolCount", 1);
        baseP.coefficientK = fd(f, "coefficientK");
        baseP.feeSpread = fd(f, "feeSpread");
        baseP.deltaTime = fd(f, "deltaTime", 1.0);
        baseP.surplusRate = fd(f, "surplusRate");
        baseP.generateStopLosses = (fv(f, "generateStopLosses") == "1");

        auto trades = db.loadTrades();

        // parse selected IDs
        bool selectAll = (idsStr == "0" || idsStr.empty());
        std::vector<int> selectedIds;
        if (!selectAll)
        {
            std::istringstream ss(idsStr);
            std::string tok;
            while (std::getline(ss, tok, ','))
            {
                try { selectedIds.push_back(std::stoi(tok)); } catch (...) {}
            }
        }

        std::vector<Trade*> buyTrades;
        for (auto& t : trades)
        {
            if (t.symbol != sym || t.type != TradeType::Buy) continue;
            if (selectAll || std::find(selectedIds.begin(), selectedIds.end(), t.tradeId) != selectedIds.end())
                buyTrades.push_back(&t);
        }

        std::ostringstream h;
        h << std::fixed << std::setprecision(2);

        if (buyTrades.empty())
        {
            h << "<div class='msg err'>No Buy trades found for " << html::esc(sym) << "</div>";
        }
        else
        {
            h << "<h1>Horizons for " << html::esc(sym) << "</h1>";
            for (auto* bt : buyTrades)
            {
                // per-trade fees from form fields
                HorizonParams tp = baseP;
                tp.buyFees = fd(f, "buyFee_" + std::to_string(bt->tradeId));
                tp.sellFees = fd(f, "sellFee_" + std::to_string(bt->tradeId));

                auto levels = MultiHorizonEngine::generate(*bt, tp);
                MultiHorizonEngine::applyFirstHorizon(*bt, levels, false);
                db.updateTrade(*bt);
                db.saveHorizonLevels(sym, bt->tradeId, levels);
                db.saveParamsSnapshot(
                    TradeDatabase::ParamsRow::from("horizon", sym, bt->tradeId,
                                                   bt->value, bt->quantity, tp));

                double overhead = MultiHorizonEngine::computeOverhead(*bt, tp);
                double eo = MultiHorizonEngine::effectiveOverhead(*bt, tp);
                h << "<h2>Trade #" << bt->tradeId
                  << " (price=" << bt->value << " qty=" << bt->quantity
                  << " buyFee=" << tp.buyFees << " sellFee=" << tp.sellFees << ")</h2>"
                  << "<div class='row'>"
                     "<div class='stat'><div class='lbl'>Overhead</div><div class='val'>"
                  << std::fixed << std::setprecision(4) << (overhead * 100) << "%</div></div>"
                     "<div class='stat'><div class='lbl'>Effective</div><div class='val'>"
                  << (eo * 100) << "%</div></div></div>"
                  << std::fixed << std::setprecision(2);
                h << "<table><tr><th>Lvl</th><th>Take Profit</th><th>TP/unit</th>"
                     "<th>Stop Loss</th><th>SL/unit</th><th>SL?</th></tr>";
                for (const auto& lv : levels)
                {
                    double tpu = bt->quantity > 0 ? lv.takeProfit / bt->quantity : 0;
                    double slu = (bt->quantity > 0 && lv.stopLoss > 0) ? lv.stopLoss / bt->quantity : 0;
                    h << "<tr><td>" << lv.index << "</td>"
                      << "<td>" << lv.takeProfit << "</td><td>" << tpu << "</td>"
                      << "<td>" << lv.stopLoss << "</td><td>" << slu << "</td>"
                      << "<td class='" << (lv.stopLossActive ? "on" : "off") << "'>"
                      << (lv.stopLossActive ? "ON" : "OFF") << "</td></tr>";
                }
                h << "</table>";
            }
            h << "<div class='msg'>Generated and saved for " << buyTrades.size() << " trade(s). SL is OFF by default.</div>";
        }
        h << "<br><a class='btn' href='/generate-horizons'>Back</a> "
             "<a class='btn' href='/trades'>Trades</a>";
        res.set_content(html::wrap("Horizon Results", h.str()), "text/html");
    });

    // ========== GET /price-check ==========
    svr.Get("/price-check", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        std::ostringstream h;
        h << std::fixed << std::setprecision(2);
        h << html::msgBanner(req) << html::errBanner(req);
        h << "<h1>Price Check (TP/SL vs Market)</h1>";

        auto trades = db.loadTrades();
        std::vector<std::string> symbols;
        for (const auto& t : trades)
            if (t.type == TradeType::Buy &&
                std::find(symbols.begin(), symbols.end(), t.symbol) == symbols.end())
                symbols.push_back(t.symbol);

        if (symbols.empty())
        {
            h << "<p class='empty'>(no Buy trades)</p>";
        }
        else
        {
            h << "<form class='card' method='POST' action='/price-check'>"
                 "<h3>Enter current market prices</h3>";
            for (const auto& sym : symbols)
            {
                h << "<label>" << html::esc(sym) << "</label>"
                  << "<input type='number' name='price_" << html::esc(sym)
                  << "' step='any' required><br>";
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

        // collect symbol prices from form
        std::vector<std::string> symbols;
        for (const auto& t : trades)
            if (t.type == TradeType::Buy &&
                std::find(symbols.begin(), symbols.end(), t.symbol) == symbols.end())
                symbols.push_back(t.symbol);

        auto priceFor = [&](const std::string& sym) -> double {
            return fd(f, "price_" + sym, 0.0);
        };

        std::ostringstream h;
        h << std::fixed << std::setprecision(2);
        h << "<h1>Price Check Results</h1>";

        // re-show the price form pre-filled
        h << "<form class='card' method='POST' action='/price-check'>"
             "<h3>Update prices</h3>";
        for (const auto& sym : symbols)
        {
            double p = priceFor(sym);
            h << "<label>" << html::esc(sym) << "</label>"
              << "<input type='number' name='price_" << html::esc(sym)
              << "' step='any' value='" << p << "' required><br>";
        }
        h << "<br><button>Check</button></form>";

        // results table
        h << "<table><tr><th>ID</th><th>Symbol</th><th>Entry</th><th>Qty</th>"
             "<th>Market</th><th>Gross P&amp;L</th>"
             "<th>TP Price</th><th>TP?</th><th>SL Price</th><th>SL?</th></tr>";
        struct Trigger { int id; std::string sym; double price; double qty; std::string tag; };
        std::vector<Trigger> triggers;

        for (const auto& t : trades)
        {
            if (t.type != TradeType::Buy) continue;
            double remaining = t.quantity - db.soldQuantityForParent(t.tradeId);
            if (remaining <= 0) continue;
            double cur = priceFor(t.symbol);
            if (cur <= 0) continue;
            double gross = (cur - t.value) * remaining;
            double tpPrice = 0, slPrice = 0;
            bool tpHit = false, slHit = false;
            if (t.takeProfit > 0) { tpPrice = t.takeProfit / t.quantity; tpHit = (cur >= tpPrice); }
            if (t.stopLossActive && t.stopLoss > 0) { slPrice = t.stopLoss / t.quantity; slHit = (cur <= slPrice); }

            h << "<tr><td>" << t.tradeId << "</td>"
              << "<td>" << html::esc(t.symbol) << "</td>"
              << "<td>" << t.value << "</td><td>" << remaining << "</td>"
              << "<td>" << cur << "</td>"
              << "<td class='" << (gross >= 0 ? "buy" : "sell") << "'>" << gross << "</td>"
              << "<td>" << tpPrice << "</td>"
              << "<td class='" << (tpHit ? "buy" : "off") << "'>" << (tpHit ? "HIT" : "-") << "</td>"
              << "<td>" << slPrice << "</td>"
              << "<td class='" << (slHit ? "sell" : "off") << "'>" << (slHit ? "BREACHED" : "-") << "</td></tr>";

            // horizon levels sub-rows
            auto levels = db.loadHorizonLevels(t.symbol, t.tradeId);
            for (const auto& lv : levels)
            {
                double htp = t.quantity > 0 ? lv.takeProfit / t.quantity : 0;
                double hsl = (t.quantity > 0 && lv.stopLoss > 0) ? lv.stopLoss / t.quantity : 0;
                bool htpHit = (cur >= htp);
                bool hslHit = (lv.stopLossActive && hsl > 0 && cur <= hsl);
                h << "<tr style='color:#8b949e;'><td></td><td>[" << lv.index << "]</td>"
                  << "<td></td><td></td><td></td><td></td>"
                  << "<td>" << htp << "</td>"
                  << "<td class='" << (htpHit ? "buy" : "off") << "'>" << (htpHit ? "HIT" : (htp > 0 ? std::to_string(htp - cur).substr(0, 8) + " away" : "-")) << "</td>"
                  << "<td>" << hsl << "</td>"
                  << "<td class='" << (hslHit ? "sell" : "off") << "'>";
                if (hsl > 0 && lv.stopLossActive)
                    h << (hslHit ? "BREACHED" : "OK");
                else if (hsl > 0)
                    h << "OFF";
                else
                    h << "-";
                h << "</td></tr>";
            }

            if (tpHit) triggers.push_back({t.tradeId, t.symbol, cur, remaining, "TP"});
            if (slHit) triggers.push_back({t.tradeId, t.symbol, cur, remaining, "SL"});
        }
        h << "</table>";

        // pending exits check
        auto pending = db.loadPendingExits();
        std::vector<TradeDatabase::PendingExit> triggered;
        for (const auto& pe : pending)
        {
            double cur = priceFor(pe.symbol);
            if (cur > 0 && cur >= pe.triggerPrice)
                triggered.push_back(pe);
        }
        if (!triggered.empty())
        {
            h << "<h2>Pending Exits Triggered</h2>"
                 "<table><tr><th>Order</th><th>Symbol</th><th>Trade</th>"
                 "<th>Trigger</th><th>Market</th><th>Qty</th></tr>";
            for (const auto& pe : triggered)
            {
                h << "<tr><td>" << pe.orderId << "</td>"
                  << "<td>" << html::esc(pe.symbol) << "</td><td>" << pe.tradeId << "</td>"
                  << "<td>" << pe.triggerPrice << "</td>"
                  << "<td>" << priceFor(pe.symbol) << "</td>"
                  << "<td>" << pe.sellQty << "</td></tr>";
            }
            h << "</table>";
        }

        if (!triggers.empty())
        {
            h << "<h2>TP/SL Triggers</h2>"
                 "<table><tr><th>Tag</th><th>ID</th><th>Symbol</th><th>Qty</th><th>Market</th></tr>";
            for (const auto& tr : triggers)
                h << "<tr><td class='" << (tr.tag == "TP" ? "buy" : "sell") << "'>" << tr.tag << "</td>"
                  << "<td>" << tr.id << "</td><td>" << html::esc(tr.sym) << "</td>"
                  << "<td>" << tr.qty << "</td><td>" << tr.price << "</td></tr>";
            h << "</table>";
        }

        h << "<br><a class='btn' href='/price-check'>Back</a> "
             "<a class='btn' href='/trades'>Trades</a>";
        res.set_content(html::wrap("Price Check Results", h.str()), "text/html");
    });

    // ========== GET /wipe ==========
    svr.Get("/wipe", [&](const httplib::Request& req, httplib::Response& res) {
        std::ostringstream h;
        h << html::msgBanner(req);
        h << "<h1>Wipe Database</h1>"
             "<p style='color:#f85149;'>This will delete ALL trades, wallet balance, "
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

    std::cout << "  [HTTP] listening on http://localhost:" << port << "\n";
    svr.listen("0.0.0.0", port);
}
