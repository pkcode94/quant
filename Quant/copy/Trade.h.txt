#pragma once

#include <string>
#include <stdexcept>
#include <algorithm>
#include <cctype>

enum class TradeType
{
    Buy,
    CoveredSell   // asset-backed sell only, no short trades
};

inline std::string normalizeSymbol(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return s;
}

struct Trade
{
    std::string symbol;
    int         tradeId       = 0;
    TradeType   type          = TradeType::Buy;
    double      value         = 0.0;   // entry price per unit
    double      quantity      = 0.0;
    int         parentTradeId = -1;    // -1 = no parent (Buy); >= 0 = parent Buy trade id (CoveredSell)

    // Take-profit / stop-loss (meaningful for Buy trades only)
    double takeProfit      = 0.0;
    double stopLoss        = 0.0;
    bool   stopLossActive  = false;   // deactivated by default
    bool   shortEnabled    = false;   // short trades disabled by default

    Trade() = default;

    bool isChild()  const { return parentTradeId >= 0; }
    bool isParent() const { return type == TradeType::Buy && parentTradeId < 0; }

    void setTakeProfit(double tp)
    {
        if (type != TradeType::Buy)
            throw std::logic_error("Take-profit is only supported for Buy trades");
        takeProfit = tp;
    }

    void setStopLoss(double sl, bool active = false)
    {
        if (type != TradeType::Buy)
            throw std::logic_error("Stop-loss is only supported for Buy trades");
        stopLoss       = sl;
        stopLossActive = active;
    }
};
