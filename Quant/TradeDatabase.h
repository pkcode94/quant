#pragma once

#include "Trade.h"
#include "MultiHorizonEngine.h"
#include "ProfitCalculator.h"
#include "IdGenerator.h"
#include "PriceSeries.h"
#include "json.h"

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <stdexcept>
#include <iomanip>
#include <algorithm>
#include <chrono>
#include <ctime>

class TradeDatabase
{
public:
    explicit TradeDatabase(const std::string& directory = "db")
        : m_dir(directory)
    {
        std::filesystem::create_directories(m_dir);
        seedIdGenerators();
    }

    // Populate ID generators from existing data on disk so that
    // acquire() finds the lowest free ID (gap-filling reuse).
    void seedIdGenerators()
    {
        {
            std::set<int> used;
            for (const auto& t : loadTrades())
                if (t.tradeId > 0) used.insert(t.tradeId);
            m_tradeIdGen.seed(used);
        }
        {
            std::set<int> used;
            for (const auto& o : loadPendingExits())
                if (o.orderId > 0) used.insert(o.orderId);
            m_pendingIdGen.seed(used);
        }
        {
            std::set<int> used;
            for (const auto& ep : loadEntryPoints())
                if (ep.entryId > 0) used.insert(ep.entryId);
            m_entryIdGen.seed(used);
        }
        {
            std::set<int> used;
            for (const auto& ep : loadExitPoints())
                if (ep.exitId > 0) used.insert(ep.exitId);
            m_exitIdGen.seed(used);
        }
    }

    // ---- Trades ----

    void saveTrades(const std::vector<Trade>& trades)
    {
        njs3::js_array arr;
        for (const auto& t : trades)
        {
            njs3::json j(njs3::js_object{});
            j["symbol"] = JStr(t.symbol);
            j["tradeId"] = JI(t.tradeId);
            j["type"] = JI(static_cast<int>(t.type));
            j["value"] = JD(t.value);
            j["quantity"] = JD(t.quantity);
            j["parentTradeId"] = JI(t.parentTradeId);
            j["shortEnabled"] = JB(t.shortEnabled);
            j["buyFee"] = JD(t.buyFee);
            j["sellFee"] = JD(t.sellFee);
            j["timestamp"] = JLL(t.timestamp);
            arr.push_back(std::move(j));
        }
        writeJson(tradesPath(), njs3::json(std::move(arr)));
    }

    void addTrade(const Trade& t)
    {
        auto all = loadTrades();
        all.push_back(t);
        saveTrades(all);
    }

    void removeTrade(int tradeId)
    {
        auto all = loadTrades();

        // cascade: if this is a parent Buy, also remove its CoveredSell children
        std::vector<int> idsToRemove = { tradeId };
        for (const auto& t : all)
            if (t.parentTradeId == tradeId)
                idsToRemove.push_back(t.tradeId);

        std::erase_if(all, [&](const Trade& t) {
            return std::find(idsToRemove.begin(), idsToRemove.end(), t.tradeId) != idsToRemove.end();
        });
        saveTrades(all);

        // Release IDs back to the pool
        for (int id : idsToRemove)
            m_tradeIdGen.release(id);

        auto hl = loadAllHorizons();
        std::erase_if(hl, [&](const std::tuple<std::string, int, HorizonLevel>& e) {
            return std::find(idsToRemove.begin(), idsToRemove.end(), std::get<1>(e)) != idsToRemove.end();
        });
        saveAllHorizons(hl);
    }

    void updateTrade(const Trade& updated)
    {
        auto all = loadTrades();
        for (auto& t : all)
        {
            if (t.tradeId == updated.tradeId)
            {
                t = updated;
                break;
            }
        }
        saveTrades(all);
    }

    std::vector<Trade> loadTrades() const
    {
        std::vector<Trade> out;
        auto j = readJsonArr(tradesPath());
        const auto* a = j.as_array();
        if (!a) return out;
        for (const auto& item : *a)
        {
            Trade t;
            t.symbol        = gs(item, "symbol");
            t.tradeId       = gi(item, "tradeId");
            t.type          = static_cast<TradeType>(gi(item, "type"));
            t.value         = gd(item, "value");
            t.quantity      = gd(item, "quantity");
            t.parentTradeId = gi(item, "parentTradeId");
            t.shortEnabled  = gb(item, "shortEnabled");
            t.buyFee        = gd(item, "buyFee");
            t.sellFee       = gd(item, "sellFee");
            t.timestamp     = gll(item, "timestamp");
            out.push_back(t);
        }
        return out;
    }

    Trade* findTrade(std::vector<Trade>& trades,
                     const std::string& symbol, int tradeId) const
    {
        for (auto& t : trades)
            if (t.symbol == symbol && t.tradeId == tradeId)
                return &t;
        return nullptr;
    }

    Trade* findTradeById(std::vector<Trade>& trades, int tradeId) const
    {
        for (auto& t : trades)
            if (t.tradeId == tradeId)
                return &t;
        return nullptr;
    }

    double soldQuantityForParent(int parentId) const
    {
        double sold = 0.0;
        for (const auto& t : loadTrades())
            if (t.type == TradeType::CoveredSell && t.parentTradeId == parentId)
                sold += t.quantity;
        return sold;
    }

    // Acquire the next available trade ID (gap-filling via IdGenerator).
    int nextTradeId()
    {
        int id = m_tradeIdGen.acquire();
        m_tradeIdGen.commit(id);
        return id;
    }

    // Release a trade ID back to the pool (e.g. after deletion).
    void releaseTradeId(int id) { m_tradeIdGen.release(id); }

    IdGenerator&       tradeIdGen()       { return m_tradeIdGen; }
    const IdGenerator& tradeIdGen() const { return m_tradeIdGen; }

    // ---- Horizon Levels ----

    void saveHorizonLevels(const std::string& symbol, int tradeId,
                           const std::vector<HorizonLevel>& levels)
    {
        auto all = loadAllHorizons();
        std::erase_if(all, [&](const std::tuple<std::string, int, HorizonLevel>& e) {
            return std::get<0>(e) == symbol && std::get<1>(e) == tradeId;
        });
        for (const auto& lv : levels)
            all.emplace_back(symbol, tradeId, lv);
        saveAllHorizons(all);
    }

    std::vector<HorizonLevel> loadHorizonLevels(const std::string& symbol,
                                                 int tradeId) const
    {
        std::vector<HorizonLevel> out;
        for (const auto& [sym, tid, lv] : loadAllHorizons())
            if (sym == symbol && tid == tradeId)
                out.push_back(lv);
        return out;
    }

    // ---- Profit Snapshots ----

