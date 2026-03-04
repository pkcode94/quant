// libquant -- standalone Quant math library
//
// Pure-math trade engine: serial plans, exit strategies, chain planning,
// profit calculation, overhead computation.  No UI, no database, no HTTP.
//
// Usage:
//   #include "quantmath.h"
//
//   QuantMath::SerialParams sp;
//   sp.currentPrice = 2650; sp.quantity = 1; sp.levels = 4;
//   ...
//   auto plan = QuantMath::generateSerialPlan(sp);
//   auto cycle = QuantMath::computeCycle(plan, sp);

#ifndef LIBQUANT_QUANTMATH_H
#define LIBQUANT_QUANTMATH_H

#include <cmath>
#include <vector>
#include <algorithm>
#include <numeric>
#include <limits>
#include <string>

class QuantMath
{
public:
    // -- Clamping -----------------------------------------------------

    static double clamp01(double v)
    {
        return (v < 0.0) ? 0.0 : (v > 1.0) ? 1.0 : v;
    }

    static double clamp(double v, double lo, double hi)
    {
        return (v < lo) ? lo : (v > hi) ? hi : v;
    }

    static double floorEps(double v)
    {
        return (v < std::numeric_limits<double>::epsilon())
             ? std::numeric_limits<double>::epsilon() : v;
    }

    // -- Sigmoid core -------------------------------------------------

    static double sigmoid(double x)
    {
        return 1.0 / (1.0 + std::exp(-x));
    }

    struct SigmoidRange
    {
        double s0    = 0.0;
        double s1    = 0.0;
        double range = 1.0;
    };

    static SigmoidRange sigmoidRange(double steepness)
    {
        SigmoidRange sr;
        sr.s0 = sigmoid(-steepness * 0.5);
        sr.s1 = sigmoid( steepness * 0.5);
        sr.range = (sr.s1 - sr.s0 > 0.0) ? sr.s1 - sr.s0 : 1.0;
        return sr;
    }

    static double sigmoidNorm(double t, double steepness)
    {
        SigmoidRange sr = sigmoidRange(steepness);
        double sigVal = sigmoid(steepness * (t - 0.5));
        return (sigVal - sr.s0) / sr.range;
    }

    static std::vector<double> sigmoidNormN(int N, double steepness)
    {
        SigmoidRange sr = sigmoidRange(steepness);
        std::vector<double> norm(N);
        for (int i = 0; i < N; ++i)
        {
            double t = (N > 1) ? static_cast<double>(i) / static_cast<double>(N - 1) : 1.0;
            double sigVal = sigmoid(steepness * (t - 0.5));
            norm[i] = (sigVal - sr.s0) / sr.range;
        }
        return norm;
    }

    // -- Risk warp ----------------------------------------------------

    static double riskWarp(double norm, double risk)
    {
        double r = clamp01(risk);
        return (1.0 - r) * norm + r * (1.0 - norm);
    }

    static std::vector<double> riskWeights(const std::vector<double>& norms, double risk)
    {
        std::vector<double> w(norms.size());
        for (size_t i = 0; i < norms.size(); ++i)
        {
            w[i] = riskWarp(norms[i], risk);
            if (w[i] < 1e-12) w[i] = 1e-12;
        }
        return w;
    }

    // -- Allocation ---------------------------------------------------

    static std::vector<double> normWeights(const std::vector<double>& weights)
    {
        double sum = 0.0;
        for (double w : weights) sum += w;
        std::vector<double> fracs(weights.size());
        for (size_t i = 0; i < weights.size(); ++i)
            fracs[i] = (sum > 0.0) ? weights[i] / sum : 0.0;
        return fracs;
    }

    static std::vector<double> allocate(const std::vector<double>& weights, double total)
    {
        auto fracs = normWeights(weights);
        std::vector<double> alloc(fracs.size());
        for (size_t i = 0; i < fracs.size(); ++i)
            alloc[i] = fracs[i] * total;
        return alloc;
    }

    // -- Interpolation ------------------------------------------------

    static double lerp(double low, double high, double t)
    {
        return low + t * (high - low);
    }

    static double sigmoidLerp(double low, double high, int i, int N, double steepness)
    {
        double t = sigmoidNorm(
            (N > 1) ? static_cast<double>(i) / static_cast<double>(N - 1) : 1.0,
            steepness);
        return lerp(low, high, t);
    }

    // -- Hyperbolic compression ---------------------------------------

