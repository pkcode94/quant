// quant_example -- demonstrates all libquant features
//
// Build:
//   cd example && make
//   ./quant_example

#include <iostream>
#include <iomanip>
#include <vector>
#include <utility>

#include "quantmath.h"

static void sep(const char* title)
{
    std::cout << "\n========================================\n"
              << "  " << title
              << "\n========================================\n\n";
}

// 1. Profit calculation
static void demo_profit()
{
    sep("Profit Calculation");

    auto p = QuantMath::computeProfit(100.0, 110.0, 10.0, 0.5, 0.5);
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  Entry=100  Exit=110  Qty=10  BuyFee=0.50  SellFee=0.50\n"
              << "  Gross  = " << p.gross << "\n"
              << "  Net    = " << p.net << "\n"
              << "  ROI    = " << p.roiPct << "%\n";

    // Short trade
    auto ps = QuantMath::computeProfit(110.0, 100.0, 5.0, 0.3, 0.3, true);
    std::cout << "\n  Short: Entry=110  Exit=100  Qty=5\n"
              << "  Gross  = " << ps.gross << "\n"
              << "  Net    = " << ps.net << "\n"
              << "  ROI    = " << ps.roiPct << "%\n";
}

// 2. DCA tracking
static void demo_dca()
{
    sep("DCA Tracking");

    std::vector<std::pair<double, double>> entries = {
        {2650.0, 0.5},
        {2580.0, 0.8},
        {2510.0, 1.2},
        {2700.0, 0.3}
    };

    auto dca = QuantMath::computeDca(entries);
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  Entries     = " << dca.count << "\n"
              << "  Total cost  = " << dca.totalCost << "\n"
              << "  Total qty   = " << dca.totalQty << "\n"
              << "  Avg entry   = " << dca.avgPrice << "\n"
              << "  Min entry   = " << dca.minPrice << "\n"
              << "  Max entry   = " << dca.maxPrice << "\n"
              << "  Spread      = " << dca.spread << "\n";
}

// 3. Overhead computation
static void demo_overhead()
{
    sep("Overhead Computation");

    double price     = 2650.0;
    double qty       = 1.0;
    double feeSpread = 0.001;
    double feeHedge  = 2.0;
    double dt        = 1.0;
    int    symbols   = 1;
    double pump      = 1000.0;
    double coeffK    = 1.0;
    double surplus   = 0.02;

    double oh = QuantMath::overhead(price, qty, feeSpread, feeHedge,
                                    dt, symbols, pump, coeffK);
    double eo = QuantMath::effectiveOverhead(oh, surplus, feeSpread, feeHedge, dt);
    double pd = QuantMath::positionDelta(price, qty, pump);
    double be = QuantMath::breakEven(price, oh);

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "  Price=" << price << "  Qty=" << qty
              << "  Funds=" << pump << "\n"
              << "  Overhead   = " << oh << "  (" << (oh * 100) << "%)\n"
              << "  Surplus    = " << surplus << "  (" << (surplus * 100) << "%)\n"
              << "  Effective  = " << eo << "  (" << (eo * 100) << "%)\n"
              << "  Pos delta  = " << pd << "  (" << (pd * 100) << "%)\n"
              << "  Break-even = " << be << "\n";
}

// 4. Serial plan
static void demo_serial_plan()
{
    sep("Serial Plan (4 levels)");

    QuantMath::SerialParams sp;
    sp.currentPrice          = 2650.0;
    sp.quantity              = 1.0;
    sp.levels                = 4;
    sp.steepness             = 6.0;
    sp.risk                  = 0.5;
    sp.availableFunds        = 1000.0;
    sp.feeSpread             = 0.001;
    sp.feeHedgingCoefficient = 2.0;
    sp.deltaTime             = 1.0;
    sp.symbolCount           = 1;
    sp.coefficientK          = 1.0;
    sp.surplusRate           = 0.02;
    sp.maxRisk               = 0.05;
    sp.minRisk               = 0.01;
    sp.generateStopLosses    = true;
    sp.stopLossFraction      = 1.0;
    sp.downtrendCount        = 1;

    auto plan = QuantMath::generateSerialPlan(sp);

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "  Overhead    = " << (plan.overhead * 100) << "%\n"
              << "  Effective   = " << (plan.effectiveOH * 100) << "%\n"
              << "  DT buffer   = " << plan.dtBuffer << "x\n"
              << "  SL buffer   = " << plan.slBuffer << "x\n"
              << "  Combined    = " << plan.combinedBuffer << "x\n"
              << "  SL fraction = " << plan.slFraction << "\n"
              << "  Funding     = " << plan.totalFunding << "\n"
              << "  TP gross    = " << plan.totalTpGross << "\n\n";

    std::cout << "  Lvl  Entry          Disc%    Qty            "
              << "Cost           TP/unit        SL/unit\n";

    for (const auto& e : plan.entries)
    {
        double c = QuantMath::cost(e.entryPrice, e.fundQty);
        std::cout << "  " << std::setw(3) << e.index
                  << "  " << std::setw(14) << e.entryPrice
                  << "  " << std::setw(7) << e.discountPct << "%"
                  << "  " << std::setw(14) << e.fundQty
                  << "  " << std::setw(14) << c
                  << "  " << std::setw(14) << e.tpUnit
                  << "  " << std::setw(14) << e.slUnit
                  << "\n";
    }
}

