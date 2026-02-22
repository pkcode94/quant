#pragma once

#include "Trade.h"

struct ProfitResult
{
    double grossProfit = 0.0;   // before fees
    double netProfit   = 0.0;   // after sell fees
    double roi         = 0.0;   // return on investment (%)
};

class ProfitCalculator
{
public:
    static ProfitResult calculate(const Trade& trade, double currentPrice,
                                  double buyFees, double sellFees)
    {
        ProfitResult r{};

        if (trade.type == TradeType::Buy)
            r.grossProfit = (currentPrice - trade.value) * trade.quantity;
        else
            r.grossProfit = (trade.value - currentPrice) * trade.quantity;

        r.netProfit = r.grossProfit - buyFees - sellFees;

        double cost = trade.value * trade.quantity + buyFees;
        r.roi = (cost != 0.0) ? (r.netProfit / cost) * 100.0 : 0.0;

        return r;
    }
};