    static double hyperbolicCompress(double x)
    {
        return (x > 0.0) ? x / (x + 1.0) : 0.0;
    }

    // -- Fee overhead -------------------------------------------------

    static double overhead(double price, double quantity,
                           double feeSpread, double feeHedge, double deltaTime,
                           int symbolCount, double pump, double coeffK,
                           int futureTradeCount = 0)
    {
        double feeComponent = feeSpread * feeHedge * deltaTime;
        double tradeScale = 1.0 + static_cast<double>(std::max(0, futureTradeCount));
        double numerator = feeComponent * static_cast<double>(symbolCount) * tradeScale;
        double pricePerQty = (quantity > 0.0) ? price / quantity : 0.0;
        double denominator = pricePerQty * pump + coeffK;
        return (denominator != 0.0) ? numerator / denominator : 0.0;
    }

    static double effectiveOverhead(double rawOverhead,
                                    double surplusRate, double feeSpread,
                                    double feeHedge, double deltaTime)
    {
        return rawOverhead
             + surplusRate * feeHedge * deltaTime
             + feeSpread * feeHedge * deltaTime;
    }

    // -- Position metrics ---------------------------------------------

    static double positionDelta(double price, double quantity, double pump)
    {
        return (pump > 0.0) ? (price * quantity) / pump : 0.0;
    }

    static double breakEven(double entryPrice, double overhead)
    {
        return entryPrice * (1.0 + overhead);
    }

    static double fundedQty(double price, double funds)
    {
        return (price > 0.0 && funds > 0.0) ? funds / price : 0.0;
    }

    // -- Profit -------------------------------------------------------

    static double grossProfit(double entryPrice, double exitPrice,
                              double quantity, bool isShort = false)
    {
        return isShort ? (entryPrice - exitPrice) * quantity
                       : (exitPrice - entryPrice) * quantity;
    }

    static double netProfit(double gross, double buyFee, double sellFee)
    {
        return gross - buyFee - sellFee;
    }

    static double roi(double netProfit, double cost)
    {
        return (cost != 0.0) ? (netProfit / cost) * 100.0 : 0.0;
    }

    // -- Buffer multipliers -------------------------------------------

    static double sigmoidBuffer(double delta, double lower, double upper,
                                int count)
    {
        if (count <= 0 || delta <= 0.0)
            return 1.0;

        double t = hyperbolicCompress(delta);
        double alpha = (delta < 0.1) ? 0.1 : delta;

        SigmoidRange sr = sigmoidRange(alpha);
        double sigVal = sigmoid(alpha * (t - 0.5));
        double norm = (sigVal - sr.s0) / sr.range;

        double perCycle = lerp(lower, upper, norm);
        return 1.0 + static_cast<double>(count) * perCycle;
    }

    // -- Stop-loss capital clamp --------------------------------------

    static double clampSlFraction(double slFrac, double eo,
                                  const std::vector<double>& fundings,
                                  double availableCapital)
    {
        if (slFrac <= 0.0 || availableCapital <= 0.0)
            return slFrac;

        double totalExposure = 0.0;
        for (double f : fundings)
            totalExposure += eo * f;
        totalExposure *= slFrac;

        if (totalExposure <= 0.0 || totalExposure <= availableCapital)
            return slFrac;

        double clamped = slFrac * (availableCapital / totalExposure);
        return clamp01(clamped);
    }

    // -- Discount / gain ----------------------------------------------

    static double discount(double referencePrice, double entryPrice)
    {
        return (referencePrice > 0.0)
             ? ((referencePrice - entryPrice) / referencePrice) * 100.0
             : 0.0;
    }

    static double pctGain(double entryPrice, double exitPrice)
    {
        return (entryPrice > 0.0)
             ? ((exitPrice - entryPrice) / entryPrice) * 100.0
             : 0.0;
    }

    // -- Savings extraction -------------------------------------------

    static double savings(double profit, double savingsRate)
    {
        return profit * clamp01(savingsRate);
    }

    static double reinvest(double profit, double savingsRate)
    {
        return profit - savings(profit, savingsRate);
    }

    // -- Fee hedging coverage -----------------------------------------

    static double feeHedgingCoverage(double hedgePool, double totalFees)
    {
        return (totalFees > 0.0) ? hedgePool / totalFees : 0.0;
    }

    // -- Trade arithmetic ---------------------------------------------

    static double cost(double price, double qty)
    {
        return price * qty;
    }

