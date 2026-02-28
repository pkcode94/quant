#pragma once

#include "Trade.h"
#include <vector>
#include <cmath>
#include <algorithm>

// TP/SL horizon formula:
//
//   overhead = (feeSpread * feeHedgingCoefficient * deltaTime)
//            * symbolCount
//            / ((price / quantity) * portfolioPump + coefficientK)
//
//   effective = overhead + surplusRate * feeHedgingCoefficient * deltaTime
//                        + feeSpread * feeHedgingCoefficient * deltaTime
//   positionDelta = (price * quantity) / portfolioPump
//
//   TP[i] = entry * qty * (1 + effective * (i + 1))
//   SL[i] = entry * qty * (1 - effective * (i + 1))
//
// levelTP is governed by maxRisk: when maxRisk = 0 the TP is 0 (no
// target).  As maxRisk increases the TP scales from break-even toward
// the ceiling, sigmoid-distributed across levels with half-steepness
// and (i+1)/(N+1) mapping so every level visibly responds.
// riskCoefficient warps the TP sigmoid to mirror funding allocation:
//   risk=0 ? forward sigmoid (higher prices get higher TP, conservative)
//   risk=0.5 ? uniform midpoint TP across all levels
//   risk=1 ? inverse sigmoid (lower prices get higher TP, aggressive)
//
// overhead scales fee spread by symbolCount and normalises against a
// denominator built from the per-unit price ratio and pump capital.
// feeHedgingCoefficient is a safety multiplier on the fee spread.
// coefficientK is an additive offset in the denominator.
// positionDelta is the portfolio weight of this trade.
// surplusRate is pure profit margin on top of break-even.
// horizonCount controls how many levels are generated.
//
// Absolute fees (buyFee / sellFee) are tracked per-trade on Trade
// and per-level on ExitLevel — not in HorizonParams.

struct HorizonParams
{
    double feeHedgingCoefficient      = 1.0;
    double portfolioPump              = 0.0;   // portfolio pump for time t
    int    symbolCount                = 1;     // number of symbols in portfolio
    double coefficientK               = 0.0;
    double feeSpread                  = 0.0;   // fee spread / slippage rate
    double deltaTime                  = 1.0;   // time delta
    double surplusRate                = 0.0;   // profit margin above break-even (e.g. 0.02 = 2%)
    int    horizonCount               = 1;     // how many TP/SL levels to generate
    bool   generateStopLosses         = false; // stop losses deactivated by default
    bool   allowShortTrades           = false; // short trades disabled by default
    double maxRisk                    = 0.0;   // max TP fraction above entry (0 = disabled)
    double minRisk                    = 0.0;   // min TP fraction above break-even (floor)
    int    futureTradeCount           = 0;     // future trades whose fees this TP must cover (0 = self only)
    double stopLossFraction           = 1.0;   // fraction of position to sell at SL (0-1, 1 = full exit)
    int    stopLossHedgeCount         = 0;     // future SL hits to pre-fund via TP inflation (0 = disabled)
};

struct HorizonLevel
{
    int    index      = 0;
    double takeProfit = 0.0;
    double stopLoss   = 0.0;
    bool   stopLossActive = false;
};

class MultiHorizonEngine
{
public:
    static double computeOverhead(double price, double quantity, const HorizonParams& p)
    {
        double feeComponent = p.feeSpread * p.feeHedgingCoefficient * p.deltaTime;
        // Scale by (1 + futureTradeCount) so this trade's TP covers fees
        // for itself plus N future trades in the chain.
        double tradeScale = 1.0 + static_cast<double>(std::max(0, p.futureTradeCount));
        double numerator = feeComponent * static_cast<double>(p.symbolCount) * tradeScale;
        double pricePerQty = (quantity > 0.0) ? price / quantity : 0.0;
        double denominator = pricePerQty * p.portfolioPump + p.coefficientK;
        return (denominator != 0.0) ? numerator / denominator : 0.0;
    }

    static double computeOverhead(const Trade& trade, const HorizonParams& p)
    {
        return computeOverhead(trade.value, trade.quantity, p);
    }

    static double effectiveOverhead(double price, double quantity, const HorizonParams& p)
    {
        return computeOverhead(price, quantity, p)
             + p.surplusRate * p.feeHedgingCoefficient * p.deltaTime
             + p.feeSpread * p.feeHedgingCoefficient * p.deltaTime;
    }

    static double effectiveOverhead(const Trade& trade, const HorizonParams& p)
    {
        return effectiveOverhead(trade.value, trade.quantity, p);
    }

    static double positionDelta(double price, double quantity, double portfolioPump)
    {
        return (portfolioPump > 0.0) ? (price * quantity) / portfolioPump : 0.0;
    }

    // Additional quantity the pump buys at this price.
    static double fundedQuantity(double price, const HorizonParams& p)
    {
        return (price > 0.0 && p.portfolioPump > 0.0) ? p.portfolioPump / price : 0.0;
    }

    static double totalQuantity(const Trade& trade, const HorizonParams& p)
    {
        return trade.quantity + fundedQuantity(trade.value, p);
    }