    void saveProfitSnapshot(const std::string& symbol, int tradeId,
                            double currentPrice, const ProfitResult& r)
    {
        auto j = readJsonArr(profitsPath());
        auto* a = j.as_array();
        if (!a) { j = njs3::json(njs3::js_array{}); a = j.as_array(); }
        njs3::json row(njs3::js_object{});
        row["symbol"] = JStr(symbol);
        row["tradeId"] = JI(tradeId);
        row["currentPrice"] = JD(currentPrice);
        row["grossProfit"] = JD(r.grossProfit);
        row["netProfit"] = JD(r.netProfit);
        row["roi"] = JD(r.roi);
        a->push_back(std::move(row));
        writeJson(profitsPath(), j);
    }

    struct ProfitRow
    {
        std::string symbol;
        int    tradeId      = 0;
        double currentPrice = 0.0;
        double grossProfit  = 0.0;
        double netProfit    = 0.0;
        double roi          = 0.0;
    };

    std::vector<ProfitRow> loadProfitHistory() const
    {
        std::vector<ProfitRow> out;
        auto j = readJsonArr(profitsPath());
        const auto* a = j.as_array();
        if (!a) return out;
        for (const auto& item : *a)
        {
            ProfitRow r;
            r.symbol       = gs(item, "symbol");
            r.tradeId      = gi(item, "tradeId");
            r.currentPrice = gd(item, "currentPrice");
            r.grossProfit  = gd(item, "grossProfit");
            r.netProfit    = gd(item, "netProfit");
            r.roi          = gd(item, "roi");
            out.push_back(r);
        }
        return out;
    }

    // ---- Parameter Snapshots ----

    struct ParamsRow
    {
        std::string calcType;     // "horizon", "entry", or "exit"
        std::string symbol;
        int    tradeId                    = -1;
        double currentPrice               = 0.0;
        double quantity                   = 0.0;
        double buyFees                    = 0.0;
        double sellFees                   = 0.0;
        double feeHedgingCoefficient      = 0.0;
        double portfolioPump              = 0.0;
        int    symbolCount                = 0;
        double coefficientK               = 0.0;
        double feeSpread                  = 0.0;
        double deltaTime                  = 0.0;
        double surplusRate                = 0.0;
        int    horizonCount               = 0;
        bool   generateStopLosses         = false;
        double riskCoefficient            = 0.0;
        double maxRisk                    = 0.0;
        double minRisk                    = 0.0;

        static ParamsRow from(const std::string& type, const std::string& sym,
                              int tid, double price, double qty,
                              const HorizonParams& p, double risk = 0.0,
                              double buyFees = 0.0, double sellFees = 0.0)
        {
            ParamsRow r;
            r.calcType              = type;
            r.symbol                = sym;
            r.tradeId               = tid;
            r.currentPrice          = price;
            r.quantity              = qty;
            r.buyFees               = buyFees;
            r.sellFees              = sellFees;
            r.feeHedgingCoefficient = p.feeHedgingCoefficient;
            r.portfolioPump         = p.portfolioPump;
            r.symbolCount           = p.symbolCount;
            r.coefficientK          = p.coefficientK;
            r.feeSpread             = p.feeSpread;
            r.deltaTime             = p.deltaTime;
            r.surplusRate           = p.surplusRate;
            r.horizonCount          = p.horizonCount;
            r.generateStopLosses    = p.generateStopLosses;
            r.riskCoefficient       = risk;
            r.maxRisk               = p.maxRisk;
            r.minRisk               = p.minRisk;
            return r;
        }

        HorizonParams toHorizonParams() const
        {
            HorizonParams p;
            p.feeHedgingCoefficient = feeHedgingCoefficient;
            p.portfolioPump         = portfolioPump;
            p.symbolCount           = symbolCount;
            p.coefficientK          = coefficientK;
            p.feeSpread             = feeSpread;
            p.deltaTime             = deltaTime;
            p.surplusRate           = surplusRate;
            p.horizonCount          = horizonCount;
            p.generateStopLosses    = generateStopLosses;
            p.maxRisk               = maxRisk;
            p.minRisk               = minRisk;
            return p;
        }
    };

    void saveParamsSnapshot(const ParamsRow& r)
    {
        auto j = readJsonArr(paramsPath());
        auto* a = j.as_array();
        if (!a) { j = njs3::json(njs3::js_array{}); a = j.as_array(); }
        njs3::json row(njs3::js_object{});
        row["calcType"] = JStr(r.calcType);
        row["symbol"] = JStr(r.symbol);
        row["tradeId"] = JI(r.tradeId);
        row["currentPrice"] = JD(r.currentPrice);
        row["quantity"] = JD(r.quantity);
        row["buyFees"] = JD(r.buyFees);
        row["sellFees"] = JD(r.sellFees);
        row["feeHedgingCoefficient"] = JD(r.feeHedgingCoefficient);
        row["portfolioPump"] = JD(r.portfolioPump);
        row["symbolCount"] = JI(r.symbolCount);
        row["coefficientK"] = JD(r.coefficientK);
        row["feeSpread"] = JD(r.feeSpread);
        row["deltaTime"] = JD(r.deltaTime);
        row["surplusRate"] = JD(r.surplusRate);
        row["horizonCount"] = JI(r.horizonCount);
        row["generateStopLosses"] = JB(r.generateStopLosses);
        row["riskCoefficient"] = JD(r.riskCoefficient);
        row["maxRisk"] = JD(r.maxRisk);
        row["minRisk"] = JD(r.minRisk);
        a->push_back(std::move(row));
        writeJson(paramsPath(), j);
    }

    std::vector<ParamsRow> loadParamsHistory() const
    {
        std::vector<ParamsRow> out;
        auto j = readJsonArr(paramsPath());
        const auto* a = j.as_array();
        if (!a) return out;
        for (const auto& item : *a)
        {
            ParamsRow r;
            r.calcType              = gs(item, "calcType");
            r.symbol                = gs(item, "symbol");
            r.tradeId               = gi(item, "tradeId");
            r.currentPrice          = gd(item, "currentPrice");
            r.quantity              = gd(item, "quantity");
            r.buyFees               = gd(item, "buyFees");
            r.sellFees              = gd(item, "sellFees");
            r.feeHedgingCoefficient = gd(item, "feeHedgingCoefficient");
            r.portfolioPump         = gd(item, "portfolioPump");
            r.symbolCount           = gi(item, "symbolCount");
            r.coefficientK          = gd(item, "coefficientK");
            r.feeSpread             = gd(item, "feeSpread");
            r.deltaTime             = gd(item, "deltaTime");
            r.surplusRate           = gd(item, "surplusRate");
            r.horizonCount          = gi(item, "horizonCount");
            r.generateStopLosses    = gb(item, "generateStopLosses");
            r.riskCoefficient       = gd(item, "riskCoefficient");
            r.maxRisk               = gd(item, "maxRisk");
            r.minRisk               = gd(item, "minRisk");
            out.push_back(r);
        }
        return out;
    }