    static double proceeds(double price, double qty, double sellFee)
    {
        return price * qty - sellFee;
    }

    static double feeFromRate(double notional, double rate)
    {
        return notional * rate;
    }

    static double avgEntry(double totalCost, double totalQty)
    {
        return (totalQty > 0.0) ? totalCost / totalQty : 0.0;
    }

    // -- Chain helpers ------------------------------------------------

    static int chainFutureTradeCount(int totalCycles, int currentCycle)
    {
        int nf = totalCycles - 1 - currentCycle;
        return (nf > 0) ? nf : 0;
    }

    // =================================================================
    //  HIGH-LEVEL API
    // =================================================================

    // -- Hyperparameters (input) --------------------------------------

    struct SerialParams
    {
        double currentPrice          = 0.0;
        double quantity              = 1.0;
        int    levels                = 4;
        double steepness             = 6.0;
        double risk                  = 0.5;
        bool   isShort               = false;
        double availableFunds        = 0.0;

        double rangeAbove            = 0.0;
        double rangeBelow            = 0.0;

        double feeSpread             = 0.0;
        double feeHedgingCoefficient = 1.0;
        double deltaTime             = 1.0;
        int    symbolCount           = 1;
        double coefficientK          = 0.0;
        double surplusRate           = 0.0;
        int    futureTradeCount      = 0;

        double maxRisk               = 0.0;
        double minRisk               = 0.0;

        bool   generateStopLosses    = false;
        double stopLossFraction      = 1.0;
        int    stopLossHedgeCount    = 0;

        int    downtrendCount        = 1;

        double savingsRate           = 0.0;
    };

    // -- Per-level output ---------------------------------------------

    struct SerialEntry
    {
        int    index        = 0;
        double entryPrice   = 0.0;
        double breakEven    = 0.0;
        double discountPct  = 0.0;
        double funding      = 0.0;
        double fundFrac     = 0.0;
        double fundQty      = 0.0;
        double tpUnit       = 0.0;
        double tpTotal      = 0.0;
        double tpGross      = 0.0;
        double slUnit       = 0.0;
        double slTotal      = 0.0;
        double slLoss       = 0.0;
        double slQty        = 0.0;
        double effectiveOH  = 0.0;
    };

    // -- Full plan output ---------------------------------------------

    struct SerialPlan
    {
        double overhead         = 0.0;
        double effectiveOH      = 0.0;
        double dtBuffer         = 1.0;
        double slBuffer         = 1.0;
        double combinedBuffer   = 1.0;
        double slFraction       = 1.0;
        double totalSlLoss      = 0.0;

        std::vector<SerialEntry> entries;

        double totalFunding     = 0.0;
        double totalTpGross     = 0.0;
    };

    // -- Cycle result -------------------------------------------------

    struct CycleResult
    {
        double totalCost        = 0.0;
        double totalRevenue     = 0.0;
        double totalFees        = 0.0;
        double grossProfit      = 0.0;
        double savingsAmount    = 0.0;
        double reinvestAmount   = 0.0;
        double nextCycleFunds   = 0.0;

        struct CycleTrade
        {
            int    index       = 0;
            double entryPrice  = 0.0;
            double qty         = 0.0;
            double funding     = 0.0;
            double buyFee      = 0.0;
            double tpPrice     = 0.0;
            double sellFee     = 0.0;
            double revenue     = 0.0;
            double net         = 0.0;
        };
        std::vector<CycleTrade> trades;
    };

    // -- generateSerialPlan -------------------------------------------

