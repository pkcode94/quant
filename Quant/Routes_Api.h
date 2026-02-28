#pragma once

#include "AppContext.h"
#include "HtmlHelpers.h"
#include "MarketEntryCalculator.h"
#include <mutex>
#include <cmath>
#include <limits>
#include <chrono>

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
            double eo = MultiHorizonEngine::effectiveOverhead(cur, qty, p);
            double overhead = MultiHorizonEngine::computeOverhead(cur, qty, p);
            double tpRef = (rangeAbove > 0.0 || rangeBelow > 0.0) ? cur + rangeAbove : cur;

            j << "{\"symbol\":\"" << sym << "\",\"currentPrice\":" << cur
              << ",\"overhead\":" << overhead << ",\"effective\":" << eo
              << ",\"isShort\":" << (isShort ? "true" : "false")
              << ",\"levels\":[";
            bool first = true;
            for (const auto& el : levels)
            {
                if (!first) j << ",";
                first = false;
                double exitTP = MultiHorizonEngine::levelTP(el.entryPrice, overhead, eo, p, steepness, el.index, p.horizonCount, isShort, risk, tpRef);
                double exitSL = MultiHorizonEngine::levelSL(el.entryPrice, eo, isShort);
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

    // ========== JSON API: POST /api/calc/serial � live serial calculation ==========
    svr.Post("/api/calc/serial", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        std::string sym = normalizeSymbol(fv(f, "symbol"));
        double cur = fd(f, "currentPrice");
        double qty = fd(f, "quantity");
        double risk = fd(f, "risk");
        double steepness = fd(f, "steepness", 6.0);
        bool isShort = (fv(f, "isShort") == "1");
        int fundMode = fi(f, "fundMode", 1);
        bool genSL = (fv(f, "generateStopLosses") == "1");
        double rangeAbove = fd(f, "rangeAbove");
        double rangeBelow = fd(f, "rangeBelow");
        int downtrendCount = fi(f, "downtrendCount", 1);

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
        p.futureTradeCount = fi(f, "futureTradeCount", 0);
        p.stopLossFraction = fd(f, "stopLossFraction", 1.0);
        p.stopLossHedgeCount = fi(f, "stopLossHedgeCount", 0);

        double walBal = db.loadWalletBalance();
        double availableFunds = p.portfolioPump;
        if (fundMode == 2) availableFunds += walBal;

        std::ostringstream j;
        j << std::fixed << std::setprecision(17);
        if (sym.empty() || cur <= 0 || qty <= 0) { j << "{\"error\":true}"; }
        else
        {
            int N = p.horizonCount;
            if (N < 1) N = 1;
            if (steepness < 0.1) steepness = 0.1;

            auto sigmoid = [](double x) { return 1.0 / (1.0 + std::exp(-x)); };
            double sig0 = sigmoid(-steepness * 0.5);
            double sig1 = sigmoid(steepness * 0.5);
            double sigRange = (sig1 - sig0 > 0) ? sig1 - sig0 : 1.0;

            double priceLow, priceHigh;
            if (rangeAbove > 0.0 || rangeBelow > 0.0)
            {
                priceLow = cur - rangeBelow;
                if (priceLow < 1e-18) priceLow = 1e-18;
                priceHigh = cur + rangeAbove;
            }
            else
            {
                priceLow = 0.0;
                priceHigh = cur;
            }

            std::vector<double> norm(N);
            for (int i = 0; i < N; ++i)
            {
                double t = (N > 1) ? static_cast<double>(i) / static_cast<double>(N - 1) : 1.0;
                double sigVal = sigmoid(steepness * (t - 0.5));
                norm[i] = (sigVal - sig0) / sigRange;
            }

            double riskClamped = (risk < 0) ? 0 : (risk > 1) ? 1 : risk;
            std::vector<double> weights(N);
            double weightSum = 0;
            for (int i = 0; i < N; ++i)
            {
                weights[i] = (1.0 - riskClamped) * norm[i] + riskClamped * (1.0 - norm[i]);
                if (weights[i] < 1e-12) weights[i] = 1e-12;
                weightSum += weights[i];
            }

            double eo = MultiHorizonEngine::effectiveOverhead(cur, qty, p);
            double overhead = MultiHorizonEngine::computeOverhead(cur, qty, p);
            double dtBuffer = MultiHorizonEngine::calculateDowntrendBuffer(cur, qty, availableFunds, eo, p.minRisk, p.maxRisk, downtrendCount);
            double slFrac1 = MultiHorizonEngine::stopLossSellFraction(p);
            double slBuf1 = MultiHorizonEngine::calculateStopLossBuffer(cur, qty, availableFunds, eo, p.minRisk, p.maxRisk, slFrac1, p.stopLossHedgeCount);
            double combinedBuffer = dtBuffer * slBuf1;

            j << "{\"symbol\":\"" << sym << "\",\"currentPrice\":" << cur
              << ",\"overhead\":" << overhead << ",\"effective\":" << eo
              << ",\"dtBuffer\":" << dtBuffer
              << ",\"slBuffer\":" << slBuf1
              << ",\"isShort\":" << (isShort ? "true" : "false")
              << ",\"levels\":[";
            bool first = true;
            for (int i = 0; i < N; ++i)
            {
                double entryPrice = priceLow + norm[i] * (priceHigh - priceLow);
                if (entryPrice < 1e-18) entryPrice = 1e-18;

                double tpPrice = MultiHorizonEngine::levelTP(entryPrice, overhead, eo, p, steepness, i, N, isShort, riskClamped, priceHigh);
                if (combinedBuffer > 1.0) tpPrice *= combinedBuffer;
                double slPrice = MultiHorizonEngine::levelSL(entryPrice, eo, isShort);

                double fundFrac = (weightSum > 0) ? weights[i] / weightSum : 0;
                double funding = availableFunds * fundFrac;
                double fundQty = funding / entryPrice;
                double breakEven = entryPrice * (1.0 + overhead);

                if (!first) j << ",";
                first = false;
                j << "{\"index\":" << i
                  << ",\"entry\":" << entryPrice
                  << ",\"breakEven\":" << breakEven
                  << ",\"tp\":" << tpPrice
                  << ",\"sl\":" << (genSL ? slPrice : 0.0)
                  << ",\"funding\":" << funding
                  << ",\"fundPct\":" << (fundFrac * 100)
                  << ",\"qty\":" << fundQty
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

        double currentPrice = fd(f, "currentPrice");
        double qty = fd(f, "quantity", 1.0);
        double risk = fd(f, "risk", 0.5);
        double steepness = fd(f, "steepness", 6.0);
        bool isShort = (fv(f, "isShort") == "1");
        int fundMode = fi(f, "fundMode", 1);
        bool genSL = (fv(f, "generateStopLosses") == "1");
        double rangeAbove = fd(f, "rangeAbove");
        double rangeBelow = fd(f, "rangeBelow");
        int downtrendCount = fi(f, "downtrendCount", 1);
        int chainCycles = fi(f, "chainCycles", 3);
        double savingsRate = fd(f, "savingsRate", 0.0);

        if (chainCycles < 1) chainCycles = 1;
        if (chainCycles > 10) chainCycles = 10;

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
        p.futureTradeCount = fi(f, "futureTradeCount", 0);
        p.stopLossFraction = fd(f, "stopLossFraction", 1.0);
        p.stopLossHedgeCount = fi(f, "stopLossHedgeCount", 0);

        double walBal = db.loadWalletBalance();
        double availableFunds = p.portfolioPump;
        if (fundMode == 2) availableFunds += walBal;

        int N = p.horizonCount;
        if (N < 1) N = 1;
        if (steepness < 0.1) steepness = 0.1;

        auto sigmoid = [](double x) { return 1.0 / (1.0 + std::exp(-x)); };
        double sig0 = sigmoid(-steepness * 0.5);
        double sig1 = sigmoid(steepness * 0.5);
        double sigRange = (sig1 - sig0 > 0) ? sig1 - sig0 : 1.0;

        double priceLow, priceHigh;
        if (rangeAbove > 0.0 || rangeBelow > 0.0) {
            priceLow = currentPrice - rangeBelow;
            if (priceLow < 1e-18) priceLow = 1e-18;
            priceHigh = currentPrice + rangeAbove;
        } else {
            priceLow = 0.0;
            priceHigh = currentPrice;
        }

        std::vector<double> norm(N);
        for (int i = 0; i < N; ++i) {
            double t = (N > 1) ? static_cast<double>(i) / static_cast<double>(N - 1) : 1.0;
            double sigVal = sigmoid(steepness * (t - 0.5));
            norm[i] = (sigVal - sig0) / sigRange;
        }

        double riskClamped = (risk < 0) ? 0 : (risk > 1) ? 1 : risk;
        std::vector<double> weights(N);
        double weightSum = 0;
        for (int i = 0; i < N; ++i) {
            weights[i] = (1.0 - riskClamped) * norm[i] + riskClamped * (1.0 - norm[i]);
            if (weights[i] < 1e-12) weights[i] = 1e-12;
            weightSum += weights[i];
        }

        std::ostringstream j;
        j << std::fixed << std::setprecision(17);

        if (currentPrice <= 0 || availableFunds <= 0) {
            j << "{\"error\":true}";
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_content(j.str(), "application/json");
            return;
        }

        double eo0 = MultiHorizonEngine::effectiveOverhead(currentPrice, qty, p);
        double oh0 = MultiHorizonEngine::computeOverhead(currentPrice, qty, p);

        j << "{\"currentPrice\":" << currentPrice
          << ",\"overhead\":" << oh0
          << ",\"effective\":" << eo0
          << ",\"cycles\":[";

        double capital = availableFunds;

        for (int ci = 0; ci < chainCycles; ++ci)
        {
            if (ci > 0) j << ",";

            HorizonParams cp = p;
            cp.portfolioPump = capital;
            double cycleEo = MultiHorizonEngine::effectiveOverhead(currentPrice, qty, cp);
            double cycleOh = MultiHorizonEngine::computeOverhead(currentPrice, qty, cp);
            double dtBuffer = MultiHorizonEngine::calculateDowntrendBuffer(
                currentPrice, qty, capital, cycleEo, cp.minRisk, cp.maxRisk, downtrendCount);
            double slFrac = MultiHorizonEngine::stopLossSellFraction(cp);
            double slBuf = MultiHorizonEngine::calculateStopLossBuffer(
                currentPrice, qty, capital, cycleEo, cp.minRisk, cp.maxRisk, slFrac, cp.stopLossHedgeCount);
            double combinedBuffer = dtBuffer * slBuf;

            j << "{\"cycle\":" << ci
              << ",\"capital\":" << capital
              << ",\"overhead\":" << cycleOh
              << ",\"effective\":" << cycleEo
              << ",\"levels\":[";

            double cycleProfit = 0;

            for (int i = 0; i < N; ++i)
            {
                double entryPrice = priceLow + norm[i] * (priceHigh - priceLow);
                if (entryPrice < 1e-18) entryPrice = 1e-18;

                double tpPrice = MultiHorizonEngine::levelTP(
                    entryPrice, cycleOh, cycleEo, cp, steepness, i, N, isShort, riskClamped, priceHigh);
                if (combinedBuffer > 1.0) tpPrice *= combinedBuffer;
                double slPrice = genSL ? MultiHorizonEngine::levelSL(entryPrice, cycleEo, isShort) : 0;

                double fundFrac = (weightSum > 0) ? weights[i] / weightSum : 0;
                double funding = capital * fundFrac;
                double fundQty = funding / entryPrice;
                double breakEven = entryPrice * (1.0 + cycleOh);

                cycleProfit += (tpPrice - entryPrice) * fundQty;

                if (i > 0) j << ",";
                j << "{\"index\":" << i
                  << ",\"entry\":" << entryPrice
                  << ",\"breakEven\":" << breakEven
                  << ",\"tp\":" << tpPrice
                  << ",\"sl\":" << (genSL ? slPrice : 0.0)
                  << ",\"funding\":" << funding
                  << ",\"fundPct\":" << (fundFrac * 100)
                  << ",\"qty\":" << fundQty
                  << "}";
            }

            double cycleSavings = (cycleProfit > 0 && savingsRate > 0) ? cycleProfit * savingsRate : 0;
            double capitalAfter = capital + cycleProfit - cycleSavings;

            j << "],\"profit\":" << cycleProfit
              << ",\"savings\":" << cycleSavings
              << ",\"capitalAfter\":" << capitalAfter
              << "}";

            capital = capitalAfter;
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
                            double eo = MultiHorizonEngine::effectiveOverhead(price, qty, hp);
                            double oh = MultiHorizonEngine::computeOverhead(price, qty, hp);
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
                                cyclePnl += (s.value - t.value) * s.quantity - s.sellFee;
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
                                cyclePnl += (s.value - t.value) * s.quantity - s.sellFee;
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

        HorizonParams p;
        p.horizonCount = fi(f, "levels", 4);
        p.feeHedgingCoefficient = fd(f, "feeHedgingCoefficient", 1.0);
        p.portfolioPump = db.loadWalletBalance();
        p.symbolCount = fi(f, "symbolCount", 1);
        p.coefficientK = fd(f, "coefficientK");
        p.feeSpread = fd(f, "feeSpread");
        p.deltaTime = fd(f, "deltaTime", 1.0);
        p.surplusRate = fd(f, "surplusRate");
        p.maxRisk = fd(f, "maxRisk");
        p.minRisk = fd(f, "minRisk");

        double availableFunds = p.portfolioPump;
        int N = p.horizonCount;
        if (N < 1) N = 1;
        if (steepness < 0.1) steepness = 0.1;

        auto sigmoid = [](double x) { return 1.0 / (1.0 + std::exp(-x)); };
        double sig0 = sigmoid(-steepness * 0.5);
        double sig1 = sigmoid(steepness * 0.5);
        double sigRange = (sig1 - sig0 > 0) ? sig1 - sig0 : 1.0;

        double priceLow, priceHigh;
        if (rangeAbove > 0.0 || rangeBelow > 0.0) {
            priceLow = currentPrice - rangeBelow;
            if (priceLow < 1e-18) priceLow = 1e-18;
            priceHigh = currentPrice + rangeAbove;
        } else {
            priceLow = 0.0;
            priceHigh = currentPrice;
        }

        std::vector<double> norm(N);
        for (int i = 0; i < N; ++i) {
            double t = (N > 1) ? static_cast<double>(i) / static_cast<double>(N - 1) : 1.0;
            double sigVal = sigmoid(steepness * (t - 0.5));
            norm[i] = (sigVal - sig0) / sigRange;
        }

        double riskClamped = (risk < 0) ? 0 : (risk > 1) ? 1 : risk;
        std::vector<double> weights(N);
        double weightSum = 0;
        for (int i = 0; i < N; ++i) {
            weights[i] = (1.0 - riskClamped) * norm[i] + riskClamped * (1.0 - norm[i]);
            if (weights[i] < 1e-12) weights[i] = 1e-12;
            weightSum += weights[i];
        }

        double eo = MultiHorizonEngine::effectiveOverhead(currentPrice, qty, p);
        double overhead = MultiHorizonEngine::computeOverhead(currentPrice, qty, p);
        double dtBuffer = MultiHorizonEngine::calculateDowntrendBuffer(
            currentPrice, qty, availableFunds, eo, p.minRisk, p.maxRisk, downtrendCount);

        int nextCycle = state.currentCycle + 1;
        std::vector<int> newEntryIds;
        auto existingEps = db.loadEntryPoints();

        for (int i = 0; i < N; ++i)
        {
            double entryPrice = priceLow + norm[i] * (priceHigh - priceLow);
            if (entryPrice < 1e-18) entryPrice = 1e-18;

            double tpPrice = MultiHorizonEngine::levelTP(
                entryPrice, overhead, eo, p, steepness, i, N, isShort, riskClamped, priceHigh);
            if (dtBuffer > 1.0) tpPrice *= dtBuffer;
            double slPrice = genSL ? MultiHorizonEngine::levelSL(entryPrice, eo, isShort) : 0;

            double fundFrac = (weightSum > 0) ? weights[i] / weightSum : 0;
            double funding = availableFunds * fundFrac;
            if (funding <= 0) continue;
            double fundQty = funding / entryPrice;
            double breakEven = entryPrice * (1.0 + overhead);

            TradeDatabase::EntryPoint ep;
            ep.symbol = state.symbol;
            ep.entryId = db.nextEntryId();
            ep.levelIndex = i;
            ep.entryPrice = entryPrice;
            ep.breakEven = breakEven;
            ep.funding = funding;
            ep.fundingQty = fundQty;
            ep.effectiveOverhead = eo;
            ep.isShort = isShort;
            ep.exitTakeProfit = tpPrice;
            ep.exitStopLoss = slPrice;
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
          << ",\"availableCapital\":" << availableFunds
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
        double currentPrice = fd(f, "currentPrice");
        double qty = fd(f, "quantity", 1.0);
        double risk = fd(f, "risk", 0.5);
        double steepness = fd(f, "steepness", 6.0);
        bool isShort = (fv(f, "isShort") == "1");
        int fundMode = fi(f, "fundMode", 1);
        bool genSL = (fv(f, "generateStopLosses") == "1");
        double rangeAbove = fd(f, "rangeAbove");
        double rangeBelow = fd(f, "rangeBelow");
        int downtrendCount = fi(f, "downtrendCount", 1);
        int chainCycles = fi(f, "chainCycles", 3);

        if (sym.empty() || currentPrice <= 0) {
            res.set_content("{\"error\":\"symbol and price required\"}", "application/json");
            return;
        }

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
        p.futureTradeCount = fi(f, "futureTradeCount", 0);
        p.stopLossFraction = fd(f, "stopLossFraction", 1.0);
        p.stopLossHedgeCount = fi(f, "stopLossHedgeCount", 0);

        double walBal = db.loadWalletBalance();
        double availableFunds = p.portfolioPump;
        if (fundMode == 2) availableFunds += walBal;

        if (chainCycles < 1) chainCycles = 1;
        if (chainCycles > 10) chainCycles = 10;

        int N = p.horizonCount;
        if (N < 1) N = 1;
        if (steepness < 0.1) steepness = 0.1;

        auto sigmoid = [](double x) { return 1.0 / (1.0 + std::exp(-x)); };
        double sig0 = sigmoid(-steepness * 0.5);
        double sig1 = sigmoid(steepness * 0.5);
        double sigRange = (sig1 - sig0 > 0) ? sig1 - sig0 : 1.0;

        double priceLow, priceHigh;
        if (rangeAbove > 0.0 || rangeBelow > 0.0) {
            priceLow = currentPrice - rangeBelow;
            if (priceLow < 1e-18) priceLow = 1e-18;
            priceHigh = currentPrice + rangeAbove;
        } else {
            priceLow = 0.0;
            priceHigh = currentPrice;
        }

        std::vector<double> norm(N);
        for (int i = 0; i < N; ++i) {
            double t = (N > 1) ? static_cast<double>(i) / static_cast<double>(N - 1) : 1.0;
            double sigVal = sigmoid(steepness * (t - 0.5));
            norm[i] = (sigVal - sig0) / sigRange;
        }

        double riskClamped = (risk < 0) ? 0 : (risk > 1) ? 1 : risk;
        std::vector<double> weights(N);
        double weightSum = 0;
        for (int i = 0; i < N; ++i) {
            weights[i] = (1.0 - riskClamped) * norm[i] + riskClamped * (1.0 - norm[i]);
            if (weights[i] < 1e-12) weights[i] = 1e-12;
            weightSum += weights[i];
        }

        // Clear existing chain
        db.saveChainMembers({});

        auto existingEps = db.loadEntryPoints();
        double capital = availableFunds;
        int totalEntries = 0;

        for (int ci = 0; ci < chainCycles; ++ci)
        {
            HorizonParams cp = p;
            cp.portfolioPump = capital;
            double cycleEo = MultiHorizonEngine::effectiveOverhead(currentPrice, qty, cp);
            double cycleOh = MultiHorizonEngine::computeOverhead(currentPrice, qty, cp);
            double dtBuffer = MultiHorizonEngine::calculateDowntrendBuffer(
                currentPrice, qty, capital, cycleEo, cp.minRisk, cp.maxRisk, downtrendCount);
            double slFrac2 = MultiHorizonEngine::stopLossSellFraction(cp);
            double slBuf2 = MultiHorizonEngine::calculateStopLossBuffer(
                currentPrice, qty, capital, cycleEo, cp.minRisk, cp.maxRisk, slFrac2, cp.stopLossHedgeCount);
            double combinedBuffer = dtBuffer * slBuf2;

            std::vector<int> cycleEntryIds;
            double cycleProfit = 0;

            for (int i = 0; i < N; ++i)
            {
                double entryPrice = priceLow + norm[i] * (priceHigh - priceLow);
                if (entryPrice < 1e-18) entryPrice = 1e-18;

                double tpPrice = MultiHorizonEngine::levelTP(
                    entryPrice, cycleOh, cycleEo, cp, steepness, i, N, isShort, riskClamped, priceHigh);
                if (combinedBuffer > 1.0) tpPrice *= combinedBuffer;
                double slPrice = genSL ? MultiHorizonEngine::levelSL(entryPrice, cycleEo, isShort) : 0;

                double fundFrac = (weightSum > 0) ? weights[i] / weightSum : 0;
                double funding = capital * fundFrac;
                if (funding <= 0) continue;
                double fundQty = funding / entryPrice;
                double breakEven = entryPrice * (1.0 + cycleOh);

                cycleProfit += (tpPrice - entryPrice) * fundQty;

                TradeDatabase::EntryPoint ep;
                ep.symbol = sym;
                ep.entryId = db.nextEntryId();
                ep.levelIndex = i;
                ep.entryPrice = entryPrice;
                ep.breakEven = breakEven;
                ep.funding = funding;
                ep.fundingQty = fundQty;
                ep.effectiveOverhead = cycleEo;
                ep.isShort = isShort;
                ep.exitTakeProfit = tpPrice;
                ep.exitStopLoss = slPrice;
                ep.traded = false;
                ep.linkedTradeId = -1;

                existingEps.push_back(ep);
                cycleEntryIds.push_back(ep.entryId);
                totalEntries++;
            }

            db.addChainMembers(ci, cycleEntryIds);

            double cycleSavings = (cycleProfit > 0 && savingsRate > 0) ? cycleProfit * savingsRate : 0;
            capital = capital + cycleProfit - cycleSavings;
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
        double tradeCost = tradePrice * tradeQty;

        double qty = fd(f, "quantity", 1.0);
        double risk = fd(f, "risk", 0.5);
        double steepness = fd(f, "steepness", 6.0);
        bool isShort = (fv(f, "isShort") == "1");
        bool genSL = (fv(f, "generateStopLosses") == "1");
        double rangeAbove = fd(f, "rangeAbove");
        double rangeBelow = fd(f, "rangeBelow");
        int downtrendCount = fi(f, "downtrendCount", 1);

        HorizonParams p;
        p.horizonCount = fi(f, "levels", 4);
        p.feeHedgingCoefficient = fd(f, "feeHedgingCoefficient", 1.0);
        p.portfolioPump = tradeCost;
        p.symbolCount = fi(f, "symbolCount", 1);
        p.coefficientK = fd(f, "coefficientK");
        p.feeSpread = fd(f, "feeSpread");
        p.deltaTime = fd(f, "deltaTime", 1.0);
        p.surplusRate = fd(f, "surplusRate");
        p.maxRisk = fd(f, "maxRisk");
        p.minRisk = fd(f, "minRisk");

        int N = p.horizonCount;
        if (N < 1) N = 1;
        if (steepness < 0.1) steepness = 0.1;

        double eo = MultiHorizonEngine::effectiveOverhead(tradePrice, qty, p);
        double overhead = MultiHorizonEngine::computeOverhead(tradePrice, qty, p);
        double riskClamped = (risk < 0) ? 0 : (risk > 1) ? 1 : risk;

        // Determine TP for cycle 0 (the existing trade)
        double tpPerUnit = 0;
        if (trade->takeProfit > 0 && tradeQty > 0)
            tpPerUnit = trade->takeProfit / tradeQty;
        else
            tpPerUnit = MultiHorizonEngine::levelTP(tradePrice, overhead, eo, p, steepness, 0, N, isShort, riskClamped, tradePrice);

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
            ep.breakEven = tradePrice * (1.0 + overhead);
            ep.funding = tradeCost;
            ep.fundingQty = tradeQty;
            ep.effectiveOverhead = eo;
            ep.isShort = isShort;
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
        double cycle0Profit = (tpPerUnit - tradePrice) * tradeQty;
        double cycle0Savings = (cycle0Profit > 0 && savingsRate > 0) ? cycle0Profit * savingsRate : 0;
        double capital = tradeCost + cycle0Profit - cycle0Savings;

        // Generate future cycles
        auto sigmoid = [](double x) { return 1.0 / (1.0 + std::exp(-x)); };
        double sig0 = sigmoid(-steepness * 0.5);
        double sig1 = sigmoid(steepness * 0.5);
        double sigRange = (sig1 - sig0 > 0) ? sig1 - sig0 : 1.0;

        double priceLow, priceHigh;
        if (rangeAbove > 0.0 || rangeBelow > 0.0) {
            priceLow = tradePrice - rangeBelow;
            if (priceLow < 1e-18) priceLow = 1e-18;
            priceHigh = tradePrice + rangeAbove;
        } else {
            priceLow = 0.0;
            priceHigh = tradePrice;
        }

        std::vector<double> norm(N);
        for (int i = 0; i < N; ++i) {
            double t = (N > 1) ? static_cast<double>(i) / static_cast<double>(N - 1) : 1.0;
            double sigVal = sigmoid(steepness * (t - 0.5));
            norm[i] = (sigVal - sig0) / sigRange;
        }

        std::vector<double> weights(N);
        double weightSum = 0;
        for (int i = 0; i < N; ++i) {
            weights[i] = (1.0 - riskClamped) * norm[i] + riskClamped * (1.0 - norm[i]);
            if (weights[i] < 1e-12) weights[i] = 1e-12;
            weightSum += weights[i];
        }

        int totalEntries = 0;

        for (int ci = 1; ci < chainCycles; ++ci)
        {
            HorizonParams cp = p;
            cp.portfolioPump = capital;
            double cycleEo = MultiHorizonEngine::effectiveOverhead(tradePrice, qty, cp);
            double cycleOh = MultiHorizonEngine::computeOverhead(tradePrice, qty, cp);
            double dtBuffer = MultiHorizonEngine::calculateDowntrendBuffer(
                tradePrice, qty, capital, cycleEo, cp.minRisk, cp.maxRisk, downtrendCount);

            std::vector<int> cycleEntryIds;
            double cycleProfit = 0;

            for (int i = 0; i < N; ++i)
            {
                double entryPrice = priceLow + norm[i] * (priceHigh - priceLow);
                if (entryPrice < 1e-18) entryPrice = 1e-18;

                double tpPrice = MultiHorizonEngine::levelTP(
                    entryPrice, cycleOh, cycleEo, cp, steepness, i, N, isShort, riskClamped, priceHigh);
                if (dtBuffer > 1.0) tpPrice *= dtBuffer;
                double slPrice = genSL ? MultiHorizonEngine::levelSL(entryPrice, cycleEo, isShort) : 0;

                double fundFrac = (weightSum > 0) ? weights[i] / weightSum : 0;
                double funding = capital * fundFrac;
                if (funding <= 0) continue;
                double fundQty = funding / entryPrice;
                double breakEven = entryPrice * (1.0 + cycleOh);

                cycleProfit += (tpPrice - entryPrice) * fundQty;

                TradeDatabase::EntryPoint nep;
                nep.symbol = sym;
                nep.entryId = db.nextEntryId();
                nep.levelIndex = i;
                nep.entryPrice = entryPrice;
                nep.breakEven = breakEven;
                nep.funding = funding;
                nep.fundingQty = fundQty;
                nep.effectiveOverhead = cycleEo;
                nep.isShort = isShort;
                nep.exitTakeProfit = tpPrice;
                nep.exitStopLoss = slPrice;
                nep.traded = false;
                nep.linkedTradeId = -1;

                allEntries.push_back(nep);
                cycleEntryIds.push_back(nep.entryId);
                totalEntries++;
            }

            db.addChainMembers(ci, cycleEntryIds);

            double cycleSavings = (cycleProfit > 0 && savingsRate > 0) ? cycleProfit * savingsRate : 0;
            capital = capital + cycleProfit - cycleSavings;
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
