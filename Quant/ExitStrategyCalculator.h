#pragma once

#include "MultiHorizonEngine.h"
#include "ProfitCalculator.h"
#include <vector>
#include <cmath>

// Exit strategy: distributes a sigmoidal fraction of one trade's holdings
// across TP levels as pending sell orders.
//
//   effective = overhead + surplusRate
//   TP_price[i] = entry * (1 + effective * (i + 1))
//
// exitFraction in [0, 1]: total fraction of holdings to sell (e.g. 0.5 = 50%)
// steepness: controls sigmoid curve shape (0=uniform, 4=smooth S, 10+=step)
// riskCoefficient: shifts sigmoid center
//   0 = conservative -> sell most at early TP levels (lock in gains)
//   1 = aggressive   -> hold most for deeper TP levels (maximize upside)
//
// Cumulative sold follows a logistic sigmoid:
//   cumFrac(i) = sigmoid(steepness * (i - center))  normalized to [0,1]
//   sellQty[i] = sellableQty * (cumFrac[i+1] - cumFrac[i])

struct ExitLevel
{
    int    index          = 0;
    double tpPrice        = 0.0;   // take-profit price per unit at this level
    double sellQty        = 0.0;   // quantity to sell at this level
    double sellFraction   = 0.0;   // fraction of sellable position (0-1)
    double sellValue      = 0.0;   // gross proceeds = tpPrice * sellQty
    double grossProfit    = 0.0;   // (tpPrice - entry) * sellQty
    double netProfit      = 0.0;   // grossProfit - proportional fees
    double cumSold        = 0.0;   // cumulative quantity sold through this level
    double cumNetProfit   = 0.0;   // cumulative net profit locked in
};

class ExitStrategyCalculator
{
public:
    static std::vector<ExitLevel> generate(const Trade& trade,
                                           const HorizonParams& p,
                                           double riskCoefficient = 0.0,
                                           double exitFraction = 1.0,
                                           double steepness = 4.0)
    {
        double eo = MultiHorizonEngine::effectiveOverhead(trade, p);

        int N = p.horizonCount;
        if (N < 1) N = 1;

        double frac  = clamp01(exitFraction);
        double risk  = clamp01(riskCoefficient);
        double steep = (steepness > 0.0) ? steepness : 0.01;

        double sellableQty = trade.quantity * frac;

        // Sigmoid cumulative distribution
        // center = where the steepest part is
        // risk=0 -> center near start -> sell heavy early
        // risk=1 -> center near end   -> sell heavy late
        double center = risk * static_cast<double>(N - 1);

        std::vector<double> cumSigma(N + 1);
        for (int i = 0; i <= N; ++i)
        {
            double x = static_cast<double>(i) - 0.5;
            cumSigma[i] = sigmoid(steep * (x - center));
        }
        double lo = cumSigma[0], hi = cumSigma[N];
        for (int i = 0; i <= N; ++i)
            cumSigma[i] = (hi > lo) ? (cumSigma[i] - lo) / (hi - lo)
                                    : static_cast<double>(i) / static_cast<double>(N);

        double totalFees = p.buyFees + p.sellFees;

        std::vector<ExitLevel> levels;
        levels.reserve(N);

        double cumSold = 0.0;
        double cumNet  = 0.0;

        for (int i = 0; i < N; ++i)
        {
            double factor = eo * static_cast<double>(i + 1);

            ExitLevel el;
            el.index        = i;
            el.tpPrice      = trade.value * (1.0 + factor);
            el.sellFraction = cumSigma[i + 1] - cumSigma[i];
            el.sellQty      = sellableQty * el.sellFraction;
            el.sellValue    = el.tpPrice * el.sellQty;
            el.grossProfit  = (el.tpPrice - trade.value) * el.sellQty;
            el.netProfit    = el.grossProfit - totalFees * el.sellFraction;

            cumSold += el.sellQty;
            cumNet  += el.netProfit;
            el.cumSold      = cumSold;
            el.cumNetProfit  = cumNet;

            levels.push_back(el);
        }

        return levels;
    }

private:
    static double sigmoid(double x)
    {
        return 1.0 / (1.0 + std::exp(-x));
    }

    static double clamp01(double v)
    {
        return (v < 0.0) ? 0.0 : (v > 1.0) ? 1.0 : v;
    }
};