    static SerialPlan generateSerialPlan(const SerialParams& sp)
    {
        SerialPlan plan;
        int N = (sp.levels < 1) ? 1 : sp.levels;
        double steep = (sp.steepness < 0.1) ? 0.1 : sp.steepness;
        double risk  = clamp01(sp.risk);

        plan.overhead = overhead(sp.currentPrice, sp.quantity,
            sp.feeSpread, sp.feeHedgingCoefficient, sp.deltaTime,
            sp.symbolCount, sp.availableFunds, sp.coefficientK,
            sp.futureTradeCount);
        plan.effectiveOH = effectiveOverhead(plan.overhead,
            sp.surplusRate, sp.feeSpread,
            sp.feeHedgingCoefficient, sp.deltaTime);

        double delta = positionDelta(sp.currentPrice, sp.quantity, sp.availableFunds);
        double lower = sp.minRisk;
        double upper = (sp.maxRisk > 0.0) ? sp.maxRisk : plan.effectiveOH;
        if (upper < lower) upper = lower;

        plan.dtBuffer = sigmoidBuffer(delta, lower, upper, sp.downtrendCount);
        plan.slFraction = clamp01(sp.stopLossFraction);
        plan.slBuffer = sigmoidBuffer(delta, lower * plan.slFraction,
                                      upper * plan.slFraction, sp.stopLossHedgeCount);
        plan.combinedBuffer = plan.dtBuffer * plan.slBuffer;

        double priceLow, priceHigh;
        if (sp.rangeAbove > 0.0 || sp.rangeBelow > 0.0)
        {
            priceLow  = floorEps(sp.currentPrice - sp.rangeBelow);
            priceHigh = sp.currentPrice + sp.rangeAbove;
        }
        else
        {
            priceLow  = 0.0;
            priceHigh = sp.currentPrice;
        }

        auto norm    = sigmoidNormN(N, steep);
        auto weights = riskWeights(norm, risk);
        double wSum  = 0.0;
        for (double w : weights) wSum += w;

        std::vector<double> fundings(N);
        for (int i = 0; i < N; ++i)
            fundings[i] = (wSum > 0) ? sp.availableFunds * weights[i] / wSum : 0;

        if (sp.generateStopLosses)
            plan.slFraction = clampSlFraction(plan.slFraction, plan.effectiveOH,
                                              fundings, sp.availableFunds);

        plan.entries.resize(N);
        for (int i = 0; i < N; ++i)
        {
            SerialEntry& e = plan.entries[i];
            e.index      = i;
            e.entryPrice = floorEps(lerp(priceLow, priceHigh, norm[i]));
            e.breakEven  = breakEven(e.entryPrice, plan.overhead);
            e.discountPct = discount(sp.currentPrice, e.entryPrice);
            e.fundFrac   = (wSum > 0) ? weights[i] / wSum : 0;
            e.funding    = sp.availableFunds * e.fundFrac;
            e.fundQty    = fundedQty(e.entryPrice, e.funding);
            e.effectiveOH = plan.effectiveOH;

            e.tpUnit = levelTP(e.entryPrice, plan.overhead, plan.effectiveOH,
                               sp, steep, i, N, priceHigh);
            if (plan.combinedBuffer > 1.0)
                e.tpUnit *= plan.combinedBuffer;
            double entryCost = e.entryPrice * e.fundQty;
            e.tpTotal    = e.tpUnit * e.fundQty;
            e.tpGross    = e.tpTotal - entryCost;

            if (sp.generateStopLosses)
            {
                e.slUnit  = levelSL(e.entryPrice, plan.effectiveOH, sp.isShort);
                e.slQty   = e.fundQty * plan.slFraction;
                e.slTotal = e.slUnit * e.slQty;
                e.slLoss  = e.slTotal - e.entryPrice * e.slQty;
            }

            plan.totalFunding += e.funding;
            plan.totalTpGross += e.tpGross;
            if (sp.generateStopLosses)
                plan.totalSlLoss += std::abs(e.slLoss);
        }

        return plan;
    }

    // -- computeCycle -------------------------------------------------

    static CycleResult computeCycle(const SerialPlan& plan,
                                    const SerialParams& sp)
    {
        CycleResult cr;

        for (const auto& e : plan.entries)
        {
            if (e.funding <= 0) continue;

            double buyFee  = e.funding * sp.feeSpread;
            double revenue = e.tpUnit * e.fundQty;
            double sellFee = revenue * sp.feeSpread;
            double net     = revenue - e.funding - buyFee - sellFee;

            cr.totalCost    += e.funding;
            cr.totalRevenue += revenue;
            cr.totalFees    += buyFee + sellFee;

            CycleResult::CycleTrade ct;
            ct.index      = e.index;
            ct.entryPrice = e.entryPrice;
            ct.qty        = e.fundQty;
            ct.funding    = e.funding;
            ct.buyFee     = buyFee;
            ct.tpPrice    = e.tpUnit;
            ct.sellFee    = sellFee;
            ct.revenue    = revenue;
            ct.net        = net;
            cr.trades.push_back(ct);
        }

        cr.grossProfit    = cr.totalRevenue - cr.totalCost - cr.totalFees;
        cr.savingsAmount  = savings(cr.grossProfit, sp.savingsRate);
        cr.reinvestAmount = cr.grossProfit - cr.savingsAmount;
        cr.nextCycleFunds = sp.availableFunds + cr.reinvestAmount;

        return cr;
    }

