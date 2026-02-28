#pragma once

#include "AppContext.h"
#include "HtmlHelpers.h"
#include "MarketEntryCalculator.h"
#include <mutex>
#include <cmath>
#include <limits>
#include <chrono>

// Build SerialParams from parsed form data + wallet balance.
inline QuantMath::SerialParams buildSerialParams(
    const std::map<std::string, std::string>& f,
    double walBal)
{
    auto fd = [&](const std::string& k, double def = 0.0) -> double {
        auto it = f.find(k); if (it == f.end() || it->second.empty()) return def;
        try { return std::stod(it->second); } catch (...) { return def; }
    };
    auto fi = [&](const std::string& k, int def = 0) -> int {
        auto it = f.find(k); if (it == f.end() || it->second.empty()) return def;
        try { return std::stoi(it->second); } catch (...) { return def; }
    };
    auto fv = [&](const std::string& k) -> std::string {
        auto it = f.find(k); return (it != f.end()) ? it->second : "";
    };

    QuantMath::SerialParams sp;
    sp.currentPrice          = fd("currentPrice");
    sp.quantity              = fd("quantity", 1.0);
    sp.levels                = fi("levels", 4);
    sp.steepness             = fd("steepness", 6.0);
    sp.risk                  = fd("risk", 0.5);
    sp.isShort               = (fv("isShort") == "1");
    sp.rangeAbove            = fd("rangeAbove");
    sp.rangeBelow            = fd("rangeBelow");
    sp.feeSpread             = fd("feeSpread");
    sp.feeHedgingCoefficient = fd("feeHedgingCoefficient", 1.0);
    sp.deltaTime             = fd("deltaTime", 1.0);
    sp.symbolCount           = fi("symbolCount", 1);
    sp.coefficientK          = fd("coefficientK");
    sp.surplusRate           = fd("surplusRate");
    sp.futureTradeCount      = fi("futureTradeCount", 0);
    sp.maxRisk               = fd("maxRisk");
    sp.minRisk               = fd("minRisk");
    sp.generateStopLosses    = (fv("generateStopLosses") == "1");
    sp.stopLossFraction      = fd("stopLossFraction", 1.0);
    sp.stopLossHedgeCount    = fi("stopLossHedgeCount", 0);
    sp.downtrendCount        = fi("downtrendCount", 1);
    sp.savingsRate           = fd("savingsRate", 0.0);

    double pump = fd("portfolioPump");
    int fundMode = fi("fundMode", 1);
    sp.availableFunds = pump;
    if (fundMode == 2) sp.availableFunds += walBal;

    return sp;
}

