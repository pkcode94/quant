#pragma once

#include "PriceSeries.h"
#include "SymbolRegistry.h"
#include "IdGenerator.h"
#include "Trade.h"
#include "ProfitCalculator.h"
#include "MultiHorizonEngine.h"
#include "MarketEntryCalculator.h"
#include "ExitStrategyCalculator.h"

#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <ctime>

// ============================================================
//  Simulator — forward simulation and historical backtesting
// ============================================================
//
// Forward mode:
//   User provides a PriceSeries (manually entered future prices),
//   starting capital, entry/exit parameters.  The engine steps
//   through time, buying at entry-point levels when prices hit,
//   selling at exit levels when prices reach TP, and tracking
//   capital, fees, and P&L at each step.
//
// Backtest mode:
//   Same logic but the PriceSeries comes from historical data.
//   The engine walks forward through the historical window,
//   executing the same entry/exit logic.
//
// Fee hedging verification:
//   After a run, compare totalFees vs feeHedgingAmount to see
//   whether the overhead formula covered all costs.
// ============================================================

struct SimTrade
{
    int         id          = 0;
    int         cycle       = 0;       // chain cycle this entry belongs to
    std::string symbol;
    double      entryPrice  = 0.0;
    double      quantity    = 0.0;
    double      buyFee      = 0.0;
    double      remaining   = 0.0;   // qty still held
    long long   entryTime   = 0;
};

struct SimSell
{
    int         buyId       = 0;
    int         cycle       = 0;
    std::string symbol;
    double      entryPrice  = 0.0;
    double      sellPrice   = 0.0;
    double      quantity    = 0.0;
    double      sellFee     = 0.0;
    double      grossProfit = 0.0;
    double      netProfit   = 0.0;
    long long   sellTime    = 0;
};

struct SimSnapshot
{
    long long   timestamp   = 0;
    double      capital     = 0.0;
    double      deployed    = 0.0;
    double      realized    = 0.0;
    double      totalFees   = 0.0;
    int         openTrades  = 0;
};

struct SimConfig
{
    double startingCapital  = 0.0;
    std::string symbol;            // single-symbol for now
    PriceSeries prices;

    // Entry parameters
    HorizonParams horizonParams;
    double entryRisk        = 0.5;
    double entrySteepness   = 6.0;
    double entryRangeBelow  = 0.0;   // price range below current
    double entryRangeAbove  = 0.0;

    // Exit parameters
    double exitRisk         = 0.5;
    double exitFraction     = 1.0;
    double exitSteepness    = 4.0;

    // Fee assumptions
    double buyFeeRate       = 0.0;   // e.g. 0.001 = 0.1% taker
    double sellFeeRate      = 0.0;

    // Downtrend hedging
    int    downtrendCount   = 1;     // n_d: number of downturn cycles to pre-fund

    // Chain mode: when a cycle completes, reinvest profit into a new cycle
    bool   chainCycles      = false;
    double savingsRate       = 0.0;  // fraction of realised profit diverted to savings (not reinvested)
};

struct SimResult
{
    double finalCapital     = 0.0;
    double totalRealized    = 0.0;
    double totalFees        = 0.0;
    double totalBuyFees     = 0.0;
    double totalSellFees    = 0.0;
    double feeHedgingAmount = 0.0;   // sum of gross profit from TP levels (fee budget)
    double feeHedgingCoverage = 0.0; // hedging / actual fees (>1 = fully covered)
    int    tradesOpened     = 0;
    int    tradesClosed     = 0;
    int    wins             = 0;
    int    losses           = 0;
    double bestTrade        = 0.0;
    double worstTrade       = 0.0;

    std::vector<SimTrade>    trades;
    std::vector<SimSell>     sells;
    std::vector<SimSnapshot> snapshots;

    // Chain mode stats
    int    cyclesCompleted   = 0;
    double totalSavings      = 0.0;  // cumulative profit diverted to savings
};

class Simulator
{
    static constexpr double EPS = 1e-12;

    // Per-trade state: the open position + its pre-computed exit plan
    struct OpenPosition
    {
        SimTrade            trade;
        int                 cycle = 0;
        std::vector<ExitLevel> exits;       // pre-computed once at entry
        std::vector<bool>      exitFilled;  // which levels have executed
    };

    // Helper: generate entry levels for a given price and capital
    struct CycleEntries
    {
        std::vector<EntryLevel> levels;
        std::vector<bool>       filled;
    };