    // -- Chain output -------------------------------------------------

    struct ChainCycle
    {
        int              cycle          = 0;
        double           capital        = 0.0;
        SerialPlan       plan;
        CycleResult      result;
    };

    struct ChainResult
    {
        std::vector<ChainCycle> cycles;
        double initialOverhead  = 0.0;
        double initialEffective = 0.0;
    };

    // -- generateChain ------------------------------------------------

    static ChainResult generateChain(const SerialParams& sp, int totalCycles)
    {
        ChainResult cr;
        if (totalCycles < 1) totalCycles = 1;

        auto plan0 = generateSerialPlan(sp);
        cr.initialOverhead  = plan0.overhead;
        cr.initialEffective = plan0.effectiveOH;

        double capital = sp.availableFunds;

        for (int ci = 0; ci < totalCycles; ++ci)
        {
            SerialParams csp  = sp;
            csp.availableFunds   = capital;
            csp.futureTradeCount = chainFutureTradeCount(totalCycles, ci);

            ChainCycle cc;
            cc.cycle   = ci;
            cc.capital = capital;
            cc.plan    = generateSerialPlan(csp);
            cc.result  = computeCycle(cc.plan, csp);

            capital = cc.result.nextCycleFunds;
            cr.cycles.push_back(std::move(cc));
        }

        return cr;
    }

    // -- Per-level TP/SL (public) -------------------------------------

    static double levelTP(double entryPrice, double oh, double eo,
                          double maxRisk, double minRisk, double risk,
                          bool isShort, double steepness,
                          int levelIndex, int totalLevels,
                          double referencePrice)
    {
        return levelTPImpl(entryPrice, oh, eo, maxRisk, minRisk, risk,
                           isShort, steepness, levelIndex, totalLevels,
                           referencePrice);
    }

    static double levelSL(double entryPrice, double eo, bool isShort)
    {
        double sl = isShort ? entryPrice * (1.0 + eo)
                            : entryPrice * (1.0 - eo);
        return (sl < 0.0) ? 0.0 : sl;
    }

    // -- Horizon factor -----------------------------------------------

    static double horizonFactor(double rawOH, double eo, double maxRisk,
                                double steepness, int levelIndex, int totalLevels)
    {
        if (maxRisk > 0.0)
        {
            double mrMinF = rawOH;
            double mrMaxF = (maxRisk > mrMinF) ? maxRisk : mrMinF;
            double steep = (steepness > 0.0) ? steepness : 0.01;
            double t = (totalLevels > 1)
                ? static_cast<double>(levelIndex) / static_cast<double>(totalLevels - 1)
                : 1.0;
            double norm = sigmoidNorm(t, steep);
            return lerp(mrMinF, mrMaxF, norm);
        }
        return eo * static_cast<double>(levelIndex + 1);
    }

    // -- Exit plan ----------------------------------------------------

    struct ExitPlanLevel
    {
        int    index          = 0;
        double tpPrice        = 0.0;
        double sellQty        = 0.0;
        double sellFraction   = 0.0;
        double sellValue      = 0.0;
        double grossProfit    = 0.0;
        double cumSold        = 0.0;
        double levelBuyFee    = 0.0;
        double netProfit      = 0.0;
        double cumNetProfit   = 0.0;
    };

    struct ExitPlan
    {
        std::vector<ExitPlanLevel> levels;
    };

    struct ExitParams
    {
        double entryPrice       = 0.0;
        double quantity         = 0.0;
        double buyFee           = 0.0;
        double rawOH            = 0.0;
        double eo               = 0.0;
        double maxRisk          = 0.0;
        int    horizonCount     = 1;
        double riskCoefficient  = 0.0;
        double exitFraction     = 1.0;
        double steepness        = 4.0;
    };