inline void registerApiRoutes(httplib::Server& svr, AppContext& ctx)
{
    auto& db = ctx.defaultDb;
    auto& dbMutex = ctx.dbMutex;

    // ========== JSON API: GET /api/trades ==========
    svr.Get("/api/trades", [&](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto trades = db.loadTrades();
        std::ostringstream j;
        j << std::fixed << std::setprecision(17) << "[";
        bool first = true;
        for (const auto& t : trades)
        {
            if (!first) j << ",";
            first = false;
            double sold = db.soldQuantityForParent(t.tradeId);
            bool isBuy = (t.type == TradeType::Buy);
            double tpPrice = (isBuy && t.quantity > 0 && t.takeProfit > 0) ? t.takeProfit / t.quantity : 0;
            double slPrice = (isBuy && t.quantity > 0 && t.stopLoss > 0) ? t.stopLoss / t.quantity : 0;
            j << "{\"id\":" << t.tradeId
              << ",\"symbol\":\"" << t.symbol << "\""
              << ",\"type\":\"" << (isBuy ? "Buy" : "Sell") << "\""
              << ",\"price\":" << t.value
              << ",\"qty\":" << t.quantity
              << ",\"sold\":" << sold
              << ",\"remaining\":" << (t.quantity - sold)
              << ",\"buyFee\":" << t.buyFee
              << ",\"sellFee\":" << t.sellFee
              << ",\"tp\":" << t.takeProfit
              << ",\"sl\":" << t.stopLoss
              << ",\"tpPrice\":" << tpPrice
              << ",\"slPrice\":" << slPrice
              << ",\"slActive\":" << (t.stopLossActive ? "true" : "false")
              << ",\"parentId\":" << t.parentTradeId
              << ",\"timestamp\":" << t.timestamp
              << "}";
        }
        j << "]";
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(j.str(), "application/json");
    });

    // ========== JSON API: GET /api/entry-points ==========
    svr.Get("/api/entry-points", [&](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto pts = db.loadEntryPoints();
        std::ostringstream j;
        j << std::fixed << std::setprecision(17) << "[";
        bool first = true;
        for (const auto& ep : pts)
        {
            if (!first) j << ",";
            first = false;
            j << "{\"id\":" << ep.entryId
              << ",\"symbol\":\"" << ep.symbol << "\""
              << ",\"level\":" << ep.levelIndex
              << ",\"entry\":" << ep.entryPrice
              << ",\"breakEven\":" << ep.breakEven
              << ",\"funding\":" << ep.funding
              << ",\"qty\":" << ep.fundingQty
              << ",\"tp\":" << ep.exitTakeProfit
              << ",\"sl\":" << ep.exitStopLoss
              << ",\"isShort\":" << (ep.isShort ? "true" : "false")
              << ",\"traded\":" << (ep.traded ? "true" : "false")
              << ",\"linkedTrade\":" << ep.linkedTradeId
              << "}";
        }
        j << "]";
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(j.str(), "application/json");
    });

    // ========== JSON API: GET /api/pending-exits ==========
    svr.Get("/api/pending-exits", [&](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto orders = db.loadPendingExits();
        std::ostringstream j;
        j << std::fixed << std::setprecision(17) << "[";
        bool first = true;
        for (const auto& pe : orders)
        {
            if (!first) j << ",";
            first = false;
            j << "{\"orderId\":" << pe.orderId
              << ",\"symbol\":\"" << pe.symbol << "\""
              << ",\"tradeId\":" << pe.tradeId
              << ",\"trigger\":" << pe.triggerPrice
              << ",\"qty\":" << pe.sellQty
              << ",\"level\":" << pe.levelIndex
              << "}";
        }
        j << "]";
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(j.str(), "application/json");
    });

    // ========== JSON API: GET /api/horizons?tradeId=N ==========
    svr.Get("/api/horizons", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        int id = 0;
        try { id = std::stoi(req.get_param_value("tradeId")); } catch (...) {}
        auto trades = db.loadTrades();
        auto* tp = db.findTradeById(trades, id);
        std::ostringstream j;
        j << std::fixed << std::setprecision(17) << "[";
        if (tp)
        {
            auto levels = db.loadHorizonLevels(tp->symbol, id);
            bool first = true;
            for (const auto& lv : levels)
            {
                if (!first) j << ",";
                first = false;
                double tpu = tp->quantity > 0 ? lv.takeProfit / tp->quantity : 0;
                double slu = (tp->quantity > 0 && lv.stopLoss > 0) ? lv.stopLoss / tp->quantity : 0;
                j << "{\"index\":" << lv.index
                  << ",\"tp\":" << lv.takeProfit
                  << ",\"tpPrice\":" << tpu
                  << ",\"sl\":" << lv.stopLoss
                  << ",\"slPrice\":" << slu
                  << ",\"slActive\":" << (lv.stopLossActive ? "true" : "false")
                  << "}";
            }
        }
        j << "]";
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(j.str(), "application/json");
    });

    // ========== JSON API: POST /api/calc/entry � live entry calculation ==========
    svr.Post("/api/calc/entry", [&](const httplib::Request& req, httplib::Response& res) {
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

        std::ostringstream j;
        j << std::fixed << std::setprecision(17);
        if (sym.empty() || cur <= 0 || qty <= 0) { j << "{\"error\":true}"; }
        else
        {
            auto levels = MarketEntryCalculator::generate(cur, qty, entryParams, risk, steepness, rangeAbove, rangeBelow);
            double oh = QuantMath::overhead(cur, qty, p.feeSpread, p.feeHedgingCoefficient,
                p.deltaTime, p.symbolCount, p.portfolioPump, p.coefficientK, p.futureTradeCount);
            double eo = QuantMath::effectiveOverhead(oh, p.surplusRate, p.feeSpread,
                p.feeHedgingCoefficient, p.deltaTime);
            double tpRef = (rangeAbove > 0.0 || rangeBelow > 0.0) ? cur + rangeAbove : cur;

            j << "{\"symbol\":\"" << sym << "\",\"currentPrice\":" << cur
              << ",\"overhead\":" << oh << ",\"effective\":" << eo
              << ",\"isShort\":" << (isShort ? "true" : "false")
              << ",\"levels\":[";
            bool first = true;
            for (const auto& el : levels)
            {
                if (!first) j << ",";
                first = false;
                double exitTP = QuantMath::levelTP(el.entryPrice, oh, eo, p.maxRisk, p.minRisk, risk, isShort, steepness, el.index, p.horizonCount, tpRef);
                double exitSL = QuantMath::levelSL(el.entryPrice, eo, isShort);
                j << "{\"index\":" << el.index
                  << ",\"entry\":" << el.entryPrice
                  << ",\"breakEven\":" << el.breakEven
                  << ",\"tp\":" << exitTP
                  << ",\"sl\":" << exitSL
                  << ",\"funding\":" << el.funding
                  << ",\"fundPct\":" << (el.fundingFraction * 100)
                  << ",\"qty\":" << el.fundingQty
                  << ",\"net\":" << el.potentialNet
                  << "}";
            }
            j << "]}";
        }
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(j.str(), "application/json");
    });

    // ========== JSON API: POST /api/calc/serial — live serial calculation ==========
    svr.Post("/api/calc/serial", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        std::string sym = normalizeSymbol(fv(f, "symbol"));
        auto sp = buildSerialParams(f, db.loadWalletBalance());

        std::ostringstream j;
        j << std::fixed << std::setprecision(17);
        if (sym.empty() || sp.currentPrice <= 0 || sp.quantity <= 0) { j << "{\"error\":true}"; }
        else
        {
            auto plan = QuantMath::generateSerialPlan(sp);

            j << "{\"symbol\":\"" << sym << "\",\"currentPrice\":" << sp.currentPrice
              << ",\"overhead\":" << plan.overhead << ",\"effective\":" << plan.effectiveOH
              << ",\"dtBuffer\":" << plan.dtBuffer
              << ",\"slBuffer\":" << plan.slBuffer
              << ",\"isShort\":" << (sp.isShort ? "true" : "false")
              << ",\"levels\":[";
            bool first = true;
            for (const auto& e : plan.entries)
            {
                if (!first) j << ",";
                first = false;
                j << "{\"index\":" << e.index
                  << ",\"entry\":" << e.entryPrice
                  << ",\"breakEven\":" << e.breakEven
                  << ",\"tp\":" << e.tpUnit
                  << ",\"sl\":" << (sp.generateStopLosses ? e.slUnit : 0.0)
                  << ",\"funding\":" << e.funding
                  << ",\"fundPct\":" << (e.fundFrac * 100)
                  << ",\"qty\":" << e.fundQty
                  << "}";
            }
            j << "]}";
        }
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(j.str(), "application/json");
    });

    // ========== JSON API: POST /api/calc/chain — multi-cycle chain preview ==========
    svr.Post("/api/calc/chain", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        auto sp = buildSerialParams(f, db.loadWalletBalance());
        int chainCycles = fi(f, "chainCycles", 3);
        if (chainCycles < 1) chainCycles = 1;
        if (chainCycles > 10) chainCycles = 10;

        std::ostringstream j;
        j << std::fixed << std::setprecision(17);

        if (sp.currentPrice <= 0 || sp.availableFunds <= 0) {
            j << "{\"error\":true}";
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_content(j.str(), "application/json");
            return;
        }

        auto chain = QuantMath::generateChain(sp, chainCycles);

        j << "{\"currentPrice\":" << sp.currentPrice
          << ",\"overhead\":" << chain.initialOverhead
          << ",\"effective\":" << chain.initialEffective
          << ",\"cycles\":[";

        for (size_t ci = 0; ci < chain.cycles.size(); ++ci)
        {
            if (ci > 0) j << ",";
            const auto& cc = chain.cycles[ci];

            j << "{\"cycle\":" << cc.cycle
              << ",\"capital\":" << cc.capital
              << ",\"overhead\":" << cc.plan.overhead
              << ",\"effective\":" << cc.plan.effectiveOH
              << ",\"slFrac\":" << cc.plan.slFraction
              << ",\"levels\":[";

            for (size_t i = 0; i < cc.plan.entries.size(); ++i)
            {
                const auto& e = cc.plan.entries[i];
                if (i > 0) j << ",";
                j << "{\"index\":" << e.index
                  << ",\"entry\":" << e.entryPrice
                  << ",\"breakEven\":" << e.breakEven
                  << ",\"tp\":" << e.tpUnit
                  << ",\"sl\":" << (sp.generateStopLosses ? e.slUnit : 0.0)
                  << ",\"funding\":" << e.funding
                  << ",\"fundPct\":" << (e.fundFrac * 100)
                  << ",\"qty\":" << e.fundQty
                  << "}";
            }

            j << "],\"profit\":" << cc.result.grossProfit
              << ",\"savings\":" << cc.result.savingsAmount
              << ",\"capitalAfter\":" << cc.result.nextCycleFunds
              << "}";
        }

        j << "]}";

        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(j.str(), "application/json");
    });

    // ========== JSON API: GET /api/param-models ==========
    svr.Get("/api/param-models", [&](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto models = db.loadParamModels();
        std::ostringstream j;
        j << std::fixed << std::setprecision(17) << "[";
        bool first = true;
        for (const auto& m : models)
        {
            if (!first) j << ",";
            first = false;
            j << "{\"name\":\"" << html::esc(m.name) << "\""
              << ",\"levels\":" << m.levels
              << ",\"risk\":" << m.risk
              << ",\"steepness\":" << m.steepness
              << ",\"feeHedgingCoefficient\":" << m.feeHedgingCoefficient
              << ",\"portfolioPump\":" << m.portfolioPump
              << ",\"symbolCount\":" << m.symbolCount
              << ",\"coefficientK\":" << m.coefficientK
              << ",\"feeSpread\":" << m.feeSpread
              << ",\"deltaTime\":" << m.deltaTime
              << ",\"surplusRate\":" << m.surplusRate
              << ",\"maxRisk\":" << m.maxRisk
              << ",\"minRisk\":" << m.minRisk
              << ",\"isShort\":" << (m.isShort ? "true" : "false")
              << ",\"fundMode\":" << m.fundMode
              << ",\"generateStopLosses\":" << (m.generateStopLosses ? "true" : "false")
              << ",\"rangeAbove\":" << m.rangeAbove
              << ",\"rangeBelow\":" << m.rangeBelow
              << ",\"futureTradeCount\":" << m.futureTradeCount
              << ",\"stopLossFraction\":" << m.stopLossFraction
              << ",\"stopLossHedgeCount\":" << m.stopLossHedgeCount
              << "}";
        }
        j << "]";
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(j.str(), "application/json");
    });

    // ========== JSON API: POST /api/param-models � save a model ==========
    svr.Post("/api/param-models", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        try {
        auto f = parseForm(req.body);
        TradeDatabase::ParamModel m;
        m.name = fv(f, "name");
        if (m.name.empty()) { res.status = 400; res.set_content("{\"error\":\"name required\"}", "application/json"); return; }
        m.levels = fi(f, "levels", 4);
        m.risk = fd(f, "risk", 0.5);
        m.steepness = fd(f, "steepness", 6.0);
        m.feeHedgingCoefficient = fd(f, "feeHedgingCoefficient", 1.0);
        m.portfolioPump = fd(f, "portfolioPump");
        m.symbolCount = fi(f, "symbolCount", 1);
        m.coefficientK = fd(f, "coefficientK");
        m.feeSpread = fd(f, "feeSpread");
        m.deltaTime = fd(f, "deltaTime", 1.0);
        m.surplusRate = fd(f, "surplusRate");
        m.maxRisk = fd(f, "maxRisk");
        m.minRisk = fd(f, "minRisk");
        m.isShort = (fv(f, "isShort") == "1");
        m.fundMode = fi(f, "fundMode", 1);
        m.generateStopLosses = (fv(f, "generateStopLosses") == "1");
        m.rangeAbove = fd(f, "rangeAbove");
        m.rangeBelow = fd(f, "rangeBelow");
        m.futureTradeCount = fi(f, "futureTradeCount", 0);
        m.stopLossFraction = fd(f, "stopLossFraction", 1.0);
        m.stopLossHedgeCount = fi(f, "stopLossHedgeCount", 0);
        db.addParamModel(m);
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content("{\"ok\":true}", "application/json");
        } catch (const std::exception& ex) {
            res.status = 500;
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_content(std::string("{\"error\":\"") + ex.what() + "\"}", "application/json");
        }
    });

    // ========== JSON API: DELETE /api/param-models � delete a model ==========
    svr.Delete("/api/param-models", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        std::string name;
        if (req.has_param("name")) name = req.get_param_value("name");
        if (name.empty()) { res.status = 400; res.set_content("{\"error\":\"name required\"}", "application/json"); return; }
        db.removeParamModel(name);
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content("{\"ok\":true}", "application/json");
    });

    // ========== JSON API: GET /api/pnl ==========
    svr.Get("/api/pnl", [&](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto entries = db.loadPnl();
        std::ostringstream j;
        j << std::fixed << std::setprecision(17) << "[";
        bool first = true;
        for (const auto& e : entries)
        {
            if (!first) j << ",";
            first = false;
            j << "{\"ts\":" << e.timestamp
              << ",\"symbol\":\"" << e.symbol << "\""
              << ",\"sellId\":" << e.sellTradeId
              << ",\"parentId\":" << e.parentTradeId
              << ",\"entry\":" << e.entryPrice
              << ",\"sell\":" << e.sellPrice
              << ",\"qty\":" << e.quantity
              << ",\"gross\":" << e.grossProfit
              << ",\"net\":" << e.netProfit
              << ",\"cum\":" << e.cumProfit
              << "}";
        }
        j << "]";
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(j.str(), "application/json");
    });

    // ========== GET /param-models — HTML management page ==========
    svr.Get("/param-models", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        std::ostringstream h;
        h << std::fixed << std::setprecision(17);
        h << html::msgBanner(req) << html::errBanner(req);
        h << "<h1>Parameter Models</h1>";
        auto models = db.loadParamModels();
        if (models.empty()) { h << "<p class='empty'>(no saved models)</p>"; }
        else
        {
            h << "<table><tr><th>Name</th><th>Levels</th><th>Risk</th><th>Steepness</th>"
                 "<th>Hedging</th><th>Pump</th><th>Spread</th><th>dT</th>"
                 "<th>Surplus</th><th>MaxRisk</th><th>MinRisk</th>"
                 "<th>Dir</th><th>Range&uarr;</th><th>Range&darr;</th><th>Actions</th></tr>";
            for (const auto& m : models)
            {
                h << "<tr><td>" << html::esc(m.name) << "</td>"
                  << "<td>" << m.levels << "</td>"
                  << "<td>" << m.risk << "</td>"
                  << "<td>" << m.steepness << "</td>"
                  << "<td>" << m.feeHedgingCoefficient << "</td>"
                  << "<td>" << m.portfolioPump << "</td>"
                  << "<td>" << m.feeSpread << "</td>"
                  << "<td>" << m.deltaTime << "</td>"
                  << "<td>" << m.surplusRate << "</td>"
                  << "<td>" << m.maxRisk << "</td>"
                  << "<td>" << m.minRisk << "</td>"
                  << "<td>" << (m.isShort ? "SHORT" : "LONG") << "</td>"
                  << "<td>" << m.rangeAbove << "</td>"
                  << "<td>" << m.rangeBelow << "</td>"
                  << "<td><form class='iform' method='POST' action='/delete-param-model'>"
                  << "<input type='hidden' name='name' value='" << html::esc(m.name) << "'>"
                  << "<button class='btn-sm btn-danger'>Del</button></form></td></tr>";
            }
            h << "</table>";
        }
        h << "<br><a class='btn' href='/chart'>Chart</a>";
        res.set_content(html::wrap("Parameter Models", h.str()), "text/html");
    });

    // ========== POST /delete-param-model ==========
    svr.Post("/delete-param-model", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        std::string name = fv(f, "name");
        db.removeParamModel(name);
        res.set_redirect("/param-models?msg=Model+" + name + "+deleted", 303);
    });

    // ========== JSON API: POST /api/calc/heatmap � TP sensitivity trellis ==========
    // Sweeps axisX � axisY as primary grid.  axis3 / axis4 (optional, "none" to
    // skip) add facet dimensions � each combination produces a sub-heatmap tile
    // arranged in a small-multiples layout.  When a base value is 0 the multiplier
    // is treated as an absolute value so the sweep is still meaningful.
    svr.Post("/api/calc/heatmap", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);

        double price    = fd(f, "currentPrice");
        double baseQty  = fd(f, "quantity");
        int    gridN    = fi(f, "gridSize", 12);
        if (gridN < 2) gridN = 2;
        if (gridN > 40) gridN = 40;

        HorizonParams p;
        p.feeHedgingCoefficient = fd(f, "feeHedgingCoefficient", 1.0);
        p.portfolioPump         = fd(f, "portfolioPump");
        p.symbolCount           = fi(f, "symbolCount", 1);
        p.coefficientK          = fd(f, "coefficientK");
        p.feeSpread             = fd(f, "feeSpread");
        p.deltaTime             = fd(f, "deltaTime", 1.0);
        p.surplusRate           = fd(f, "surplusRate");

        std::string axisX = fv(f, "axisX");
        std::string axisY = fv(f, "axisY");
        std::string axis3 = fv(f, "axis3");
        std::string axis4 = fv(f, "axis4");
        if (axisX.empty()) axisX = "pump";
        if (axisY.empty()) axisY = "qty";
        if (axis3.empty() || axis3 == "none") axis3 = "none";
        if (axis4.empty() || axis4 == "none") axis4 = "none";

        double xLo = fd(f, "xLo", 0.1);
        double xHi = fd(f, "xHi", 3.0);
        double yLo = fd(f, "yLo", 0.1);
        double yHi = fd(f, "yHi", 3.0);

        int    z3Steps = (axis3 != "none") ? fi(f, "z3Steps", 3) : 1;
        double z3Lo    = fd(f, "z3Lo", 0.1);
        double z3Hi    = fd(f, "z3Hi", 3.0);
        int    z4Steps = (axis4 != "none") ? fi(f, "z4Steps", 3) : 1;
        double z4Lo    = fd(f, "z4Lo", 0.1);
        double z4Hi    = fd(f, "z4Hi", 3.0);

        if (z3Steps < 1) z3Steps = 1; if (z3Steps > 6) z3Steps = 6;
        if (z4Steps < 1) z4Steps = 1; if (z4Steps > 6) z4Steps = 6;

        int totalSlices = z3Steps * z4Steps;
        if (totalSlices > 9 && gridN > 6)       gridN = 6;
        else if (totalSlices > 4 && gridN > 8)   gridN = 8;
        else if (totalSlices > 1 && gridN > 10)  gridN = 10;

        auto getBase = [&](const std::string& axis) -> double {
            if (axis == "pump")       return p.portfolioPump;
            if (axis == "qty")        return baseQty;
            if (axis == "feeSpread")  return p.feeSpread;
            if (axis == "feeHedging") return p.feeHedgingCoefficient;
            if (axis == "deltaTime")  return p.deltaTime;
            if (axis == "surplus")    return p.surplusRate;
            if (axis == "coeffK")     return p.coefficientK;
            if (axis == "symbols")    return static_cast<double>(p.symbolCount);
            return 0.0;
        };

        auto applyVal = [](HorizonParams& hp, double& qty,
                           const std::string& axis, double val)
        {
            if      (axis == "pump")       hp.portfolioPump         = val;
            else if (axis == "qty")        qty                      = val;
            else if (axis == "feeSpread")  hp.feeSpread             = val;
            else if (axis == "feeHedging") hp.feeHedgingCoefficient = val;
            else if (axis == "deltaTime")  hp.deltaTime             = val;
            else if (axis == "surplus")    hp.surplusRate            = val;
            else if (axis == "coeffK")     hp.coefficientK           = val;
            else if (axis == "symbols")    hp.symbolCount = std::max(1, static_cast<int>(val));
        };

        auto scaled = [](double base, double mul) -> double {
            return (std::abs(base) > 1e-18) ? base * mul : mul;
        };

        double baseX = getBase(axisX);
        double baseY = getBase(axisY);
        double base3 = (axis3 != "none") ? getBase(axis3) : 0.0;
        double base4 = (axis4 != "none") ? getBase(axis4) : 0.0;

        std::ostringstream j;
        j << std::fixed << std::setprecision(8);

        if (price <= 0) {
            j << "{\"error\":true}";
        } else {
            j << "{\"gridSize\":" << gridN
              << ",\"axisX\":\"" << axisX << "\""
              << ",\"axisY\":\"" << axisY << "\""
              << ",\"axis3\":\"" << axis3 << "\""
              << ",\"axis4\":\"" << axis4 << "\""
              << ",\"baseX\":" << baseX
              << ",\"baseY\":" << baseY
              << ",\"base3\":" << base3
              << ",\"base4\":" << base4
              << ",\"xLo\":" << xLo << ",\"xHi\":" << xHi
              << ",\"yLo\":" << yLo << ",\"yHi\":" << yHi
              << ",\"z3Steps\":" << z3Steps
              << ",\"z3Lo\":" << z3Lo << ",\"z3Hi\":" << z3Hi
              << ",\"z4Steps\":" << z4Steps
              << ",\"z4Lo\":" << z4Lo << ",\"z4Hi\":" << z4Hi
              << ",\"slices\":[";

            bool firstSlice = true;
            for (int z4i = 0; z4i < z4Steps; ++z4i)
            {
                double z4Mul = (z4Steps > 1)
                    ? z4Lo + (z4Hi - z4Lo) * static_cast<double>(z4i) / (z4Steps - 1)
                    : 1.0;
                for (int z3i = 0; z3i < z3Steps; ++z3i)
                {
                    double z3Mul = (z3Steps > 1)
                        ? z3Lo + (z3Hi - z3Lo) * static_cast<double>(z3i) / (z3Steps - 1)
                        : 1.0;
                    if (!firstSlice) j << ",";
                    firstSlice = false;
                    j << "{\"z3m\":" << z3Mul
                      << ",\"z3v\":" << scaled(base3, z3Mul)
                      << ",\"z4m\":" << z4Mul
                      << ",\"z4v\":" << scaled(base4, z4Mul)
                      << ",\"rows\":[";
                    for (int yi = 0; yi < gridN; ++yi)
                    {
                        double yMul = (gridN > 1)
                            ? yLo + (yHi - yLo) * static_cast<double>(yi) / (gridN - 1)
                            : yLo;
                        if (yi > 0) j << ",";
                        j << "[";
                        for (int xi = 0; xi < gridN; ++xi)
                        {
                            double xMul = (gridN > 1)
                                ? xLo + (xHi - xLo) * static_cast<double>(xi) / (gridN - 1)
                                : xLo;
                            HorizonParams hp = p;
                            double qty = baseQty;
                            applyVal(hp, qty, axisX, scaled(baseX, xMul));
                            applyVal(hp, qty, axisY, scaled(baseY, yMul));
                            if (axis3 != "none") applyVal(hp, qty, axis3, scaled(base3, z3Mul));
                            if (axis4 != "none") applyVal(hp, qty, axis4, scaled(base4, z4Mul));
                            if (qty < 1e-18) qty = 1e-18;
                            double oh = QuantMath::overhead(price, qty, hp.feeSpread,
                                hp.feeHedgingCoefficient, hp.deltaTime, hp.symbolCount,
                                hp.portfolioPump, hp.coefficientK, hp.futureTradeCount);
                            double eo = QuantMath::effectiveOverhead(oh, hp.surplusRate,
                                hp.feeSpread, hp.feeHedgingCoefficient, hp.deltaTime);
                            if (xi > 0) j << ",";
                            j << "{\"xm\":" << xMul
                              << ",\"ym\":" << yMul
                              << ",\"xv\":" << scaled(baseX, xMul)
                              << ",\"yv\":" << scaled(baseY, yMul)
                              << ",\"eo\":" << (eo * 100)
                              << ",\"oh\":" << (oh * 100)
                              << "}";
                        }
                        j << "]";
                    }
                    j << "]}";
                }
            }
            j << "]}";
        }
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(j.str(), "application/json");
    });

    // ========== JSON API: GET /api/symbols ==========
    svr.Get("/api/symbols", [&](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        std::ostringstream j;
        j << "[";
        bool first = true;
        for (const auto& s : ctx.symbols.all())
        {
            if (!first) j << ",";
            first = false;
            j << "{\"id\":" << s.id << ",\"name\":\"" << s.name << "\"}";
        }
        j << "]";
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(j.str(), "application/json");
    });

    // ========== JSON API: GET /api/prices?symbol=BTC ==========
    svr.Get("/api/prices", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        std::string sym = req.has_param("symbol") ? req.get_param_value("symbol") : "";
        std::ostringstream j;
        j << std::fixed << std::setprecision(17);
        if (sym.empty())
        {
            // Return latest for all symbols
            j << "{";
            bool first = true;
            for (const auto& s : ctx.prices.symbols())
            {
                if (!first) j << ",";
                first = false;
                j << "\"" << s << "\":" << ctx.prices.latest(s);
            }
            j << "}";
        }
        else
        {
            j << "[";
            auto pts = ctx.prices.data().find(sym);
            if (pts != ctx.prices.data().end())
            {
                bool first = true;
                for (const auto& p : pts->second)
                {
                    if (!first) j << ",";
                    first = false;
                    j << "{\"ts\":" << p.timestamp << ",\"price\":" << p.price << "}";
                }
            }
            j << "]";
        }
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(j.str(), "application/json");
    });

    // ========== JSON API: POST /api/price — set a price point ==========
    svr.Post("/api/price", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        std::string sym = normalizeSymbol(fv(f, "symbol"));
        double price = fd(f, "price");
        long long ts = static_cast<long long>(fd(f, "timestamp"));
        if (sym.empty() || price <= 0) {
            res.set_content("{\"error\":\"symbol and price required\"}", "application/json");
            return;
        }
        if (ts <= 0) {
            auto now = std::chrono::system_clock::now();
            ts = std::chrono::duration_cast<std::chrono::seconds>(
                now.time_since_epoch()).count();
        }
        ctx.prices.set(sym, ts, price);
        ctx.symbols.getOrCreate(sym);
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content("{\"ok\":true,\"symbol\":\"" + sym + "\",\"price\":"
                        + std::to_string(price) + "}", "application/json");
    });

    // ========== JSON API: GET /api/chain/status ==========
    svr.Get("/api/chain/status", [&](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto state = db.loadChainState();
        std::ostringstream j;
        j << std::fixed << std::setprecision(17);
        j << "{\"active\":" << (state.active ? "true" : "false")
          << ",\"symbol\":\"" << state.symbol << "\""
          << ",\"cycle\":" << state.currentCycle
          << ",\"totalSavings\":" << state.totalSavings
          << ",\"savingsRate\":" << state.savingsRate;

        if (!state.active) {
            j << ",\"cycleEntries\":[],\"cycleComplete\":false"
              << ",\"cycleRealizedPnl\":0,\"completionPct\":0"
              << ",\"walletBalance\":" << db.loadWalletBalance() << "}";
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_content(j.str(), "application/json");
            return;
        }

        auto members = db.loadChainMembers();
        auto allEntries = db.loadEntryPoints();
        auto allTrades = db.loadTrades();

        std::vector<int> cycleEids;
        for (const auto& m : members)
            if (m.cycle == state.currentCycle)
                cycleEids.push_back(m.entryId);

        double cyclePnl = 0;
        int entryTotal = 0, entryDone = 0;
        bool anyTraded = false;
        bool allTradedSold = true;

        j << ",\"cycleEntries\":[";
        bool first = true;
        for (int eid : cycleEids)
        {
            const TradeDatabase::EntryPoint* ep = nullptr;
            for (const auto& e : allEntries)
                if (e.entryId == eid) { ep = &e; break; }
            if (!ep) continue;

            entryTotal++;
            bool traded = ep->traded && ep->linkedTradeId >= 0;
            bool fullySold = false;
            double soldQty = 0;
            double tradeQty = ep->fundingQty;

            if (traded)
            {
                anyTraded = true;
                for (const auto& t : allTrades)
                {
                    if (t.tradeId == ep->linkedTradeId && t.type == TradeType::Buy)
                    {
                        tradeQty = t.quantity;
                        soldQty = db.soldQuantityForParent(t.tradeId);
                        fullySold = (soldQty >= t.quantity - 1e-9);
                        if (fullySold) entryDone++;
                        else allTradedSold = false;
                        for (const auto& s : allTrades)
                            if (s.type == TradeType::CoveredSell && s.parentTradeId == t.tradeId)
                                cyclePnl += QuantMath::netProfit(
                                    QuantMath::grossProfit(t.value, s.value, s.quantity),
                                    0.0, s.sellFee);
                        break;
                    }
                }
            }

            if (!first) j << ",";
            first = false;
            j << "{\"id\":" << ep->entryId
              << ",\"level\":" << ep->levelIndex
              << ",\"entry\":" << ep->entryPrice
              << ",\"breakEven\":" << ep->breakEven
              << ",\"tp\":" << ep->exitTakeProfit
              << ",\"sl\":" << ep->exitStopLoss
              << ",\"qty\":" << tradeQty
              << ",\"funding\":" << ep->funding
              << ",\"traded\":" << (traded ? "true" : "false")
              << ",\"fullySold\":" << (fullySold ? "true" : "false")
              << ",\"soldQty\":" << soldQty
              << "}";
        }
        j << "]";

        bool cycleComplete = anyTraded && allTradedSold;
        double pct = entryTotal > 0 ? (static_cast<double>(entryDone) / entryTotal * 100) : 0;

        j << ",\"cycleComplete\":" << (cycleComplete ? "true" : "false")
          << ",\"cycleRealizedPnl\":" << cyclePnl
          << ",\"completionPct\":" << pct
          << ",\"walletBalance\":" << db.loadWalletBalance()
          << "}";

        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(j.str(), "application/json");
    });

    // ========== JSON API: POST /api/chain/start ==========
    svr.Post("/api/chain/start", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        std::string sym = normalizeSymbol(fv(f, "symbol"));
        double savingsRate = fd(f, "savingsRate", 0.0);

        if (sym.empty()) {
            res.set_content("{\"error\":\"symbol required\"}", "application/json");
            return;
        }

        TradeDatabase::ChainState state;
        state.symbol = sym;
        state.currentCycle = 0;
        state.totalSavings = 0;
        state.savingsRate = savingsRate;
        state.active = true;
        db.saveChainState(state);

        auto entries = db.loadEntryPoints();
        std::vector<int> entryIds;
        for (const auto& ep : entries)
            if (ep.symbol == sym && !ep.traded)
                entryIds.push_back(ep.entryId);

        db.saveChainMembers({});
        if (!entryIds.empty())
            db.addChainMembers(0, entryIds);

        std::ostringstream j;
        j << "{\"ok\":true,\"entries\":" << entryIds.size() << "}";
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(j.str(), "application/json");
    });

    // ========== JSON API: POST /api/chain/advance ==========
    svr.Post("/api/chain/advance", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto state = db.loadChainState();
        if (!state.active) {
            res.set_content("{\"error\":\"No active chain\"}", "application/json");
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

        if (!anyTraded || !allSold) {
            res.set_content("{\"error\":\"Cycle not complete\"}", "application/json");
            return;
        }

        // Divert savings
        double savingsDiv = (cyclePnl > 0 && state.savingsRate > 0) ? cyclePnl * state.savingsRate : 0;
        if (savingsDiv > 0) {
            db.withdraw(savingsDiv);
            state.totalSavings += savingsDiv;
        }

        // Parse generation params
        auto f = parseForm(req.body);
        double currentPrice = fd(f, "currentPrice");
        if (currentPrice <= 0) {
            res.set_content("{\"error\":\"currentPrice required\"}", "application/json");
            return;
        }

        double qty = fd(f, "quantity", 1.0);
        double risk = fd(f, "risk", 0.5);
        double steepness = fd(f, "steepness", 6.0);
        bool isShort = (fv(f, "isShort") == "1");
        bool genSL = (fv(f, "generateStopLosses") == "1");
        double rangeAbove = fd(f, "rangeAbove");
        double rangeBelow = fd(f, "rangeBelow");
        int downtrendCount = fi(f, "downtrendCount", 1);

        QuantMath::SerialParams sp;
        sp.currentPrice          = currentPrice;
        sp.quantity              = qty;
        sp.levels                = fi(f, "levels", 4);
        sp.steepness             = steepness;
        sp.risk                  = risk;
        sp.isShort               = isShort;
        sp.rangeAbove            = rangeAbove;
        sp.rangeBelow            = rangeBelow;
        sp.feeSpread             = fd(f, "feeSpread");
        sp.feeHedgingCoefficient = fd(f, "feeHedgingCoefficient", 1.0);
        sp.deltaTime             = fd(f, "deltaTime", 1.0);
        sp.symbolCount           = fi(f, "symbolCount", 1);
        sp.coefficientK          = fd(f, "coefficientK");
        sp.surplusRate           = fd(f, "surplusRate");
        sp.maxRisk               = fd(f, "maxRisk");
        sp.minRisk               = fd(f, "minRisk");
        sp.generateStopLosses    = genSL;
        sp.downtrendCount        = downtrendCount;
        sp.availableFunds        = db.loadWalletBalance();

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
            ep.isShort = isShort;
            ep.exitTakeProfit = e.tpUnit;
            ep.exitStopLoss = genSL ? e.slUnit : 0;
            ep.traded = false;
            ep.linkedTradeId = -1;

            existingEps.push_back(ep);
            newEntryIds.push_back(ep.entryId);
        }

        db.saveEntryPoints(existingEps);
        db.addChainMembers(nextCycle, newEntryIds);

        state.currentCycle = nextCycle;
        db.saveChainState(state);

        std::ostringstream j;
        j << std::fixed << std::setprecision(17);
        j << "{\"ok\":true,\"cycle\":" << nextCycle
          << ",\"entriesCreated\":" << newEntryIds.size()
          << ",\"savingsDiverted\":" << savingsDiv
          << ",\"totalSavings\":" << state.totalSavings
          << ",\"availableCapital\":" << sp.availableFunds
          << "}";

        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(j.str(), "application/json");
    });

    // ========== JSON API: POST /api/chain/reset ==========
    svr.Post("/api/chain/reset", [&](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        TradeDatabase::ChainState state;
        db.saveChainState(state);
        db.saveChainMembers({});
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content("{\"ok\":true}", "application/json");
    });

    // ========== JSON API: POST /api/chain/save-all — save entire chain ==========
    svr.Post("/api/chain/save-all", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        std::string sym = normalizeSymbol(fv(f, "symbol"));
        double savingsRate = fd(f, "savingsRate", 0.0);
        int chainCycles = fi(f, "chainCycles", 3);

        if (sym.empty() || fd(f, "currentPrice") <= 0) {
            res.set_content("{\"error\":\"symbol and price required\"}", "application/json");
            return;
        }
        if (chainCycles < 1) chainCycles = 1;
        if (chainCycles > 10) chainCycles = 10;

        auto sp = buildSerialParams(f, db.loadWalletBalance());
        sp.savingsRate = savingsRate;

        auto chain = QuantMath::generateChain(sp, chainCycles);

        // Clear existing chain
        db.saveChainMembers({});

        auto existingEps = db.loadEntryPoints();
        int totalEntries = 0;

        for (const auto& cc : chain.cycles)
        {
            std::vector<int> cycleEntryIds;
            for (const auto& e : cc.plan.entries)
            {
                if (e.funding <= 0) continue;

                TradeDatabase::EntryPoint ep;
                ep.symbol = sym;
                ep.entryId = db.nextEntryId();
                ep.levelIndex = e.index;
                ep.entryPrice = e.entryPrice;
                ep.breakEven = e.breakEven;
                ep.funding = e.funding;
                ep.fundingQty = e.fundQty;
                ep.effectiveOverhead = cc.plan.effectiveOH;
                ep.isShort = sp.isShort;
                ep.exitTakeProfit = e.tpUnit;
                ep.exitStopLoss = sp.generateStopLosses ? e.slUnit : 0;
                ep.traded = false;
                ep.linkedTradeId = -1;

                existingEps.push_back(ep);
                cycleEntryIds.push_back(ep.entryId);
                totalEntries++;
            }

            db.addChainMembers(cc.cycle, cycleEntryIds);
        }

        db.saveEntryPoints(existingEps);

        // Start chain tracking
        TradeDatabase::ChainState state;
        state.symbol = sym;
        state.currentCycle = 0;
        state.totalSavings = 0;
        state.savingsRate = savingsRate;
        state.active = true;
        db.saveChainState(state);

        std::ostringstream j;
        j << "{\"ok\":true,\"entries\":" << totalEntries
          << ",\"cycles\":" << chainCycles << "}";
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(j.str(), "application/json");
    });

    // ========== JSON API: POST /api/chain/from-trade — extrapolate chain from existing trade ==========
    svr.Post("/api/chain/from-trade", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        int tradeId = fi(f, "tradeId");
        double savingsRate = fd(f, "savingsRate", 0.0);
        int chainCycles = fi(f, "chainCycles", 3);
        if (chainCycles < 1) chainCycles = 1;
        if (chainCycles > 10) chainCycles = 10;

        auto trades = db.loadTrades();
        Trade* trade = db.findTradeById(trades, tradeId);
        if (!trade || trade->type != TradeType::Buy) {
            res.set_content("{\"error\":\"Buy trade not found\"}", "application/json");
            return;
        }

        std::string sym = trade->symbol;
        double tradePrice = trade->value;
        double tradeQty = trade->quantity;
        double tradeCost = QuantMath::cost(tradePrice, tradeQty);

        // Build params seeded from the trade
        QuantMath::SerialParams sp;
        sp.currentPrice          = tradePrice;
        sp.quantity              = fd(f, "quantity", 1.0);
        sp.levels                = fi(f, "levels", 4);
        sp.steepness             = fd(f, "steepness", 6.0);
        sp.risk                  = fd(f, "risk", 0.5);
        sp.isShort               = (fv(f, "isShort") == "1");
        sp.rangeAbove            = fd(f, "rangeAbove");
        sp.rangeBelow            = fd(f, "rangeBelow");
        sp.feeSpread             = fd(f, "feeSpread");
        sp.feeHedgingCoefficient = fd(f, "feeHedgingCoefficient", 1.0);
        sp.deltaTime             = fd(f, "deltaTime", 1.0);
        sp.symbolCount           = fi(f, "symbolCount", 1);
        sp.coefficientK          = fd(f, "coefficientK");
        sp.surplusRate           = fd(f, "surplusRate");
        sp.maxRisk               = fd(f, "maxRisk");
        sp.minRisk               = fd(f, "minRisk");
        sp.generateStopLosses    = (fv(f, "generateStopLosses") == "1");
        sp.downtrendCount        = fi(f, "downtrendCount", 1);
        sp.savingsRate           = savingsRate;
        sp.availableFunds        = tradeCost;

        // Compute overhead for cycle 0 entry point
        auto plan0 = QuantMath::generateSerialPlan(sp);

        // Determine TP for cycle 0 (the existing trade)
        double tpPerUnit = 0;
        if (trade->takeProfit > 0 && tradeQty > 0)
            tpPerUnit = trade->takeProfit / tradeQty;
        else if (!plan0.entries.empty())
            tpPerUnit = plan0.entries[0].tpUnit;

        double slPerUnit = 0;
        if (trade->stopLoss > 0 && tradeQty > 0)
            slPerUnit = trade->stopLoss / tradeQty;

        // Find or create entry point for the trade
        auto allEntries = db.loadEntryPoints();
        int tradeEpId = -1;
        for (const auto& ep : allEntries)
            if (ep.linkedTradeId == tradeId) { tradeEpId = ep.entryId; break; }

        if (tradeEpId < 0) {
            TradeDatabase::EntryPoint ep;
            ep.symbol = sym;
            ep.entryId = db.nextEntryId();
            ep.levelIndex = 0;
            ep.entryPrice = tradePrice;
            ep.breakEven = QuantMath::breakEven(tradePrice, plan0.overhead);
            ep.funding = tradeCost;
            ep.fundingQty = tradeQty;
            ep.effectiveOverhead = plan0.effectiveOH;
            ep.isShort = sp.isShort;
            ep.traded = true;
            ep.linkedTradeId = tradeId;
            ep.exitTakeProfit = tpPerUnit;
            ep.exitStopLoss = slPerUnit;
            ep.stopLossActive = trade->stopLossActive;
            tradeEpId = ep.entryId;
            allEntries.push_back(ep);
        }

        // Clear existing chain, tag trade as cycle 0
        db.saveChainMembers({});
        db.addChainMembers(0, {tradeEpId});

        // Cycle 0 theoretical profit
        double cycle0Profit = QuantMath::grossProfit(tradePrice, tpPerUnit, tradeQty);
        double cycle0Savings = QuantMath::savings(cycle0Profit, savingsRate);
        double startCapital = tradeCost + cycle0Profit - cycle0Savings;

        // Generate future cycles via generateChain seeded at post-cycle-0 capital
        QuantMath::SerialParams futureSp = sp;
        futureSp.availableFunds = startCapital;
        int futureCycles = chainCycles - 1;
        auto chain = QuantMath::generateChain(futureSp, futureCycles);

        int totalEntries = 0;
        for (const auto& cc : chain.cycles)
        {
            int ci = cc.cycle + 1; // offset: cycle 0 is the existing trade
            std::vector<int> cycleEntryIds;
            for (const auto& e : cc.plan.entries)
            {
                if (e.funding <= 0) continue;

                TradeDatabase::EntryPoint nep;
                nep.symbol = sym;
                nep.entryId = db.nextEntryId();
                nep.levelIndex = e.index;
                nep.entryPrice = e.entryPrice;
                nep.breakEven = e.breakEven;
                nep.funding = e.funding;
                nep.fundingQty = e.fundQty;
                nep.effectiveOverhead = cc.plan.effectiveOH;
                nep.isShort = sp.isShort;
                nep.exitTakeProfit = e.tpUnit;
                nep.exitStopLoss = sp.generateStopLosses ? e.slUnit : 0;
                nep.traded = false;
                nep.linkedTradeId = -1;

                allEntries.push_back(nep);
                cycleEntryIds.push_back(nep.entryId);
                totalEntries++;
            }

            db.addChainMembers(ci, cycleEntryIds);
        }

        db.saveEntryPoints(allEntries);

        TradeDatabase::ChainState state;
        state.symbol = sym;
        state.currentCycle = 0;
        state.totalSavings = 0;
        state.savingsRate = savingsRate;
        state.active = true;
        db.saveChainState(state);

        std::ostringstream j;
        j << std::fixed << std::setprecision(17);
        j << "{\"ok\":true,\"tradeId\":" << tradeId
          << ",\"tradePrice\":" << tradePrice
          << ",\"tradeCost\":" << tradeCost
          << ",\"entries\":" << totalEntries
          << ",\"cycles\":" << chainCycles << "}";
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(j.str(), "application/json");
    });
}