    static CycleEntries generateCycleEntries(double price, double capital,
                                             const SimConfig& cfg)
    {
        CycleEntries ce;
        HorizonParams ep = cfg.horizonParams;
        ep.portfolioPump = capital;

        ce.levels = MarketEntryCalculator::generate(
            price, 1.0, ep,
            cfg.entryRisk, cfg.entrySteepness,
            cfg.entryRangeAbove, cfg.entryRangeBelow);

        double minEntry = price * 0.01;
        std::erase_if(ce.levels, [minEntry](const EntryLevel& el) {
            return el.entryPrice < minEntry;
        });

        ce.filled.assign(ce.levels.size(), false);
        return ce;
    }

public:
    // Run a forward simulation stepping through the price series.
    static SimResult run(const SimConfig& cfg)
    {
        SimResult result;
        if (!cfg.prices.hasSymbol(cfg.symbol)) return result;

        const auto& pts = cfg.prices.data().at(cfg.symbol);
        if (pts.empty()) return result;

        double capital   = cfg.startingCapital;
        double realized  = 0;
        double totalFees = 0;
        double hedgePool = 0;
        double savings   = 0;
        int    cycle     = 0;
        IdGenerator idGen;

        std::vector<OpenPosition> positions;

        // Generate initial entry levels (cycle 0)
        double firstPrice = pts.front().price;
        auto ce = generateCycleEntries(firstPrice, capital, cfg);

        for (size_t pi = 0; pi < pts.size(); ++pi)
        {
            long long now   = pts[pi].timestamp;
            double    price = pts[pi].price;

            // --- Check entries: buy when price drops to entry level ---
            for (size_t ei = 0; ei < ce.levels.size(); ++ei)
            {
                if (ce.filled[ei]) continue;
                if (price > ce.levels[ei].entryPrice) continue;

                double qty   = ce.levels[ei].fundingQty;
                if (qty < EPS) continue;
                double cost  = ce.levels[ei].entryPrice * qty;
                double fee   = cost * cfg.buyFeeRate;

                if (cost + fee > capital) continue;

                int tid = idGen.acquire();
                idGen.commit(tid);

                SimTrade st;
                st.id         = tid;
                st.cycle      = cycle;
                st.symbol     = cfg.symbol;
                st.entryPrice = ce.levels[ei].entryPrice;
                st.quantity   = qty;
                st.buyFee     = fee;
                st.remaining  = qty;
                st.entryTime  = now;

                capital   -= (cost + fee);
                totalFees += fee;

                // Pre-compute exit levels for this position ONCE
                Trade tmpTrade;
                tmpTrade.symbol   = st.symbol;
                tmpTrade.type     = TradeType::Buy;
                tmpTrade.value    = st.entryPrice;
                tmpTrade.quantity = st.quantity;
                tmpTrade.buyFee   = st.buyFee;

                // Use a HorizonParams with portfolioPump = trade cost
                // so overhead is meaningful relative to this position
                HorizonParams exitParams = cfg.horizonParams;
                exitParams.portfolioPump = cost;

                auto exitLevels = ExitStrategyCalculator::generate(
                    tmpTrade, exitParams,
                    cfg.exitRisk, cfg.exitFraction, cfg.exitSteepness);

                // Fee hedging: the sum of gross profits from exit levels
                // represents the overhead budget built into the TP targets
                for (const auto& el : exitLevels)
                    hedgePool += el.grossProfit;

                // Apply downtrend buffer and SL hedge buffer to exit TP prices
                {
                    double eo = MultiHorizonEngine::effectiveOverhead(
                        st.entryPrice, st.quantity, exitParams);
                    double dtBuf = (cfg.downtrendCount > 0)
                        ? MultiHorizonEngine::calculateDowntrendBuffer(
                              st.entryPrice, st.quantity, exitParams.portfolioPump,
                              eo, exitParams.minRisk, exitParams.maxRisk,
                              cfg.downtrendCount)
                        : 1.0;
                    double slFrac = MultiHorizonEngine::stopLossSellFraction(exitParams);
                    double slBuf = MultiHorizonEngine::calculateStopLossBuffer(
                        st.entryPrice, st.quantity, exitParams.portfolioPump,
                        eo, exitParams.minRisk, exitParams.maxRisk,
                        slFrac, exitParams.stopLossHedgeCount);
                    double combinedBuf = dtBuf * slBuf;
                    if (combinedBuf > 1.0)
                    {
                        for (auto& el : exitLevels)
                            el.tpPrice *= combinedBuf;
                    }
                }

                OpenPosition pos;
                pos.trade      = st;
                pos.cycle      = cycle;
                pos.exits      = std::move(exitLevels);
                pos.exitFilled.assign(pos.exits.size(), false);
                positions.push_back(std::move(pos));

                result.trades.push_back(st);
                result.tradesOpened++;
                ce.filled[ei] = true;
            }

            // --- Check exits: sell when price rises to TP level ---
            for (auto& pos : positions)
            {
                if (pos.trade.remaining < EPS) continue;

                for (size_t li = 0; li < pos.exits.size(); ++li)
                {
                    if (pos.exitFilled[li]) continue;

                    const auto& el = pos.exits[li];
                    if (el.sellQty < EPS) continue;
                    if (price < el.tpPrice) continue;
                    if (pos.trade.remaining < EPS) break;

                    double sellQty = std::min(el.sellQty, pos.trade.remaining);
                    if (sellQty < EPS) { pos.exitFilled[li] = true; continue; }

                    double sellFee = el.tpPrice * sellQty * cfg.sellFeeRate;
                    double gross   = (el.tpPrice - pos.trade.entryPrice) * sellQty;
                    double net     = gross - sellFee;

                    SimSell ss;
                    ss.buyId       = pos.trade.id;
                    ss.cycle       = pos.cycle;
                    ss.symbol      = pos.trade.symbol;
                    ss.entryPrice  = pos.trade.entryPrice;
                    ss.sellPrice   = el.tpPrice;
                    ss.quantity    = sellQty;
                    ss.sellFee     = sellFee;
                    ss.grossProfit = gross;
                    ss.netProfit   = net;
                    ss.sellTime    = now;

                    pos.trade.remaining -= sellQty;
                    capital             += (el.tpPrice * sellQty - sellFee);
                    realized            += net;
                    totalFees           += sellFee;

                    result.sells.push_back(ss);
                    result.tradesClosed++;
                    if (net >= 0) result.wins++;
                    else          result.losses++;
                    if (net > result.bestTrade)  result.bestTrade  = net;
                    if (net < result.worstTrade) result.worstTrade = net;

                    pos.exitFilled[li] = true;
                }
            }

            // --- Chain mode: when all positions from current cycle are closed,
            //     divert savings and regenerate entries at current price ---
            if (cfg.chainCycles)
            {
                bool allClosed = true;
                bool hadPositions = false;
                for (const auto& pos : positions)
                {
                    if (pos.cycle == cycle)
                    {
                        hadPositions = true;
                        if (pos.trade.remaining > EPS)
                        {
                            allClosed = false;
                            break;
                        }
                    }
                }

                // Also require at least one entry to have been filled this cycle
                bool anyFilled = false;
                for (size_t ei = 0; ei < ce.filled.size(); ++ei)
                    if (ce.filled[ei]) { anyFilled = true; break; }

                if (hadPositions && allClosed && anyFilled && capital > EPS)
                {
                    // Divert savings: fraction of cycle's realised profit
                    double cycleProfit = 0;
                    for (const auto& s : result.sells)
                        if (s.cycle == cycle) cycleProfit += s.netProfit;

                    if (cycleProfit > 0 && cfg.savingsRate > 0)
                    {
                        double saved = cycleProfit * cfg.savingsRate;
                        savings += saved;
                        capital -= saved;
                    }

                    result.cyclesCompleted++;
                    cycle++;

                    // Regenerate entries at current price with updated capital
                    ce = generateCycleEntries(price, capital, cfg);
                }
            }

            // --- Snapshot ---
            double deployed = 0;
            int openCount = 0;
            for (const auto& pos : positions)
            {
                if (pos.trade.remaining > EPS)
                {
                    deployed += pos.trade.entryPrice * pos.trade.remaining;
                    ++openCount;
                }
            }

            SimSnapshot snap;
            snap.timestamp  = now;
            snap.capital    = capital;
            snap.deployed   = deployed;
            snap.realized   = realized;
            snap.totalFees  = totalFees;
            snap.openTrades = openCount;
            result.snapshots.push_back(snap);
        }

        // Also update final remaining in result.trades from positions
        for (auto& pos : positions)
            for (auto& rt : result.trades)
                if (rt.id == pos.trade.id)
                    rt.remaining = pos.trade.remaining;

        result.finalCapital       = capital;
        result.totalRealized      = realized;
        result.totalFees          = totalFees;
        result.totalBuyFees       = 0;
        result.totalSellFees      = 0;
        for (const auto& t : result.trades) result.totalBuyFees  += t.buyFee;
        for (const auto& s : result.sells)  result.totalSellFees += s.sellFee;
        result.feeHedgingAmount   = hedgePool;
        result.feeHedgingCoverage = (totalFees > 0) ? hedgePool / totalFees : 0;
        result.totalSavings       = savings;
        if (cfg.chainCycles && cycle > 0)
            result.cyclesCompleted = cycle;

        return result;
    }
};