    static double sigmoid(double x)
    {
        return 1.0 / (1.0 + std::exp(-x));
    }

    // Per-level TP controlled by maxRisk.
    // maxRisk = 0  ->  TP = 0  (no take-profit target).
    // maxRisk > 0  ->  TP ceiling = referencePrice * (1 + maxRisk) for LONG.
    //
    // The TP floor (break-even) is always per-entry: entryPrice * (1 + eo).
    // The TP ceiling uses referencePrice (the highest entry in the set,
    // typically currentPrice or priceHigh) so the ceiling is fixed across
    // all levels.  This prevents a bell-curve when entry prices span a
    // wide range: the ceiling is constant, only the sigmoid norm varies.
    //
    // riskCoefficient warps the TP distribution (mirrors funding):
    //   risk=0   -> forward sigmoid (higher price levels get higher TP)
    //   risk=0.5 -> uniform (all levels get midpoint TP)
    //   risk=1   -> inverse sigmoid (lower price levels get higher TP)
    //
    // referencePrice: the highest entry price in the set (or currentPrice).
    //   When 0, falls back to entryPrice (legacy single-entry behaviour).
    static double levelTP(double entryPrice, double overhead, double eo,
                          const HorizonParams& p, double steepness,
                          int levelIndex, int totalLevels, bool isShort,
                          double riskCoefficient = 0.5,
                          double referencePrice = 0.0)
    {
        if (p.maxRisk <= 0.0)
            return 0.0;

        double risk = (riskCoefficient < 0.0) ? 0.0
                    : (riskCoefficient > 1.0) ? 1.0
                    : riskCoefficient;

        double tpRef = (referencePrice > 0.0) ? referencePrice : entryPrice;

        // Halve steepness so the TP sigmoid doesn't compress to the
        // same extremes as the already-sigmoidal entry prices.
        double steep = (steepness > 0.1) ? steepness * 0.5 : 0.1;
        double s0 = sigmoid(-steep * 0.5);
        double s1 = sigmoid( steep * 0.5);
        double sR = (s1 - s0 > 0.0) ? s1 - s0 : 1.0;
        // (i+1)/(N+1) keeps t inside (0,1) so every level moves.
        double t = static_cast<double>(levelIndex + 1)
                 / static_cast<double>(totalLevels + 1);
        double rawNorm = (sigmoid(steep * (t - 0.5)) - s0) / sR;

        // risk warps the curve: risk=1 inverts so lower prices get higher TP
        double norm = (1.0 - risk) * rawNorm + risk * (1.0 - rawNorm);

        if (!isShort)
        {
            double minTP = entryPrice * (1.0 + eo + p.minRisk);
            double maxTP = tpRef * (1.0 + p.maxRisk);
            if (maxTP <= minTP) return minTP;
            return minTP + norm * (maxTP - minTP);
        }
        else
        {
            double maxTP = entryPrice * (1.0 - eo - p.minRisk);
            if (maxTP < 0.0) maxTP = 0.0;
            double floorTP = tpRef * (1.0 - p.maxRisk);
            if (floorTP < 0.0) floorTP = 0.0;
            if (floorTP >= maxTP) return maxTP;
            return maxTP - norm * (maxTP - floorTP);
        }
    }

    static double levelSL(double entryPrice, double eo, bool isShort)
    {
        double sl = isShort ? entryPrice * (1.0 + eo) : entryPrice * (1.0 - eo);
        return (sl < 0.0) ? 0.0 : sl;
    }

    // Fraction of position to sell at SL (clamped to [0,1]).
    static double stopLossSellFraction(const HorizonParams& p)
    {
        return (p.stopLossFraction < 0.0) ? 0.0
             : (p.stopLossFraction > 1.0) ? 1.0
             : p.stopLossFraction;
    }

    // Stop-loss hedging buffer: TP multiplier that pre-funds potential
    // future SL hits, mirroring the downtrend buffer (§5.5) structure.
    //
    // Each pre-funded SL hit costs approximately EO of the position.
    // The buffer scales TP upward so that the extra profit covers
    // n_sl future SL events, shaped by the same axis-dependent
    // sigmoid as the downtrend buffer but using stopLossFraction
    // to scale the per-hit cost (partial SL = partial cost).
    //
    //   buffer = 1 + n_sl * slFraction * perCycle
    //
    //   n_sl = 0  ->  1 (disabled)
    //   slFraction = 0.25  ->  each SL hit costs 25% of full EO
    static double calculateStopLossBuffer(double price, double quantity,
                                          double portfolioPump,
                                          double effectiveOverhead,
                                          double minRisk, double maxRisk,
                                          double slFraction,
                                          int slHedgeCount = 0)
    {
        if (slHedgeCount <= 0)
            return 1.0;
        double delta = positionDelta(price, quantity, portfolioPump);
        if (delta <= 0.0)
            return 1.0;

        double lower = minRisk;
        double upper = (maxRisk > 0.0) ? maxRisk : effectiveOverhead;
        if (upper < lower) upper = lower;

        double t = delta / (delta + 1.0);
        double alpha = (delta < 0.1) ? 0.1 : delta;

        double sig0 = sigmoid(-alpha * 0.5);
        double sig1 = sigmoid( alpha * 0.5);
        double sigRange = sig1 - sig0;
        if (sigRange < 1e-12) sigRange = 1.0;
        double sigVal = sigmoid(alpha * (t - 0.5));
        double norm = (sigVal - sig0) / sigRange;

        double frac = (slFraction < 0.0) ? 0.0
                    : (slFraction > 1.0) ? 1.0
                    : slFraction;
        double perCycle = lower + norm * (upper - lower);
        return 1.0 + static_cast<double>(slHedgeCount) * frac * perCycle;
    }