    // ---- Pending Exits ----

    struct PendingExit
    {
        std::string symbol;
        int    orderId       = 0;
        int    tradeId       = 0;    // parent Buy trade
        double triggerPrice  = 0.0;  // market price must reach this to trigger
        double sellQty       = 0.0;  // quantity to sell when triggered
        int    levelIndex    = 0;
    };

    void savePendingExits(const std::vector<PendingExit>& orders)
    {
        njs3::js_array arr;
        for (const auto& o : orders)
        {
            njs3::json j(njs3::js_object{});
            j["symbol"] = JStr(o.symbol);
            j["orderId"] = JI(o.orderId);
            j["tradeId"] = JI(o.tradeId);
            j["triggerPrice"] = JD(o.triggerPrice);
            j["sellQty"] = JD(o.sellQty);
            j["levelIndex"] = JI(o.levelIndex);
            arr.push_back(std::move(j));
        }
        writeJson(pendingExitsPath(), njs3::json(std::move(arr)));
    }

    std::vector<PendingExit> loadPendingExits() const
    {
        std::vector<PendingExit> out;
        auto j = readJsonArr(pendingExitsPath());
        const auto* a = j.as_array();
        if (!a) return out;
        for (const auto& item : *a)
        {
            PendingExit o;
            o.symbol       = gs(item, "symbol");
            o.orderId      = gi(item, "orderId");
            o.tradeId      = gi(item, "tradeId");
            o.triggerPrice = gd(item, "triggerPrice");
            o.sellQty      = gd(item, "sellQty");
            o.levelIndex   = gi(item, "levelIndex");
            out.push_back(o);
        }
        return out;
    }

    void addPendingExits(const std::vector<PendingExit>& orders)
    {
        auto all = loadPendingExits();
        for (const auto& o : orders) all.push_back(o);
        savePendingExits(all);
    }

    void removePendingExit(int orderId)
    {
        auto all = loadPendingExits();
        std::erase_if(all, [orderId](const PendingExit& o) { return o.orderId == orderId; });
        savePendingExits(all);
        m_pendingIdGen.release(orderId);
    }

    int nextPendingId()
    {
        int id = m_pendingIdGen.acquire();
        m_pendingIdGen.commit(id);
        return id;
    }

    void releasePendingId(int id) { m_pendingIdGen.release(id); }

    IdGenerator&       pendingIdGen()       { return m_pendingIdGen; }
    const IdGenerator& pendingIdGen() const { return m_pendingIdGen; }

    // ---- Exit Points ----

    struct ExitPoint
    {
        int    exitId          = 0;
        int    tradeId         = 0;    // parent Buy trade
        std::string symbol;
        int    levelIndex      = 0;
        double tpPrice         = 0.0;  // per-unit take-profit price
        double slPrice         = 0.0;  // per-unit stop-loss price
        double sellQty         = 0.0;  // quantity to sell at this level
        double sellFraction    = 0.0;  // fraction of position (0-1)
        bool   slActive        = false;
        bool   executed        = false;
        int    linkedSellId    = -1;   // trade ID of the sell, if executed
    };

    void saveExitPoints(const std::vector<ExitPoint>& points)
    {
        njs3::js_array arr;
        for (const auto& ep : points)
        {
            njs3::json j(njs3::js_object{});
            j["exitId"] = JI(ep.exitId);
            j["tradeId"] = JI(ep.tradeId);
            j["symbol"] = JStr(ep.symbol);
            j["levelIndex"] = JI(ep.levelIndex);
            j["tpPrice"] = JD(ep.tpPrice);
            j["slPrice"] = JD(ep.slPrice);
            j["sellQty"] = JD(ep.sellQty);
            j["sellFraction"] = JD(ep.sellFraction);
            j["slActive"] = JB(ep.slActive);
            j["executed"] = JB(ep.executed);
            j["linkedSellId"] = JI(ep.linkedSellId);
            arr.push_back(std::move(j));
        }
        writeJson(exitPointsPath(), njs3::json(std::move(arr)));
    }

    std::vector<ExitPoint> loadExitPoints() const
    {
        std::vector<ExitPoint> out;
        auto j = readJsonArr(exitPointsPath());
        const auto* a = j.as_array();
        if (!a) return out;
        for (const auto& item : *a)
        {
            ExitPoint ep;
            ep.exitId        = gi(item, "exitId");
            ep.tradeId       = gi(item, "tradeId");
            ep.symbol        = gs(item, "symbol");
            ep.levelIndex    = gi(item, "levelIndex");
            ep.tpPrice       = gd(item, "tpPrice");
            ep.slPrice       = gd(item, "slPrice");
            ep.sellQty       = gd(item, "sellQty");
            ep.sellFraction  = gd(item, "sellFraction");
            ep.slActive      = gb(item, "slActive");
            ep.executed      = gb(item, "executed");
            ep.linkedSellId  = gi(item, "linkedSellId");
            out.push_back(ep);
        }
        return out;
    }

    std::vector<ExitPoint> loadExitPointsForTrade(int tradeId) const
    {
        std::vector<ExitPoint> out;
        for (const auto& ep : loadExitPoints())
            if (ep.tradeId == tradeId) out.push_back(ep);
        return out;
    }

    int nextExitId()
    {
        int id = m_exitIdGen.acquire();
        m_exitIdGen.commit(id);
        return id;
    }

    void releaseExitId(int id) { m_exitIdGen.release(id); }

    IdGenerator&       exitIdGen()       { return m_exitIdGen; }
    const IdGenerator& exitIdGen() const { return m_exitIdGen; }

    // ---- Entry Points ----

    struct EntryPoint
    {
        std::string symbol;
        int    entryId            = 0;
        int    levelIndex         = 0;
        double entryPrice         = 0.0;
        double breakEven          = 0.0;
        double funding            = 0.0;
        double fundingQty         = 0.0;
        double effectiveOverhead  = 0.0;
        bool   isShort            = false;
        bool   traded             = false;
        int    linkedTradeId      = -1;
        double exitTakeProfit     = 0.0;   // per-unit TP price
        double exitStopLoss       = 0.0;   // per-unit SL price
        double stopLossFraction   = 0.0;   // 0=SL off, >0=sell fraction
        bool   stopLossActive     = false;
    };

