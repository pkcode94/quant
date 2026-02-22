#pragma once

#include "Trade.h"
#include "MultiHorizonEngine.h"
#include "ProfitCalculator.h"

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <stdexcept>
#include <iomanip>
#include <algorithm>

class TradeDatabase
{
public:
    explicit TradeDatabase(const std::string& directory = "db")
        : m_dir(directory)
    {
        std::filesystem::create_directories(m_dir);
    }

    // ---- Trades ----

    void saveTrades(const std::vector<Trade>& trades)
    {
        std::ofstream f(tradesPath(), std::ios::trunc);
        if (!f) throw std::runtime_error("Cannot open " + tradesPath());

        for (const auto& t : trades)
        {
            f << t.symbol       << ','
              << t.tradeId      << ','
              << static_cast<int>(t.type) << ','
              << std::fixed << std::setprecision(8)
              << t.value        << ','
              << t.quantity     << ','
              << t.parentTradeId << ','
              << t.takeProfit   << ','
              << t.stopLoss     << ','
              << t.stopLossActive << ','
              << t.shortEnabled
              << '\n';
        }
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
            if (t.symbol == updated.symbol && t.tradeId == updated.tradeId)
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
        std::ifstream f(tradesPath());
        if (!f) return out;

        std::string line;
        while (std::getline(f, line))
        {
            if (line.empty()) continue;
            std::istringstream ss(line);
            std::string tok;
            Trade t;

            std::getline(ss, t.symbol, ',');
            std::getline(ss, tok, ','); t.tradeId       = std::stoi(tok);
            std::getline(ss, tok, ','); t.type           = static_cast<TradeType>(std::stoi(tok));
            std::getline(ss, tok, ','); t.value          = std::stod(tok);
            std::getline(ss, tok, ','); t.quantity       = std::stod(tok);
            std::getline(ss, tok, ','); t.parentTradeId  = std::stoi(tok);
            std::getline(ss, tok, ','); t.takeProfit     = std::stod(tok);
            std::getline(ss, tok, ','); t.stopLoss       = std::stod(tok);
            std::getline(ss, tok, ','); t.stopLossActive = (std::stoi(tok) != 0);
            std::getline(ss, tok, ','); t.shortEnabled   = (std::stoi(tok) != 0);

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

    int nextTradeId() const
    {
        auto all = loadTrades();
        int maxId = 0;
        for (const auto& t : all)
            if (t.tradeId > maxId) maxId = t.tradeId;
        return maxId + 1;
    }

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
        std::ofstream f(profitsPath(), std::ios::app);
        if (!f) throw std::runtime_error("Cannot open " + profitsPath());

        f << symbol       << ','
          << tradeId      << ','
          << std::fixed << std::setprecision(8)
          << currentPrice << ','
          << r.grossProfit << ','
          << r.netProfit   << ','
          << r.roi
          << '\n';
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
        std::ifstream f(profitsPath());
        if (!f) return out;

        std::string line;
        while (std::getline(f, line))
        {
            if (line.empty()) continue;
            std::istringstream ss(line);
            std::string tok;
            ProfitRow r;
            std::getline(ss, r.symbol, ',');
            std::getline(ss, tok, ','); r.tradeId      = std::stoi(tok);
            std::getline(ss, tok, ','); r.currentPrice = std::stod(tok);
            std::getline(ss, tok, ','); r.grossProfit  = std::stod(tok);
            std::getline(ss, tok, ','); r.netProfit    = std::stod(tok);
            std::getline(ss, tok, ','); r.roi          = std::stod(tok);
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

        static ParamsRow from(const std::string& type, const std::string& sym,
                              int tid, double price, double qty,
                              const HorizonParams& p, double risk = 0.0)
        {
            ParamsRow r;
            r.calcType              = type;
            r.symbol                = sym;
            r.tradeId               = tid;
            r.currentPrice          = price;
            r.quantity              = qty;
            r.buyFees               = p.buyFees;
            r.sellFees              = p.sellFees;
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
            return r;
        }

        HorizonParams toHorizonParams() const
        {
            HorizonParams p;
            p.buyFees               = buyFees;
            p.sellFees              = sellFees;
            p.feeHedgingCoefficient = feeHedgingCoefficient;
            p.portfolioPump         = portfolioPump;
            p.symbolCount           = symbolCount;
            p.coefficientK          = coefficientK;
            p.feeSpread             = feeSpread;
            p.deltaTime             = deltaTime;
            p.surplusRate           = surplusRate;
            p.horizonCount          = horizonCount;
            p.generateStopLosses    = generateStopLosses;
            return p;
        }
    };

    void saveParamsSnapshot(const ParamsRow& r)
    {
        std::ofstream f(paramsPath(), std::ios::app);
        if (!f) throw std::runtime_error("Cannot open " + paramsPath());

        f << r.calcType     << ','
          << r.symbol       << ','
          << r.tradeId      << ','
          << std::fixed << std::setprecision(8)
          << r.currentPrice << ','
          << r.quantity     << ','
          << r.buyFees      << ','
          << r.sellFees     << ','
          << r.feeHedgingCoefficient << ','
          << r.portfolioPump << ','
          << r.symbolCount  << ','
          << r.coefficientK << ','
          << r.feeSpread    << ','
          << r.deltaTime    << ','
          << r.surplusRate  << ','
          << r.horizonCount << ','
          << r.generateStopLosses << ','
          << r.riskCoefficient
          << '\n';
    }

    std::vector<ParamsRow> loadParamsHistory() const
    {
        std::vector<ParamsRow> out;
        std::ifstream f(paramsPath());
        if (!f) return out;

        std::string line;
        while (std::getline(f, line))
        {
            if (line.empty()) continue;
            std::istringstream ss(line);
            std::string tok;
            ParamsRow r;

            std::getline(ss, r.calcType, ',');
            std::getline(ss, r.symbol, ',');
            std::getline(ss, tok, ','); r.tradeId                    = std::stoi(tok);
            std::getline(ss, tok, ','); r.currentPrice               = std::stod(tok);
            std::getline(ss, tok, ','); r.quantity                   = std::stod(tok);
            std::getline(ss, tok, ','); r.buyFees                    = std::stod(tok);
            std::getline(ss, tok, ','); r.sellFees                   = std::stod(tok);
            std::getline(ss, tok, ','); r.feeHedgingCoefficient      = std::stod(tok);
            std::getline(ss, tok, ','); r.portfolioPump              = std::stod(tok);
            std::getline(ss, tok, ','); r.symbolCount                = std::stoi(tok);
            std::getline(ss, tok, ','); r.coefficientK               = std::stod(tok);
            std::getline(ss, tok, ','); r.feeSpread                  = std::stod(tok);
            std::getline(ss, tok, ','); r.deltaTime                  = std::stod(tok);
            std::getline(ss, tok, ','); r.surplusRate                = std::stod(tok);
            std::getline(ss, tok, ','); r.horizonCount               = std::stoi(tok);
            std::getline(ss, tok, ','); r.generateStopLosses         = (std::stoi(tok) != 0);
            std::getline(ss, tok, ','); r.riskCoefficient            = std::stod(tok);

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
        std::ofstream f(pendingExitsPath(), std::ios::trunc);
        if (!f) throw std::runtime_error("Cannot open " + pendingExitsPath());
        for (const auto& o : orders)
        {
            f << o.symbol      << ','
              << o.orderId     << ','
              << o.tradeId     << ','
              << std::fixed << std::setprecision(8)
              << o.triggerPrice << ','
              << o.sellQty     << ','
              << o.levelIndex  << '\n';
        }
    }

    std::vector<PendingExit> loadPendingExits() const
    {
        std::vector<PendingExit> out;
        std::ifstream f(pendingExitsPath());
        if (!f) return out;
        std::string line;
        while (std::getline(f, line))
        {
            if (line.empty()) continue;
            std::istringstream ss(line);
            std::string tok;
            PendingExit o;
            std::getline(ss, o.symbol, ',');
            std::getline(ss, tok, ','); o.orderId      = std::stoi(tok);
            std::getline(ss, tok, ','); o.tradeId      = std::stoi(tok);
            std::getline(ss, tok, ','); o.triggerPrice  = std::stod(tok);
            std::getline(ss, tok, ','); o.sellQty       = std::stod(tok);
            std::getline(ss, tok, ','); o.levelIndex    = std::stoi(tok);
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
    }

    int nextPendingId() const
    {
        int maxId = 0;
        for (const auto& o : loadPendingExits())
            if (o.orderId > maxId) maxId = o.orderId;
        return maxId + 1;
    }

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
        bool   stopLossActive     = false;
    };

    void saveEntryPoints(const std::vector<EntryPoint>& points)
    {
        std::ofstream f(entryPointsPath(), std::ios::trunc);
        if (!f) throw std::runtime_error("Cannot open " + entryPointsPath());
        for (const auto& ep : points)
        {
            f << ep.symbol             << ','
              << ep.entryId            << ','
              << ep.levelIndex         << ','
              << std::fixed << std::setprecision(8)
              << ep.entryPrice         << ','
              << ep.breakEven          << ','
              << ep.funding            << ','
              << ep.fundingQty         << ','
              << ep.effectiveOverhead  << ','
              << ep.isShort            << ','
              << ep.traded             << ','
              << ep.linkedTradeId      << ','
              << ep.exitTakeProfit     << ','
              << ep.exitStopLoss       << ','
              << ep.stopLossActive
              << '\n';
        }
    }

    std::vector<EntryPoint> loadEntryPoints() const
    {
        std::vector<EntryPoint> out;
        std::ifstream f(entryPointsPath());
        if (!f) return out;
        std::string line;
        while (std::getline(f, line))
        {
            if (line.empty()) continue;
            std::istringstream ss(line);
            std::string tok;
            EntryPoint ep;
            std::getline(ss, ep.symbol, ',');
            std::getline(ss, tok, ','); ep.entryId           = std::stoi(tok);
            std::getline(ss, tok, ','); ep.levelIndex        = std::stoi(tok);
            std::getline(ss, tok, ','); ep.entryPrice        = std::stod(tok);
            std::getline(ss, tok, ','); ep.breakEven         = std::stod(tok);
            std::getline(ss, tok, ','); ep.funding           = std::stod(tok);
            std::getline(ss, tok, ','); ep.fundingQty        = std::stod(tok);
            std::getline(ss, tok, ','); ep.effectiveOverhead = std::stod(tok);
            std::getline(ss, tok, ','); ep.isShort           = (std::stoi(tok) != 0);
            std::getline(ss, tok, ','); ep.traded            = (std::stoi(tok) != 0);
            std::getline(ss, tok, ','); ep.linkedTradeId     = std::stoi(tok);
            std::getline(ss, tok, ','); ep.exitTakeProfit    = std::stod(tok);
            std::getline(ss, tok, ','); ep.exitStopLoss      = std::stod(tok);
            std::getline(ss, tok, ','); ep.stopLossActive    = (std::stoi(tok) != 0);
            out.push_back(ep);
        }
        return out;
    }

    int nextEntryId() const
    {
        int maxId = 0;
        for (const auto& ep : loadEntryPoints())
            if (ep.entryId > maxId) maxId = ep.entryId;
        return maxId + 1;
    }

    // ---- Wallet ----

    double loadWalletBalance() const
    {
        std::ifstream f(walletPath());
        if (!f) return 0.0;
        double bal = 0.0;
        f >> bal;
        return bal;
    }

    void saveWalletBalance(double balance)
    {
        std::ofstream f(walletPath(), std::ios::trunc);
        if (!f) throw std::runtime_error("Cannot open " + walletPath());
        f << std::fixed << std::setprecision(8) << balance << '\n';
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
            if (t.type == TradeType::Buy)
                deployed += t.value * t.quantity;
        return deployed;
    }

    // Execute a sell: create a CoveredSell child, credit wallet with proceeds.
    // Returns the new trade ID, or -1 if validation fails.
    int executeSell(int parentTradeId, double sellPrice, double sellQty)
    {
        auto trades = loadTrades();
        auto* parent = findTradeById(trades, parentTradeId);
        if (!parent || parent->type != TradeType::Buy) return -1;

        double remaining = parent->quantity - soldQuantityForParent(parentTradeId);
        if (sellQty > remaining + 1e-9) return -1;
        if (sellQty > remaining) sellQty = remaining;

        Trade sell;
        sell.tradeId       = nextTradeId();
        sell.symbol        = parent->symbol;
        sell.type          = TradeType::CoveredSell;
        sell.value         = sellPrice;
        sell.quantity      = sellQty;
        sell.parentTradeId = parentTradeId;
        addTrade(sell);

        double proceeds = sellPrice * sellQty;
        deposit(proceeds);

        return sell.tradeId;
    }

    // Execute a buy: create a Buy trade, debit wallet.
    // Returns the new trade ID.
    int executeBuy(const std::string& symbol, double price, double qty)
    {
        Trade buy;
        buy.tradeId  = nextTradeId();
        buy.symbol   = symbol;
        buy.type     = TradeType::Buy;
        buy.value    = price;
        buy.quantity = qty;
        addTrade(buy);

        double cost = price * qty;
        withdraw(cost);

        return buy.tradeId;
    }

    // ---- Housekeeping ----

    void exportHtmlReport(const std::string& path) const
    {
        std::ofstream f(path, std::ios::trunc);
        if (!f) throw std::runtime_error("Cannot open " + path);

        f << std::fixed << std::setprecision(2);

        f << R"(<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>Quant Trade Report</title>
<style>
body{font-family:monospace;background:#1a1a2e;color:#e0e0e0;padding:20px}
h1{color:#0ff}h2{color:#0af;border-bottom:1px solid #333;padding-bottom:4px}
table{border-collapse:collapse;margin:10px 0;width:100%}
th,td{border:1px solid #333;padding:6px 10px;text-align:right}
th{background:#16213e;color:#0ff}
tr:nth-child(even){background:#16213e}
.buy{color:#0f0}.sell{color:#f55}.pos{color:#0f0}.neg{color:#f55}
.wallet{background:#16213e;padding:12px;border-radius:6px;margin:10px 0;display:inline-block}
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
              << "<th>TP</th><th>SL</th><th>SL Active</th></tr>\n";
            for (const auto& t : trades)
            {
                f << "<tr><td>" << t.tradeId << "</td>"
                  << "<td>" << t.symbol << "</td>"
                  << "<td class='" << (t.type == TradeType::Buy ? "buy" : "sell") << "'>"
                  << (t.type == TradeType::Buy ? "BUY" : "COVERED_SELL") << "</td>"
                  << "<td>" << t.value << "</td>"
                  << "<td>" << t.quantity << "</td>"
                  << "<td>" << (t.parentTradeId >= 0 ? std::to_string(t.parentTradeId) : "-") << "</td>"
                  << "<td>" << t.takeProfit << "</td>"
                  << "<td>" << t.stopLoss << "</td>"
                  << "<td>" << (t.stopLossActive ? "ON" : "OFF") << "</td></tr>\n";
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
            f << std::setprecision(4);
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

        f << std::fixed << std::setprecision(2);

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
            f << "  TP=" << t.takeProfit
              << "  SL=" << t.stopLoss
              << (t.stopLossActive ? " [SL ON]" : " [SL OFF]")
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
        f << std::setprecision(4);
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

    void clearAll()
    {
        std::filesystem::remove(tradesPath());
        std::filesystem::remove(horizonsPath());
        std::filesystem::remove(profitsPath());
        std::filesystem::remove(paramsPath());
        std::filesystem::remove(walletPath());
        std::filesystem::remove(pendingExitsPath());
        std::filesystem::remove(entryPointsPath());
    }

private:
    std::string m_dir;

    std::string tradesPath()   const { return m_dir + "/trades.csv"; }
    std::string horizonsPath() const { return m_dir + "/horizons.csv"; }
    std::string profitsPath()  const { return m_dir + "/profits.csv"; }
    std::string paramsPath()   const { return m_dir + "/params.csv"; }
    std::string walletPath()   const { return m_dir + "/wallet.csv"; }
    std::string pendingExitsPath() const { return m_dir + "/pending_exits.csv"; }
    std::string entryPointsPath()  const { return m_dir + "/entry_points.csv"; }

    using HorizonRow = std::tuple<std::string, int, HorizonLevel>;

    std::vector<HorizonRow> loadAllHorizons() const
    {
        std::vector<HorizonRow> out;
        std::ifstream f(horizonsPath());
        if (!f) return out;

        std::string line;
        while (std::getline(f, line))
        {
            if (line.empty()) continue;
            std::istringstream ss(line);
            std::string tok, sym;
            int tid = 0;
            HorizonLevel lv;

            std::getline(ss, sym, ',');
            std::getline(ss, tok, ','); tid              = std::stoi(tok);
            std::getline(ss, tok, ','); lv.index         = std::stoi(tok);
            std::getline(ss, tok, ','); lv.takeProfit    = std::stod(tok);
            std::getline(ss, tok, ','); lv.stopLoss      = std::stod(tok);
            std::getline(ss, tok, ','); lv.stopLossActive = (std::stoi(tok) != 0);

            out.emplace_back(sym, tid, lv);
        }
        return out;
    }

    void saveAllHorizons(const std::vector<HorizonRow>& rows)
    {
        std::ofstream f(horizonsPath(), std::ios::trunc);
        if (!f) throw std::runtime_error("Cannot open " + horizonsPath());

        for (const auto& [sym, tid, lv] : rows)
        {
            f << sym            << ','
              << tid            << ','
              << lv.index       << ','
              << std::fixed << std::setprecision(8)
              << lv.takeProfit  << ','
              << lv.stopLoss    << ','
              << lv.stopLossActive
              << '\n';
        }
    }
};

