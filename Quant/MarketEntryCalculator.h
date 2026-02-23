#pragma once

#include "MultiHorizonEngine.h"
#include <vector>

// Inverts the TP/SL horizon formula to find entry price levels.
//
// Uses overhead only (no surplusRate) — surplus is a profit margin
// applied at exit/horizon time, not entry spacing.
//
//   overhead = computeOverhead(price, qty, params)
//
//   EntryPrice[i] = currentPrice * (1 - overhead * (i + 1))
//   BreakEven[i]  = entryPrice[i] * (1 + overhead)
//
// Funding allocation (portfolioPump = total funds):
//   riskCoefficient in [0, 1]
//     0 = low risk  -> most funds at level 0 (closest to price)
//     1 = high risk -> most funds at deepest level (furthest from price)
//   weight[i] = (N - i) * (1 - risk) + (i + 1) * risk
//   allocation[i] = weight[i] / sum(weights) * totalFunds

struct EntryLevel
{
    int    index           = 0;
    double entryPrice      = 0.0;   // suggested entry price per unit
    double breakEven       = 0.0;   // price needed to break even after costs
    double costCoverage    = 0.0;   // how many layers of overhead are covered
    double potentialNet    = 0.0;   // net profit if price returns to currentPrice
    double funding         = 0.0;   // allocated funds for this level
    double fundingFraction = 0.0;   // fraction of total funds (0-1)
    double fundingQty      = 0.0;   // how many units this funding buys at entryPrice
};

class MarketEntryCalculator
{
public:
    static std::vector<EntryLevel> generate(double currentPrice,
                                            double quantity,
                                            const HorizonParams& p,
                                            double riskCoefficient = 0.0)
    {
        double oh = MultiHorizonEngine::computeOverhead(currentPrice, quantity, p);

        int N = p.horizonCount;
        double risk = (riskCoefficient < 0.0) ? 0.0
                    : (riskCoefficient > 1.0) ? 1.0
                    : riskCoefficient;

        // compute weights
        std::vector<double> weights(N);
        double weightSum = 0.0;
        for (int i = 0; i < N; ++i)
        {
            double lowRiskW  = static_cast<double>(N - i);
            double highRiskW = static_cast<double>(i + 1);
            weights[i] = lowRiskW * (1.0 - risk) + highRiskW * risk;
            weightSum += weights[i];
        }

        std::vector<EntryLevel> levels;
        levels.reserve(N);

        for (int i = 0; i < N; ++i)
        {
            double factor = oh * static_cast<double>(i + 1);

            EntryLevel el;
            el.index        = i;
            el.costCoverage = static_cast<double>(i + 1);
            el.entryPrice   = currentPrice * (1.0 - factor);
            el.breakEven    = el.entryPrice * (1.0 + oh);

            el.fundingFraction = (weightSum != 0.0) ? weights[i] / weightSum : 0.0;
            el.funding         = p.portfolioPump * el.fundingFraction;
            el.fundingQty      = (el.entryPrice > 0.0) ? el.funding / el.entryPrice : 0.0;

            el.potentialNet = (currentPrice - el.entryPrice) * el.fundingQty;

            levels.push_back(el);
        }

        return levels;
    }
};