    void saveEntryPoints(const std::vector<EntryPoint>& points)
    {
        njs3::js_array arr;
        for (const auto& ep : points)
        {
            njs3::json j(njs3::js_object{});
            j["symbol"] = JStr(ep.symbol);
            j["entryId"] = JI(ep.entryId);
            j["levelIndex"] = JI(ep.levelIndex);
            j["entryPrice"] = JD(ep.entryPrice);
            j["breakEven"] = JD(ep.breakEven);
            j["funding"] = JD(ep.funding);
            j["fundingQty"] = JD(ep.fundingQty);
            j["effectiveOverhead"] = JD(ep.effectiveOverhead);
            j["isShort"] = JB(ep.isShort);
            j["traded"] = JB(ep.traded);
            j["linkedTradeId"] = JI(ep.linkedTradeId);
            j["exitTakeProfit"] = JD(ep.exitTakeProfit);
            j["exitStopLoss"] = JD(ep.exitStopLoss);
            j["stopLossFraction"] = JD(ep.stopLossFraction);
            j["stopLossActive"] = JB(ep.stopLossActive);
            arr.push_back(std::move(j));
        }
        writeJson(entryPointsPath(), njs3::json(std::move(arr)));
    }

    std::vector<EntryPoint> loadEntryPoints() const
    {
        std::vector<EntryPoint> out;
        auto j = readJsonArr(entryPointsPath());
        const auto* a = j.as_array();
        if (!a) return out;
        for (const auto& item : *a)
        {
            EntryPoint ep;
            ep.symbol            = gs(item, "symbol");
            ep.entryId           = gi(item, "entryId");
            ep.levelIndex        = gi(item, "levelIndex");
            ep.entryPrice        = gd(item, "entryPrice");
            ep.breakEven         = gd(item, "breakEven");
            ep.funding           = gd(item, "funding");
            ep.fundingQty        = gd(item, "fundingQty");
            ep.effectiveOverhead = gd(item, "effectiveOverhead");
            ep.isShort           = gb(item, "isShort");
            ep.traded            = gb(item, "traded");
            ep.linkedTradeId     = gi(item, "linkedTradeId");
            ep.exitTakeProfit    = gd(item, "exitTakeProfit");
            ep.exitStopLoss      = gd(item, "exitStopLoss");
            ep.stopLossFraction  = gd(item, "stopLossFraction");
            ep.stopLossActive    = (ep.stopLossFraction > 0.0);
            out.push_back(ep);
        }
        return out;
    }

    int nextEntryId()
    {
        int id = m_entryIdGen.acquire();
        m_entryIdGen.commit(id);
        return id;
    }

    void releaseEntryId(int id) { m_entryIdGen.release(id); }

    IdGenerator&       entryIdGen()       { return m_entryIdGen; }
    const IdGenerator& entryIdGen() const { return m_entryIdGen; }

    // ---- Wallet ----

    double loadWalletBalance() const
    {
        auto j = readJsonObj(walletPath());
        return static_cast<double>(j["balance"]->get_number_or(0.0L));
    }

    void saveWalletBalance(double balance)
    {
        njs3::json j(njs3::js_object{});
        j["balance"] = JD(balance);
        writeJson(walletPath(), j);
    }

    void deposit(double amount)
    {
        saveWalletBalance(loadWalletBalance() + amount);
    }

    void withdraw(double amount)
    {
        saveWalletBalance(loadWalletBalance() - amount);
    }

    double deployedCapital() const
    {
        double deployed = 0.0;
        for (const auto& t : loadTrades())
        {
            if (t.type != TradeType::Buy) continue;
            double sold = soldQuantityForParent(t.tradeId);
            double released = releasedForTrade(t.tradeId);
            double remaining = t.quantity - sold - released;
            if (remaining <= 0) continue;
            double remainFrac = t.quantity > 0 ? remaining / t.quantity : 0.0;
            deployed += QuantMath::cost(t.value, remaining) + t.buyFee * remainFrac;
        }
        return deployed;
    }

    // ---- Released Holdings ----

    double releasedForTrade(int tradeId) const
    {
        double total = 0.0;
        auto j = readJsonArr(releasedPath());
        const auto* a = j.as_array();
        if (!a) return 0.0;
        for (const auto& item : *a)
        {
            if (gi(item, "tradeId") == tradeId)
                total += gd(item, "qty");
        }
        return total;
    }

    bool releaseFromTrade(int tradeId, double qty)
    {
        auto trades = loadTrades();
        auto* t = findTradeById(trades, tradeId);
        if (!t || t->type != TradeType::Buy) return false;

        double sold = soldQuantityForParent(tradeId);
        double released = releasedForTrade(tradeId);
        double allocated = t->quantity - sold - released;
        if (qty > allocated + 1e-9) return false;
        if (qty > allocated) qty = allocated;

        auto j = readJsonArr(releasedPath());
        auto* a = j.as_array();
        if (!a) { j = njs3::json(njs3::js_array{}); a = j.as_array(); }
        njs3::json row(njs3::js_object{});
        row["symbol"] = JStr(t->symbol);
        row["tradeId"] = JI(tradeId);
        row["qty"] = JD(qty);
        a->push_back(std::move(row));
        writeJson(releasedPath(), j);
        return true;
    }

    bool hasBuyTrades() const
    {
        for (const auto& t : loadTrades())
            if (t.type == TradeType::Buy) return true;
        return false;
    }

    bool hasAnyHorizons() const
    {
        return !loadAllHorizons().empty();
    }

    // Compute net holdings for a symbol (total bought - total sold).
    double holdingsForSymbol(const std::string& symbol) const
    {
        double holdings = 0.0;
        for (const auto& t : loadTrades())
        {
            if (t.symbol != symbol) continue;
            if (t.type == TradeType::Buy)
                holdings += t.quantity;
            else
                holdings -= t.quantity;
        }
        return holdings;
    }

    // Execute a sell: deduct from the symbol's holdings, credit wallet with (proceeds - sellFee).
    // Returns the new trade ID, or -1 if validation fails.
    int executeSell(const std::string& symbol, double sellPrice, double sellQty, double sellFee = 0.0)
    {
        if (symbol.empty()) return -1;

        double remaining = holdingsForSymbol(symbol);
        if (sellQty > remaining + 1e-9) return -1;
        if (sellQty > remaining) sellQty = remaining;

        Trade sell;
        sell.tradeId       = nextTradeId();
        sell.symbol        = symbol;
        sell.type          = TradeType::CoveredSell;
        sell.value         = sellPrice;
        sell.quantity      = sellQty;
        sell.parentTradeId = -1;
        sell.sellFee       = sellFee;
        sell.timestamp     = static_cast<long long>(std::time(nullptr));
        addTrade(sell);

        double proceeds = sellPrice * sellQty - sellFee;
        deposit(proceeds);

        return sell.tradeId;
    }