// 5. Cycle computation
static void demo_cycle()
{
    sep("Cycle Computation");

    QuantMath::SerialParams sp;
    sp.currentPrice          = 2650.0;
    sp.quantity              = 1.0;
    sp.levels                = 4;
    sp.steepness             = 6.0;
    sp.risk                  = 0.5;
    sp.availableFunds        = 1000.0;
    sp.feeSpread             = 0.001;
    sp.feeHedgingCoefficient = 2.0;
    sp.deltaTime             = 1.0;
    sp.symbolCount           = 1;
    sp.coefficientK          = 1.0;
    sp.surplusRate           = 0.02;
    sp.maxRisk               = 0.05;
    sp.minRisk               = 0.01;
    sp.savingsRate           = 0.10;

    auto plan = QuantMath::generateSerialPlan(sp);
    auto cr   = QuantMath::computeCycle(plan, sp);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  Total cost     = " << cr.totalCost << "\n"
              << "  Total revenue  = " << cr.totalRevenue << "\n"
              << "  Total fees     = " << cr.totalFees << "\n"
              << "  Gross profit   = " << cr.grossProfit << "\n"
              << "  Savings (10%)  = " << cr.savingsAmount << "\n"
              << "  Reinvest       = " << cr.reinvestAmount << "\n"
              << "  Next cycle cap = " << cr.nextCycleFunds << "\n";
}

// 6. Chain planning
static void demo_chain()
{
    sep("Chain (3 cycles)");

    QuantMath::SerialParams sp;
    sp.currentPrice          = 2650.0;
    sp.quantity              = 1.0;
    sp.levels                = 4;
    sp.steepness             = 6.0;
    sp.risk                  = 0.5;
    sp.availableFunds        = 1000.0;
    sp.feeSpread             = 0.001;
    sp.feeHedgingCoefficient = 2.0;
    sp.deltaTime             = 1.0;
    sp.symbolCount           = 1;
    sp.coefficientK          = 1.0;
    sp.surplusRate           = 0.02;
    sp.maxRisk               = 0.05;
    sp.minRisk               = 0.01;
    sp.savingsRate           = 0.10;

    auto chain = QuantMath::generateChain(sp, 3);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  Initial OH  = " << (chain.initialOverhead * 100) << "%\n"
              << "  Initial Eff = " << (chain.initialEffective * 100) << "%\n\n";

    for (const auto& cc : chain.cycles)
    {
        std::cout << "  Cycle " << cc.cycle
                  << "  Capital=" << cc.capital
                  << "  Profit=" << cc.result.grossProfit
                  << "  Savings=" << cc.result.savingsAmount
                  << "  Next=" << cc.result.nextCycleFunds
                  << "  (" << cc.plan.entries.size() << " levels)\n";
    }
}

// 7. Exit plan
static void demo_exit_plan()
{
    sep("Exit Plan (3 levels)");

    double price = 2650.0;
    double qty   = 1.0;
    double funds = 1000.0;

    double oh = QuantMath::overhead(price, qty, 0.001, 2.0, 1.0, 1, funds, 1.0);
    double eo = QuantMath::effectiveOverhead(oh, 0.02, 0.001, 2.0, 1.0);

    QuantMath::ExitParams ep;
    ep.entryPrice      = price;
    ep.quantity         = qty;
    ep.buyFee           = 0.50;
    ep.rawOH            = oh;
    ep.eo               = eo;
    ep.maxRisk          = 0.05;
    ep.horizonCount     = 3;
    ep.riskCoefficient  = 0.5;
    ep.exitFraction     = 0.8;
    ep.steepness        = 4.0;

    auto plan = QuantMath::generateExitPlan(ep);

    std::cout << std::fixed << std::setprecision(4);
    for (const auto& el : plan.levels)
    {
        std::cout << "  [" << el.index << "]"
                  << "  TP=" << el.tpPrice
                  << "  qty=" << el.sellQty
                  << " (" << (el.sellFraction * 100) << "%)"
                  << "  value=" << el.sellValue
                  << "  net=" << el.netProfit
                  << "  cum=" << el.cumNetProfit
                  << "\n";
    }
}

// 8. Utility helpers
static void demo_helpers()
{
    sep("Utility Helpers");

    std::cout << std::fixed << std::setprecision(4);
    std::cout << "  discount(2650, 2580)  = " << QuantMath::discount(2650, 2580) << "%\n"
              << "  pctGain(100, 110)     = " << QuantMath::pctGain(100, 110) << "%\n"
              << "  breakEven(100, 0.03)  = " << QuantMath::breakEven(100, 0.03) << "\n"
              << "  fundedQty(2650, 500)  = " << QuantMath::fundedQty(2650, 500) << "\n"
              << "  cost(2650, 0.5)       = " << QuantMath::cost(2650, 0.5) << "\n"
              << "  avgEntry(5000, 2)     = " << QuantMath::avgEntry(5000, 2) << "\n"
              << "  sigmoid(0)            = " << QuantMath::sigmoid(0) << "\n"
              << "  sigmoidNorm(0.5, 6)   = " << QuantMath::sigmoidNorm(0.5, 6) << "\n"
              << "  levelSL(100, 0.03, 0) = " << QuantMath::levelSL(100, 0.03, false) << "\n"
              << "  levelTP(100,...)      = " << QuantMath::levelTP(100, 0.01, 0.03,
                                                    0.05, 0.01, 0.5, false, 6.0, 0, 4, 100) << "\n";
}

int main()
{
    std::cout << "libquant example -- all features\n";

    demo_profit();
    demo_dca();
    demo_overhead();
    demo_serial_plan();
    demo_cycle();
    demo_chain();
    demo_exit_plan();
    demo_helpers();

    std::cout << "\nDone.\n";
    return 0;
}
