#pragma once

#include "Trade.h"
#include <vector>
#include <cmath>

// TP/SL horizon formula:
//
//   overhead = ((sellFees + buyFees) * feeHedgingCoefficient + feeSpread * deltaTime)
//            * symbolCount
//            / ((price / quantity) * portfolioPump + coefficientK)
//
//   effective = overhead + surplusRate
//   positionDelta = (price * quantity) / portfolioPump
//
//   TP[i] = entry * qty * (1 + effective * (i + 1))
//   SL[i] = entry * qty * (1 - effective * (i + 1))
//
// overhead scales fee costs by symbolCount and normalises against a
// denominator built from the per-unit price ratio and pump capital.
// coefficientK is an additive offset in the denominator.
// positionDelta is the portfolio weight of this trade.
// surplusRate is pure profit margin on top of break-even.
// horizonCount controls how many levels are generated.

struct HorizonParams
{
    double buyFees                    = 0.0;   // specified per calculation
    double sellFees                   = 0.0;   // specified per calculation
    double feeHedgingCoefficient      = 1.0;
    double portfolioPump              = 0.0;   // portfolio pump for time t
    int    symbolCount                = 1;     // number of symbols in portfolio
    double coefficientK               = 0.0;
    double feeSpread                  = 0.0;   // fee spread / slippage for this symbol
    double deltaTime                  = 1.0;   // time delta
    double surplusRate                = 0.0;   // profit margin above break-even (e.g. 0.02 = 2%)
    int    horizonCount               = 1;     // how many TP/SL levels to generate
    bool   generateStopLosses         = false; // stop losses deactivated by default
    bool   allowShortTrades           = false; // short trades disabled by default
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
        double feeComponent =
            (p.sellFees + p.buyFees) * p.feeHedgingCoefficient;
        double spreadComponent = p.feeSpread * p.deltaTime;
        double numerator = (feeComponent + spreadComponent)
                         * static_cast<double>(p.symbolCount);
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
        return computeOverhead(price, quantity, p) + p.surplusRate;
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

    static std::vector<HorizonLevel> generate(const Trade& trade,
                                              const HorizonParams& p)
    {
        std::vector<HorizonLevel> levels;
        levels.reserve(p.horizonCount);

        double base     = trade.value * trade.quantity;
        double eo       = effectiveOverhead(trade, p);

        for (int i = 0; i < p.horizonCount; ++i)
        {
            double factor = eo * static_cast<double>(i + 1);

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