    // Per-trade sell: validates against a specific parent Buy trade's remaining qty
    // instead of global holdings. Avoids floating-point issues with extreme quantities.
    int executeSellForTrade(const std::string& symbol, double sellPrice, double sellQty,
                           double sellFee, int parentTradeId)
    {
        if (symbol.empty()) return -1;

        auto trades = loadTrades();
        Trade* parent = nullptr;
        for (auto& t : trades)
            if (t.tradeId == parentTradeId) { parent = &t; break; }
        if (!parent || parent->type != TradeType::Buy || parent->symbol != symbol)
            return -1;

        double sold = soldQuantityForParent(parentTradeId);
        double remaining = parent->quantity - sold;
        if (sellQty > remaining + 1e-9) return -1;
        if (sellQty > remaining) sellQty = remaining;

        Trade sell;
        sell.tradeId       = nextTradeId();
        sell.symbol        = symbol;
        sell.type          = TradeType::CoveredSell;
        sell.value         = sellPrice;
        sell.quantity      = sellQty;
        sell.parentTradeId = parentTradeId;
        sell.sellFee       = sellFee;
        sell.timestamp     = static_cast<long long>(std::time(nullptr));
        addTrade(sell);

        double saleProceeds = QuantMath::proceeds(sellPrice, sellQty, sellFee);
        deposit(saleProceeds);

        // record realized P&L
        double gp = QuantMath::grossProfit(parent->value, sellPrice, sellQty);
        double np = QuantMath::netProfit(gp, 0.0, sellFee);
        recordPnl(symbol, sell.tradeId, parentTradeId,
                  parent->value, sellPrice, sellQty, gp, np);

        return sell.tradeId;
    }

    // Execute a buy: create a Buy trade, debit wallet for (cost + buyFee).
    // Returns the new trade ID.
    int executeBuy(const std::string& symbol, double price, double qty, double buyFee = 0.0)
    {
        Trade buy;
        buy.tradeId    = nextTradeId();
        buy.symbol     = symbol;
        buy.type       = TradeType::Buy;
        buy.value      = price;
        buy.quantity   = qty;
        buy.buyFee     = buyFee;
        buy.timestamp  = static_cast<long long>(std::time(nullptr));
        addTrade(buy);

        double buyCost = QuantMath::cost(price, qty) + buyFee;
        withdraw(buyCost);

        return buy.tradeId;
    }

    // ---- Price series seeding ----

    // Populate a PriceSeries with trade timestamps + prices.
    // Useful for backtesting: trades become historical data points.
    void seedPriceSeries(PriceSeries& ps) const
    {
        for (const auto& t : loadTrades())
        {
            if (t.timestamp > 0 && t.value > 0.0)
                ps.set(t.symbol, t.timestamp, t.value);
        }
    }

    // ---- Housekeeping ----