    static ExitPlan generateExitPlan(const ExitParams& ep)
    {
        ExitPlan plan;
        int N = (ep.horizonCount < 1) ? 1 : ep.horizonCount;
        double frac  = clamp01(ep.exitFraction);
        double risk  = clamp01(ep.riskCoefficient);
        double steep = (ep.steepness > 0.0) ? ep.steepness : 0.01;

        double sellableQty = ep.quantity * frac;

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

        plan.levels.reserve(N);
        double cumSold = 0.0;
        double cumNet  = 0.0;

        for (int i = 0; i < N; ++i)
        {
            double factor = horizonFactor(ep.rawOH, ep.eo, ep.maxRisk, steep, i, N);

            ExitPlanLevel el;
            el.index        = i;
            el.tpPrice      = ep.entryPrice * (1.0 + factor);
            el.sellFraction = cumSigma[i + 1] - cumSigma[i];
            el.sellQty      = sellableQty * el.sellFraction;
            el.sellValue    = el.tpPrice * el.sellQty;
            el.grossProfit  = QuantMath::grossProfit(ep.entryPrice, el.tpPrice, el.sellQty);
            el.levelBuyFee  = ep.buyFee * el.sellFraction;
            el.netProfit    = el.grossProfit - el.levelBuyFee;

            cumSold += el.sellQty;
            cumNet  += el.netProfit;
            el.cumSold      = cumSold;
            el.cumNetProfit  = cumNet;

            plan.levels.push_back(el);
        }

        return plan;
    }

    // -- Profit -------------------------------------------------------

    struct ProfitOut
    {
        double gross  = 0.0;
        double net    = 0.0;
        double roiPct = 0.0;
    };

    static ProfitOut computeProfit(double entryPrice, double exitPrice,
                                   double qty, double buyFee, double sellFee,
                                   bool isShort = false)
    {
        ProfitOut p;
        p.gross  = grossProfit(entryPrice, exitPrice, qty, isShort);
        p.net    = netProfit(p.gross, buyFee, sellFee);
        double c = entryPrice * qty + buyFee;
        p.roiPct = roi(p.net, c);
        return p;
    }

    // -- DCA ----------------------------------------------------------

    struct DcaResult
    {
        int    count     = 0;
        double totalCost = 0.0;
        double totalQty  = 0.0;
        double avgPrice  = 0.0;
        double minPrice  = 0.0;
        double maxPrice  = 0.0;
        double spread    = 0.0;
    };

    static DcaResult computeDca(const std::vector<std::pair<double, double>>& entries)
    {
        DcaResult r;
        r.count = static_cast<int>(entries.size());
        if (r.count == 0) return r;

        r.minPrice = entries[0].first;
        r.maxPrice = entries[0].first;

        for (const auto& [price, qty] : entries)
        {
            r.totalCost += price * qty;
            r.totalQty  += qty;
            if (price < r.minPrice) r.minPrice = price;
            if (price > r.maxPrice) r.maxPrice = price;
        }

        r.avgPrice = avgEntry(r.totalCost, r.totalQty);
        r.spread   = r.maxPrice - r.minPrice;
        return r;
    }

private:
    static double levelTPImpl(double entryPrice, double /*oh*/, double eo,
                              double maxRisk, double minRisk, double risk,
                              bool isShort, double steepness,
                              int levelIndex, int totalLevels,
                              double referencePrice)
    {
        if (maxRisk <= 0.0)
            return 0.0;

        double r = clamp01(risk);
        double tpRef = (referencePrice > 0.0) ? referencePrice : entryPrice;
        double steep = (steepness > 0.1) ? steepness * 0.5 : 0.1;
        double t = static_cast<double>(levelIndex + 1)
                 / static_cast<double>(totalLevels + 1);
        double rawNorm = sigmoidNorm(t, steep);
        double norm = riskWarp(rawNorm, r);

        if (!isShort)
        {
            double minTP = entryPrice * (1.0 + eo + minRisk);
            double maxTP = tpRef * (1.0 + maxRisk);
            if (maxTP <= minTP) return minTP;
            return lerp(minTP, maxTP, norm);
        }
        else
        {
            double maxTP = entryPrice * (1.0 - eo - minRisk);
            if (maxTP < 0.0) maxTP = 0.0;
            double floorTP = tpRef * (1.0 - maxRisk);
            if (floorTP < 0.0) floorTP = 0.0;
            if (floorTP >= maxTP) return maxTP;
            return lerp(maxTP, floorTP, norm);
        }
    }

    static double levelTP(double entryPrice, double oh, double eo,
                          const SerialParams& sp, double steepness,
                          int levelIndex, int totalLevels,
                          double referencePrice)
    {
        return levelTPImpl(entryPrice, oh, eo, sp.maxRisk, sp.minRisk,
                           sp.risk, sp.isShort, steepness,
                           levelIndex, totalLevels, referencePrice);
    }
};

#endif  // LIBQUANT_QUANTMATH_H