    // Downtrend buffer: position-derived TP multiplier with
    // axis-dependent sigmoid curvature (§5.5).
    //
    // Position delta ? mapped through the normalised sigmoid (§3.1)
    // with ? itself as steepness ? curvature depends on P × q space.
    //
    // The sigmoid interpolates between R_min (lower asymptote) and
    // max(R_max, EO) (upper asymptote).  R_min guarantees a minimum
    // buffer even for tiny positions; R_max caps per-cycle cost.
    // When R_max = 0 the upper bound falls back to EO (time-sensitive
    // via ?t baked into the fee component).
    //
    //   buffer = 1 + n_d · (R_min + ??_?(t) · (upper ? R_min))
    //
    //   n_d = 0   ? 1 (disabled)
    //   ? = 0     ? 1 (nothing deployed)
    //   ? ? ?     ? 1 + n_d · upper  (saturated)
    static double calculateDowntrendBuffer(double price, double quantity,
                                           double portfolioPump,
                                           double effectiveOverhead,
                                           double minRisk, double maxRisk,
                                           int downtrendCount = 1)
    {
        if (downtrendCount <= 0)
            return 1.0;
        double delta = positionDelta(price, quantity, portfolioPump);
        if (delta <= 0.0)
            return 1.0;

        // Asymptotes: R_min is the floor, upper is R_max or EO fallback.
        double lower = minRisk;
        double upper = (maxRisk > 0.0) ? maxRisk : effectiveOverhead;
        if (upper < lower) upper = lower;

        // Map ? to [0,1) via hyperbolic compression.
        double t = delta / (delta + 1.0);

        // Axis-dependent steepness: ? controls curvature shape.
        double alpha = (delta < 0.1) ? 0.1 : delta;

        // Normalised sigmoid §3.1: ??_?(t)
        double sig0 = sigmoid(-alpha * 0.5);
        double sig1 = sigmoid( alpha * 0.5);
        double sigRange = sig1 - sig0;
        if (sigRange < 1e-12) sigRange = 1.0;
        double sigVal = sigmoid(alpha * (t - 0.5));
        double norm = (sigVal - sig0) / sigRange;

        double perCycle = lower + norm * (upper - lower);
        return 1.0 + static_cast<double>(downtrendCount) * perCycle;
    }






    static std::vector<HorizonLevel> generate(const Trade& trade,
                                              const HorizonParams& p)
    {
        std::vector<HorizonLevel> levels;
        levels.reserve(p.horizonCount);

        double base     = trade.value * trade.quantity;
        double eo       = effectiveOverhead(trade, p);

        bool useMaxRisk = (p.maxRisk > 0.0);
        double mrMinF = 0, mrMaxF = 0, mrS0 = 0, mrSR = 1;
        if (useMaxRisk)
        {
            mrMinF = computeOverhead(trade, p);
            mrMaxF = p.maxRisk;
            if (mrMaxF < mrMinF) mrMaxF = mrMinF;
            double steep = 4.0;
            mrS0 = sigmoid(-steep * 0.5);
            double mrS1 = sigmoid(steep * 0.5);
            mrSR = (mrS1 - mrS0 > 0) ? mrS1 - mrS0 : 1.0;
        }

        for (int i = 0; i < p.horizonCount; ++i)
        {
            double factor;
            if (useMaxRisk)
            {
                double t = (p.horizonCount > 1)
                         ? static_cast<double>(i) / static_cast<double>(p.horizonCount - 1)
                         : 1.0;
                double norm = (sigmoid(4.0 * (t - 0.5)) - mrS0) / mrSR;
                factor = mrMinF + norm * (mrMaxF - mrMinF);
            }
            else
            {
                factor = eo * static_cast<double>(i + 1);
            }

            HorizonLevel hl;
            hl.index     = i;
            hl.takeProfit = base * (1.0 + factor);

            if (p.generateStopLosses && trade.type == TradeType::Buy)
            {
                hl.stopLoss       = base * (1.0 - factor);
                hl.stopLossActive = false;
            }

            levels.push_back(hl);
        }

        return levels;
    }

    // Convenience: apply the generated horizons back onto a Trade
    static void applyFirstHorizon(Trade& trade,
                                  const std::vector<HorizonLevel>& levels,
                                  bool activateStopLoss = false)
    {
        if (levels.empty() || trade.type != TradeType::Buy)
            return;

        trade.takeProfit     = levels.front().takeProfit;
        trade.stopLoss       = levels.front().stopLoss;
        trade.stopLossActive = activateStopLoss;
    }
};