    void exportHtmlReport(const std::string& path) const
    {
        std::ofstream f(path, std::ios::trunc);
        if (!f) throw std::runtime_error("Cannot open " + path);

        f << std::fixed << std::setprecision(17);

        f << R"(<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>Quant Trade Report</title>
<style>
body{font-family:monospace;background:#0b1426;color:#cbd5e1;padding:20px}
h1{color:#c9a44a}h2{color:#7b97c4;border-bottom:1px solid #1a2744;padding-bottom:4px}
table{border-collapse:collapse;margin:10px 0;width:100%}
th,td{border:1px solid #1a2744;padding:6px 10px;text-align:right}
th{background:#0f1b2d;color:#c9a44a}
tr:nth-child(even){background:#0f1b2d}
.buy{color:#22c55e}.sell{color:#ef4444}.pos{color:#22c55e}.neg{color:#ef4444}
.wallet{background:#0f1b2d;padding:12px;border-radius:6px;margin:10px 0;display:inline-block}
</style></head><body>
<h1>Quant Trade Report</h1>
)";

        // Wallet
        double wb = loadWalletBalance();
        double dc = deployedCapital();
        f << "<h2>Wallet</h2><div class='wallet'>"
          << "Liquid: <b>" << wb << "</b> &nbsp; "
          << "Deployed: <b>" << dc << "</b> &nbsp; "
          << "Total: <b>" << (wb + dc) << "</b></div>\n";

        // Trades
        auto trades = loadTrades();
        f << "<h2>Trades</h2>\n";
        if (trades.empty()) { f << "<p>(none)</p>\n"; }
        else
        {
            f << "<table><tr><th>ID</th><th>Symbol</th><th>Type</th>"
              << "<th>Price</th><th>Qty</th><th>Parent</th>"
              << "<th>Buy Fee</th><th>Sell Fee</th></tr>\n";
            for (const auto& t : trades)
            {
                f << "<tr><td>" << t.tradeId << "</td>"
                  << "<td>" << t.symbol << "</td>"
                  << "<td class='" << (t.type == TradeType::Buy ? "buy" : "sell") << "'>"
                  << (t.type == TradeType::Buy ? "BUY" : "COVERED_SELL") << "</td>"
                  << "<td>" << t.value << "</td>"
                  << "<td>" << t.quantity << "</td>"
                  << "<td>" << (t.parentTradeId >= 0 ? std::to_string(t.parentTradeId) : "-") << "</td>"
                  << "<td>" << t.buyFee << "</td>"
                  << "<td>" << t.sellFee << "</td></tr>\n";
            }
            f << "</table>\n";
        }

        // Horizon Levels
        auto hl = loadAllHorizons();
        f << "<h2>Horizon Levels</h2>\n";
        if (hl.empty()) { f << "<p>(none)</p>\n"; }
        else
        {
            f << "<table><tr><th>Symbol</th><th>Trade</th><th>Level</th>"
              << "<th>TP</th><th>SL</th><th>SL Active</th></tr>\n";
            for (const auto& [sym, tid, lv] : hl)
            {
                f << "<tr><td>" << sym << "</td>"
                  << "<td>" << tid << "</td>"
                  << "<td>" << lv.index << "</td>"
                  << "<td>" << lv.takeProfit << "</td>"
                  << "<td>" << lv.stopLoss << "</td>"
                  << "<td>" << (lv.stopLossActive ? "ON" : "OFF") << "</td></tr>\n";
            }
            f << "</table>\n";
        }

        // Profit History
        auto ph = loadProfitHistory();
        f << "<h2>Profit History</h2>\n";
        if (ph.empty()) { f << "<p>(none)</p>\n"; }
        else
        {
            f << "<table><tr><th>Trade</th><th>Symbol</th><th>Price</th>"
              << "<th>Gross</th><th>Net</th><th>ROI%</th></tr>\n";
            for (const auto& r : ph)
            {
                f << "<tr><td>" << r.tradeId << "</td>"
                  << "<td>" << r.symbol << "</td>"
                  << "<td>" << r.currentPrice << "</td>"
                  << "<td class='" << (r.grossProfit >= 0 ? "pos" : "neg") << "'>"
                  << r.grossProfit << "</td>"
                  << "<td class='" << (r.netProfit >= 0 ? "pos" : "neg") << "'>"
                  << r.netProfit << "</td>"
                  << "<td class='" << (r.roi >= 0 ? "pos" : "neg") << "'>"
                  << r.roi << "%</td></tr>\n";
            }
            f << "</table>\n";
        }

        // Parameter History
        auto pm = loadParamsHistory();
        f << "<h2>Parameter History</h2>\n";
        if (pm.empty()) { f << "<p>(none)</p>\n"; }
        else
        {
            f << std::setprecision(17);
            f << "<table><tr><th>Type</th><th>Symbol</th><th>Trade</th>"
              << "<th>Price</th><th>Qty</th><th>Levels</th>"
              << "<th>BuyF</th><th>SellF</th><th>Hedge</th>"
              << "<th>Pump</th><th>Syms</th><th>K</th>"
              << "<th>Spread</th><th>dt</th><th>Surplus</th></tr>\n";
            for (const auto& r : pm)
            {
                f << "<tr><td>" << r.calcType << "</td>"
                  << "<td>" << r.symbol << "</td>"
                  << "<td>" << (r.tradeId >= 0 ? std::to_string(r.tradeId) : "-") << "</td>"
                  << "<td>" << r.currentPrice << "</td>"
                  << "<td>" << r.quantity << "</td>"
                  << "<td>" << r.horizonCount << "</td>"
                  << "<td>" << r.buyFees << "</td>"
                  << "<td>" << r.sellFees << "</td>"
                  << "<td>" << r.feeHedgingCoefficient << "</td>"
                  << "<td>" << r.portfolioPump << "</td>"
                  << "<td>" << r.symbolCount << "</td>"
                  << "<td>" << r.coefficientK << "</td>"
                  << "<td>" << r.feeSpread << "</td>"
                  << "<td>" << r.deltaTime << "</td>"
                  << "<td>" << r.surplusRate << "</td></tr>\n";
            }
            f << "</table>\n";
        }

        f << "</body></html>\n";
    }

    void exportReport(const std::string& path) const
    {
        std::ofstream f(path, std::ios::trunc);
        if (!f) throw std::runtime_error("Cannot open " + path);

        f << std::fixed << std::setprecision(17);

        f << "====== QUANT TRADE REPORT ======\n\n";

        f << "--- WALLET ---\n";
        double wb = loadWalletBalance();
        double dc = deployedCapital();
        f << "  balance=" << wb
          << "  deployed=" << dc
          << "  total=" << (wb + dc) << "\n";

        f << "\n--- TRADES ---\n";
        auto trades = loadTrades();
        if (trades.empty()) f << "(none)\n";
        for (const auto& t : trades)
        {
            f << "#" << t.tradeId
              << "  " << t.symbol
              << "  " << (t.type == TradeType::Buy ? "BUY" : "COVERED_SELL")
              << "  price=" << t.value
              << "  qty=" << t.quantity;
            if (t.parentTradeId >= 0)
                f << "  parent=#" << t.parentTradeId;
            if (t.buyFee > 0) f << "  buyFee=" << t.buyFee;
            if (t.sellFee > 0) f << "  sellFee=" << t.sellFee;
            f << "  TP=" << t.takeProfit
              << " [TP " << (t.takeProfitFraction * 100) << "%]"
              << "  SL=" << t.stopLoss
              << " [SL " << (t.stopLossFraction * 100) << "%]"
              << "\n";
        }

        f << "\n--- HORIZON LEVELS ---\n";
        auto hl = loadAllHorizons();
        if (hl.empty()) f << "(none)\n";
        for (const auto& [sym, tid, lv] : hl)
        {
            f << sym << " #" << tid
              << "  [" << lv.index << "]"
              << "  TP=" << lv.takeProfit
              << "  SL=" << lv.stopLoss
              << (lv.stopLossActive ? " [ON]" : " [OFF]")
              << "\n";
        }

        f << "\n--- PROFIT HISTORY ---\n";
        auto ph = loadProfitHistory();
        if (ph.empty()) f << "(none)\n";
        for (const auto& r : ph)
        {
            f << "#" << r.tradeId
              << "  " << r.symbol
              << "  @" << r.currentPrice
              << "  gross=" << r.grossProfit
              << "  net=" << r.netProfit
              << "  ROI=" << r.roi << "%"
              << "\n";
        }

        f << "\n--- PARAMETER HISTORY ---\n";
        auto pm = loadParamsHistory();
        if (pm.empty()) f << "(none)\n";
        f << std::setprecision(17);
        for (const auto& r : pm)
        {
            f << r.calcType << "  " << r.symbol;
            if (r.tradeId >= 0) f << " #" << r.tradeId;
            f << "  price=" << r.currentPrice
              << " qty=" << r.quantity
              << " levels=" << r.horizonCount
              << " buyF=" << r.buyFees
              << " sellF=" << r.sellFees
              << " hedge=" << r.feeHedgingCoefficient
              << " pump=" << r.portfolioPump
              << " syms=" << r.symbolCount
              << " K=" << r.coefficientK
              << " spread=" << r.feeSpread
              << " dt=" << r.deltaTime
              << " surplus=" << r.surplusRate;
            if (r.calcType == "horizon")
                f << " SL=" << (r.generateStopLosses ? "yes" : "no");
            if (r.calcType == "entry" || r.calcType == "exit")
                f << " risk=" << r.riskCoefficient;
            f << "\n";
        }

        f << "\n====== END OF REPORT ======\n";
    }

    // ---- Parameter Models (named presets) ----

    struct ParamModel
    {
        std::string name;
        int    levels                     = 4;
        double risk                       = 0.5;
        double steepness                  = 6.0;
        double feeHedgingCoefficient      = 1.0;
        double portfolioPump              = 0.0;
        int    symbolCount                = 1;
        double coefficientK               = 0.0;
        double feeSpread                  = 0.0;
        double deltaTime                  = 1.0;
        double surplusRate                = 0.02;
        double maxRisk                    = 0.0;
        double minRisk                    = 0.0;
        bool   isShort                    = false;
        int    fundMode                   = 1;
        bool   generateStopLosses         = false;
        double rangeAbove                 = 0.0;
        double rangeBelow                 = 0.0;
        int    futureTradeCount           = 0;
        double stopLossFraction           = 1.0;
        int    stopLossHedgeCount         = 0;
    };

    void saveParamModels(const std::vector<ParamModel>& models)
    {
        njs3::js_array arr;
        for (const auto& m : models)
        {
            njs3::json j(njs3::js_object{});
            j["name"] = JStr(m.name);
            j["levels"] = JI(m.levels);
            j["risk"] = JD(m.risk);
            j["steepness"] = JD(m.steepness);
            j["feeHedgingCoefficient"] = JD(m.feeHedgingCoefficient);
            j["portfolioPump"] = JD(m.portfolioPump);
            j["symbolCount"] = JI(m.symbolCount);
            j["coefficientK"] = JD(m.coefficientK);
            j["feeSpread"] = JD(m.feeSpread);
            j["deltaTime"] = JD(m.deltaTime);
            j["surplusRate"] = JD(m.surplusRate);
            j["maxRisk"] = JD(m.maxRisk);
            j["minRisk"] = JD(m.minRisk);
            j["isShort"] = JB(m.isShort);
            j["fundMode"] = JI(m.fundMode);
            j["generateStopLosses"] = JB(m.generateStopLosses);
            j["rangeAbove"] = JD(m.rangeAbove);
            j["rangeBelow"] = JD(m.rangeBelow);
            j["futureTradeCount"] = JI(m.futureTradeCount);
            j["stopLossFraction"] = JD(m.stopLossFraction);
            j["stopLossHedgeCount"] = JI(m.stopLossHedgeCount);
            arr.push_back(std::move(j));
        }
        writeJson(paramModelsPath(), njs3::json(std::move(arr)));
    }

    std::vector<ParamModel> loadParamModels() const
    {
        std::vector<ParamModel> out;
        auto j = readJsonArr(paramModelsPath());
        const auto* a = j.as_array();
        if (!a) return out;
        for (const auto& item : *a)
        {
            ParamModel m;
            m.name                  = gs(item, "name");
            m.levels                = gi(item, "levels");
            m.risk                  = gd(item, "risk");
            m.steepness             = gd(item, "steepness");
            m.feeHedgingCoefficient = gd(item, "feeHedgingCoefficient");
            m.portfolioPump         = gd(item, "portfolioPump");
            m.symbolCount           = gi(item, "symbolCount");
            m.coefficientK          = gd(item, "coefficientK");
            m.feeSpread             = gd(item, "feeSpread");
            m.deltaTime             = gd(item, "deltaTime");
            m.surplusRate           = gd(item, "surplusRate");
            m.maxRisk               = gd(item, "maxRisk");
            m.minRisk               = gd(item, "minRisk");
            m.isShort               = gb(item, "isShort");
            m.fundMode              = gi(item, "fundMode");
            m.generateStopLosses    = gb(item, "generateStopLosses");
            m.rangeAbove            = gd(item, "rangeAbove");
            m.rangeBelow            = gd(item, "rangeBelow");
            m.futureTradeCount      = gi(item, "futureTradeCount");
            m.stopLossFraction      = gd(item, "stopLossFraction");
            m.stopLossHedgeCount    = gi(item, "stopLossHedgeCount");
            out.push_back(m);
        }
        return out;
    }

    void addParamModel(const ParamModel& model)
    {
        auto all = loadParamModels();
        // overwrite if name already exists
        std::erase_if(all, [&](const ParamModel& m) { return m.name == model.name; });
        all.push_back(model);
        saveParamModels(all);
    }

    void removeParamModel(const std::string& name)
    {
        auto all = loadParamModels();
        std::erase_if(all, [&](const ParamModel& m) { return m.name == name; });
        saveParamModels(all);
    }

    const ParamModel* findParamModel(const std::vector<ParamModel>& models,
                                     const std::string& name) const
    {
        for (const auto& m : models)
            if (m.name == name) return &m;
        return nullptr;
    }

    // ---- P&L Ledger ----

    struct PnlEntry
    {
        long long   timestamp    = 0;   // unix seconds
        std::string symbol;
        int         sellTradeId  = 0;
        int         parentTradeId= 0;
        double      entryPrice   = 0.0;
        double      sellPrice    = 0.0;
        double      quantity     = 0.0;
        double      grossProfit  = 0.0;
        double      netProfit    = 0.0;
        double      cumProfit    = 0.0; // cumulative net at this point
    };

    void recordPnl(const std::string& symbol, int sellId, int parentId,
                   double entryPrice, double sellPrice, double qty,
                   double grossProfit, double netProfit)
    {
        auto history = loadPnl();
        double cum = history.empty() ? 0.0 : history.back().cumProfit;
        cum += netProfit;

        auto now = std::chrono::system_clock::now();
        long long ts = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();

        auto j = readJsonArr(pnlPath());
        auto* a = j.as_array();
        if (!a) { j = njs3::json(njs3::js_array{}); a = j.as_array(); }
        njs3::json row(njs3::js_object{});
        row["timestamp"] = JLL(ts);
        row["symbol"] = JStr(symbol);
        row["sellTradeId"] = JI(sellId);
        row["parentTradeId"] = JI(parentId);
        row["entryPrice"] = JD(entryPrice);
        row["sellPrice"] = JD(sellPrice);
        row["quantity"] = JD(qty);
        row["grossProfit"] = JD(grossProfit);
        row["netProfit"] = JD(netProfit);
        row["cumProfit"] = JD(cum);
        a->push_back(std::move(row));
        writeJson(pnlPath(), j);
    }

    std::vector<PnlEntry> loadPnl() const
    {
        std::vector<PnlEntry> out;
        auto j = readJsonArr(pnlPath());
        const auto* a = j.as_array();
        if (!a) return out;
        for (const auto& item : *a)
        {
            PnlEntry e;
            e.timestamp     = gll(item, "timestamp");
            e.symbol        = gs(item, "symbol");
            e.sellTradeId   = gi(item, "sellTradeId");
            e.parentTradeId = gi(item, "parentTradeId");
            e.entryPrice    = gd(item, "entryPrice");
            e.sellPrice     = gd(item, "sellPrice");
            e.quantity      = gd(item, "quantity");
            e.grossProfit   = gd(item, "grossProfit");
            e.netProfit     = gd(item, "netProfit");
            e.cumProfit     = gd(item, "cumProfit");
            out.push_back(e);
        }
        return out;
    }

    // ---- Chain Execution State ----

    struct ChainState
    {
        std::string symbol;
        int    currentCycle    = 0;
        double totalSavings   = 0.0;
        double savingsRate    = 0.0;
        bool   active         = false;
    };

    struct ChainMember
    {
        int cycle    = 0;
        int entryId  = 0;
    };

    void saveChainState(const ChainState& state)
    {
        njs3::json j(njs3::js_object{});
        j["symbol"] = JStr(state.symbol);
        j["currentCycle"] = JI(state.currentCycle);
        j["totalSavings"] = JD(state.totalSavings);
        j["savingsRate"] = JD(state.savingsRate);
        j["active"] = JB(state.active);
        writeJson(chainStatePath(), j);
    }

    ChainState loadChainState() const
    {
        ChainState state;
        auto j = readJsonObj(chainStatePath());
        if (!j->is_object()) return state;
        state.symbol       = gs(j, "symbol");
        state.currentCycle = gi(j, "currentCycle");
        state.totalSavings = gd(j, "totalSavings");
        state.savingsRate  = gd(j, "savingsRate");
        state.active       = gb(j, "active");
        return state;
    }

    void saveChainMembers(const std::vector<ChainMember>& members)
    {
        njs3::js_array arr;
        for (const auto& m : members)
        {
            njs3::json j(njs3::js_object{});
            j["cycle"] = JI(m.cycle);
            j["entryId"] = JI(m.entryId);
            arr.push_back(std::move(j));
        }
        writeJson(chainMembersPath(), njs3::json(std::move(arr)));
    }

    std::vector<ChainMember> loadChainMembers() const
    {
        std::vector<ChainMember> out;
        auto j = readJsonArr(chainMembersPath());
        const auto* a = j.as_array();
        if (!a) return out;
        for (const auto& item : *a)
        {
            ChainMember m;
            m.cycle   = gi(item, "cycle");
            m.entryId = gi(item, "entryId");
            out.push_back(m);
        }
        return out;
    }

    void addChainMembers(int cycle, const std::vector<int>& entryIds)
    {
        auto all = loadChainMembers();
        for (int id : entryIds)
        {
            ChainMember m;
            m.cycle   = cycle;
            m.entryId = id;
            all.push_back(m);
        }
        saveChainMembers(all);
    }

    void clearAll()
    {
        // Remove JSON files
        std::filesystem::remove(tradesPath());
        std::filesystem::remove(horizonsPath());
        std::filesystem::remove(profitsPath());
        std::filesystem::remove(paramsPath());
        std::filesystem::remove(walletPath());
        std::filesystem::remove(pendingExitsPath());
        std::filesystem::remove(entryPointsPath());
        std::filesystem::remove(releasedPath());
        std::filesystem::remove(paramModelsPath());
        std::filesystem::remove(pnlPath());
        std::filesystem::remove(chainStatePath());
        std::filesystem::remove(chainMembersPath());
        std::filesystem::remove(exitPointsPath());
        // Also remove legacy CSV files if present
        for (const char* ext : {".csv"})
        {
            for (const char* name : {"trades", "horizons", "profits", "params",
                "wallet", "pending_exits", "entry_points", "released",
                "param_models", "pnl", "chain_state", "chain_members", "exit_points"})
                std::filesystem::remove(m_dir + "/" + name + ext);
        }
        seedIdGenerators();
    }

private:
std::string  m_dir;
IdGenerator  m_tradeIdGen;
IdGenerator  m_pendingIdGen;
IdGenerator  m_entryIdGen;
IdGenerator  m_exitIdGen;

    std::string tradesPath()       const { return m_dir + "/trades.json"; }
    std::string horizonsPath()     const { return m_dir + "/horizons.json"; }
    std::string profitsPath()      const { return m_dir + "/profits.json"; }
    std::string paramsPath()       const { return m_dir + "/params.json"; }
    std::string walletPath()       const { return m_dir + "/wallet.json"; }
    std::string pendingExitsPath() const { return m_dir + "/pending_exits.json"; }
    std::string entryPointsPath()  const { return m_dir + "/entry_points.json"; }
    std::string releasedPath()     const { return m_dir + "/released.json"; }
    std::string paramModelsPath()  const { return m_dir + "/param_models.json"; }
    std::string pnlPath()          const { return m_dir + "/pnl.json"; }
    std::string chainStatePath()   const { return m_dir + "/chain_state.json"; }
    std::string chainMembersPath() const { return m_dir + "/chain_members.json"; }
    std::string exitPointsPath()   const { return m_dir + "/exit_points.json"; }

    // ---- JSON I/O helpers ----
    static njs3::json readJsonArr(const std::string& path)
    {
        std::ifstream f(path);
        if (!f) return njs3::json(njs3::js_array{});
        std::string c((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (c.empty()) return njs3::json(njs3::js_array{});
        try { return njs3::parse_json(c); }
        catch (...) { return njs3::json(njs3::js_array{}); }
    }
    static njs3::json readJsonObj(const std::string& path)
    {
        std::ifstream f(path);
        if (!f) return njs3::json(njs3::js_object{});
        std::string c((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (c.empty()) return njs3::json(njs3::js_object{});
        try { return njs3::parse_json(c); }
        catch (...) { return njs3::json(njs3::js_object{}); }
    }
    static void writeJson(const std::string& path, const njs3::json& j)
    {
        std::ofstream f(path, std::ios::trunc);
        if (!f) throw std::runtime_error("Cannot open " + path);
        f << njs3::serialize_json(j, njs3::json_serialize_option::pretty,
            njs3::json_floating_format_options{std::chars_format::general, 17});
    }
    // Value constructors
    static njs3::json JI(int v)                { return njs3::json(static_cast<njs3::js_integer>(v)); }
    static njs3::json JD(double v)             { return njs3::json(static_cast<njs3::js_floating>(v)); }
    static njs3::json JLL(long long v)         { return njs3::json(static_cast<njs3::js_integer>(v)); }
    static njs3::json JB(bool v)               { return njs3::json(v); }
    static njs3::json JStr(const std::string& v) { return njs3::json(njs3::js_string(v)); }
    // Read helpers
    static int         gi(const njs3::json& j, const char* k) { return static_cast<int>(j[k]->get_integer_or(0LL)); }
    static double      gd(const njs3::json& j, const char* k) { return static_cast<double>(j[k]->get_number_or(0.0L)); }
    static long long  gll(const njs3::json& j, const char* k) { return j[k]->get_integer_or(0LL); }
    static bool        gb(const njs3::json& j, const char* k) { return j[k]->get_boolean_or(false); }
    static std::string gs(const njs3::json& j, const char* k) { return j[k]->get_string_or(njs3::js_string("")); }

    using HorizonRow = std::tuple<std::string, int, HorizonLevel>;

    std::vector<HorizonRow> loadAllHorizons() const
    {
        std::vector<HorizonRow> out;
        auto j = readJsonArr(horizonsPath());
        const auto* a = j.as_array();
        if (!a) return out;
        for (const auto& item : *a)
        {
            HorizonLevel lv;
            lv.index         = gi(item, "index");
            lv.takeProfit    = gd(item, "takeProfit");
            lv.stopLoss      = gd(item, "stopLoss");
            lv.stopLossActive = gb(item, "stopLossActive");
            out.emplace_back(gs(item, "symbol"), gi(item, "tradeId"), lv);
        }
        return out;
    }

    void saveAllHorizons(const std::vector<HorizonRow>& rows)
    {
        njs3::js_array arr;
        for (const auto& [sym, tid, lv] : rows)
        {
            njs3::json j(njs3::js_object{});
            j["symbol"] = JStr(sym);
            j["tradeId"] = JI(tid);
            j["index"] = JI(lv.index);
            j["takeProfit"] = JD(lv.takeProfit);
            j["stopLoss"] = JD(lv.stopLoss);
            j["stopLossActive"] = JB(lv.stopLossActive);
            arr.push_back(std::move(j));
        }
        writeJson(horizonsPath(), njs3::json(std::move(arr)));
    }
};

