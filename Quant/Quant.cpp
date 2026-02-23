#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>
#include <limits>
#include <algorithm>

#include "Trade.h"
#include "ProfitCalculator.h"
#include "MultiHorizonEngine.h"
#include "MarketEntryCalculator.h"
#include "ExitStrategyCalculator.h"
#include "TradeDatabase.h"
#include "HttpApi.h"

#include <thread>
#include <mutex>

static void printSep() { std::cout << "----------------------------------------\n"; }

static void clearInput()
{
    std::cin.clear();
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
}

static double readDouble(const char* prompt)
{
    double v;
    while (true)
    {
        std::cout << prompt;
        if (std::cin >> v) return v;
        std::cout << "  Invalid number, try again.\n";
        clearInput();
    }
}

static double readDouble(const std::string& prompt) { return readDouble(prompt.c_str()); }

static int readInt(const char* prompt)
{
    int v;
    while (true)
    {
        std::cout << prompt;
        if (std::cin >> v) return v;
        std::cout << "  Invalid number, try again.\n";
        clearInput();
    }
}

static int readInt(const std::string& prompt) { return readInt(prompt.c_str()); }

static std::string readSymbol(const char* prompt)
{
    std::string s;
    std::cout << prompt;
    std::cin >> s;
    return normalizeSymbol(s);
}

static std::string readSymbol(const std::string& prompt) { return readSymbol(prompt.c_str()); }

static std::string readString(const char* prompt)
{
    std::string s;
    std::cout << prompt;
    std::cin >> s;
    return s;
}

static std::string readString(const std::string& prompt) { return readString(prompt.c_str()); }

static HorizonParams readHorizonParams(const char* levelsPrompt = "  How many levels: ")
{
HorizonParams p;
if (levelsPrompt)
{
    p.horizonCount = readInt(levelsPrompt);
    if (p.horizonCount < 1) p.horizonCount = 1;
}
else
{
    p.horizonCount = 1;
}
p.feeHedgingCoefficient = readDouble("  Fee hedging coefficient: ");
    p.portfolioPump         = readDouble("  Portfolio pump (t): ");
    p.symbolCount           = readInt("  Symbol count in portfolio: ");
    if (p.symbolCount < 1) p.symbolCount = 1;
    p.coefficientK          = readDouble("  Coefficient K: ");
    p.feeSpread             = readDouble("  Fee spread / slippage: ");
    p.deltaTime             = readDouble("  Delta time: ");
    p.surplusRate           = readDouble("  Surplus rate (profit margin, e.g. 0.02 = 2%): ");
    return p;
}

// Exit strategy params: no pump (not injecting capital), no batch fees (per-exit)
static HorizonParams readExitParams()
{
    HorizonParams p;
    p.horizonCount          = 1;
    p.feeHedgingCoefficient = readDouble("  Fee hedging coefficient: ");
    p.symbolCount           = readInt("  Symbol count in portfolio: ");
    if (p.symbolCount < 1) p.symbolCount = 1;
    p.coefficientK          = readDouble("  Coefficient K: ");
    p.feeSpread             = readDouble("  Fee spread / slippage: ");
    p.deltaTime             = readDouble("  Delta time: ");
    p.surplusRate           = readDouble("  Surplus rate (profit margin, e.g. 0.02 = 2%): ");
    return p;
}

static void printOverhead(double overhead, double surplusRate, double posDelta)
{
    double eo = overhead + surplusRate;
    std::cout << std::fixed << std::setprecision(6)
              << "  overhead ratio  = " << overhead
              << "  (" << (overhead * 100.0) << "% cost per level)\n"
              << "  surplus rate    = " << surplusRate
              << "  (" << (surplusRate * 100.0) << "% profit margin)\n"
              << "  effective rate  = " << eo
              << "  (" << (eo * 100.0) << "% per level)\n"
              << "  position delta  = " << posDelta
              << "  (" << (posDelta * 100.0) << "% of portfolio)\n";
}

// ---- Menu actions ----

static void listTrades(TradeDatabase& db)
{
    auto trades  = db.loadTrades();
    if (trades.empty()) { std::cout << "  (no trades)\n"; return; }

    auto pending = db.loadPendingExits();

    std::cout << std::fixed << std::setprecision(2);

    // show Buy trades first, with children and pending exits indented underneath
    for (const auto& t : trades)
    {
        if (t.type != TradeType::Buy) continue;

        double sold      = db.soldQuantityForParent(t.tradeId);
        double remaining = t.quantity - sold;

        std::cout << "  #" << t.tradeId
                  << "  " << t.symbol
                  << "  BUY"
                  << "  price=" << t.value
                  << "  qty=" << t.quantity
                  << "  TP=" << t.takeProfit
                  << "  SL=" << t.stopLoss
                  << (t.stopLossActive ? " [SL ON]" : " [SL OFF]");
        if (sold > 0.0)
            std::cout << "  sold=" << sold
                      << "  remaining=" << remaining;
        std::cout << '\n';

        // show executed child CoveredSell trades
        for (const auto& c : trades)
        {
            if (c.parentTradeId != t.tradeId) continue;
            double exitPnl = (c.value - t.value) * c.quantity;
            std::cout << "    -> #" << c.tradeId
                      << "  EXIT"
                      << "  price=" << c.value
                      << "  qty=" << c.quantity
                      << "  P&L=" << exitPnl
                      << " (" << ((c.value - t.value) / t.value * 100.0) << "%)\n";
        }

        // show pending exit orders for this trade
        for (const auto& pe : pending)
        {
            if (pe.tradeId != t.tradeId) continue;
            double pctGain = ((pe.triggerPrice - t.value) / t.value) * 100.0;
            std::cout << "    .. order#" << pe.orderId
                      << "  PENDING EXIT"
                      << "  trigger=" << pe.triggerPrice
                      << "  (+" << pctGain << "%)"
                      << "  qty=" << pe.sellQty
                      << "  [" << pe.levelIndex << "]\n";
        }
    }

    // show orphan CoveredSells (parent deleted or missing)
    for (const auto& t : trades)
    {
        if (t.type != TradeType::CoveredSell) continue;
        bool hasParent = false;
        for (const auto& p : trades)
            if (p.tradeId == t.parentTradeId && p.type == TradeType::Buy)
            { hasParent = true; break; }
        if (hasParent) continue;

        std::cout << "  #" << t.tradeId
                  << "  " << t.symbol
                  << "  COVERED_SELL"
                  << "  price=" << t.value
                  << "  qty=" << t.quantity
                  << "  parent=#" << t.parentTradeId << " (missing)\n";
    }
}

static void addTrade(TradeDatabase& db)
{
    Trade t;
    t.tradeId = db.nextTradeId();
    t.symbol  = readSymbol("  Symbol: ");

    int typeChoice = readInt("  Type (0=Buy, 1=CoveredSell): ");
    t.type = (typeChoice == 1) ? TradeType::CoveredSell : TradeType::Buy;

    t.value    = readDouble("  Entry price per unit: ");
    if (t.value <= 0.0) { std::cout << "  Price must be positive.\n"; return; }
    t.quantity = readDouble("  Quantity: ");
    if (t.quantity <= 0.0) { std::cout << "  Quantity must be positive.\n"; return; }

    if (t.type == TradeType::CoveredSell)
    {
        std::cout << "  Available parent Buy trades:\n";
        auto trades = db.loadTrades();
        for (const auto& bt : trades)
            if (bt.isParent())
            {
                double sold = db.soldQuantityForParent(bt.tradeId);
                std::cout << "    #" << bt.tradeId << "  " << bt.symbol
                          << "  qty=" << bt.quantity
                          << "  already sold=" << sold
                          << "  remaining=" << (bt.quantity - sold) << '\n';
            }
        t.parentTradeId = readInt("  Parent trade ID: ");

        auto* parent = db.findTradeById(trades, t.parentTradeId);
        if (!parent || !parent->isParent())
        {
            std::cout << "  Invalid parent trade.\n";
            return;
        }
        double remaining = parent->quantity - db.soldQuantityForParent(parent->tradeId);
        if (t.quantity > remaining)
        {
            std::cout << "  Cannot sell " << t.quantity
                      << ", only " << remaining << " remaining.\n";
            return;
        }
        t.symbol = parent->symbol;
    }

    t.stopLossActive = false;
    t.shortEnabled   = false;

    db.addTrade(t);
    std::cout << "  -> Trade #" << t.tradeId << " saved.\n";
}

static void deleteTrade(TradeDatabase& db)
{
    listTrades(db);
    int id = readInt("  Trade ID to delete: ");
    auto trades = db.loadTrades();
    auto* tp = db.findTradeById(trades, id);
    if (!tp) { std::cout << "  Trade not found.\n"; return; }

    if (tp->isParent())
    {
        bool hasChildren = false;
        for (const auto& t : trades)
            if (t.parentTradeId == id) { hasChildren = true; break; }
        if (hasChildren)
            std::cout << "  Warning: child CoveredSell trades will also be deleted.\n";
    }

    db.removeTrade(id);
    std::cout << "  -> Deleted.\n";
}

static void editTrade(TradeDatabase& db)
{
    listTrades(db);
    int id = readInt("  Trade ID: ");

    auto trades = db.loadTrades();
    auto* tp = db.findTradeById(trades, id);
    if (!tp) { std::cout << "  Trade not found.\n"; return; }

    std::cout << "  Enter new values (enter 0 to keep current):\n";
    std::cout << std::fixed << std::setprecision(2);

    auto newSym = readSymbol("  Symbol [" + tp->symbol + "]: ");
    if (newSym != "0") tp->symbol = newSym;

    int newType = readInt("  Type 0=Buy 1=CoveredSell [-1=keep]: ");
    if (newType >= 0) tp->type = (newType == 1) ? TradeType::CoveredSell : TradeType::Buy;

    double v;
    v = readDouble("  Entry price [" + std::to_string(tp->value) + "]: ");
    if (v != 0) tp->value = v;

    v = readDouble("  Quantity [" + std::to_string(tp->quantity) + "]: ");
    if (v != 0) tp->quantity = v;

    if (tp->type == TradeType::CoveredSell)
    {
        int pid = readInt("  Parent trade ID [" + std::to_string(tp->parentTradeId) + "] (-1=keep): ");
        if (pid >= 0) tp->parentTradeId = pid;
    }

    v = readDouble("  Take profit [" + std::to_string(tp->takeProfit) + "]: ");
    if (v != 0) tp->takeProfit = v;

    v = readDouble("  Stop loss [" + std::to_string(tp->stopLoss) + "]: ");
    if (v != 0) tp->stopLoss = v;

    db.updateTrade(*tp);
    std::cout << "  -> Trade #" << tp->tradeId << " updated.\n";
}

static void calculateProfit(TradeDatabase& db)
{
    listTrades(db);
    int id = readInt("  Trade ID: ");

    auto trades = db.loadTrades();
    auto* tp = db.findTradeById(trades, id);
    if (!tp) { std::cout << "  Trade not found.\n"; return; }

    double cur      = readDouble("  Current market price: ");
    double buyFees  = readDouble("  Buy fees: ");
    double sellFees = readDouble("  Sell fees: ");
    auto r = ProfitCalculator::calculate(*tp, cur, buyFees, sellFees);
    db.saveProfitSnapshot(tp->symbol, id, cur, r);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  Gross profit : " << r.grossProfit << '\n';
    std::cout << "  Net profit   : " << r.netProfit << '\n';
    std::cout << "  ROI          : " << r.roi << "%\n";
    std::cout << "  (snapshot saved)\n";
}

static void generateHorizons(TradeDatabase& db)
{
    auto trades = db.loadTrades();

    // collect symbols that have Buy trades
    std::vector<std::string> symbols;
    for (const auto& t : trades)
        if (t.type == TradeType::Buy &&
            std::find(symbols.begin(), symbols.end(), t.symbol) == symbols.end())
            symbols.push_back(t.symbol);

    if (symbols.empty()) { std::cout << "  (no Buy trades)\n"; return; }

    std::cout << "  Symbols with Buy trades:\n";
    for (const auto& sym : symbols)
    {
        int count = 0;
        for (const auto& t : trades)
            if (t.symbol == sym && t.type == TradeType::Buy) ++count;
        std::cout << "    " << sym << "  (" << count << " trades)\n";
    }

    auto sym = readSymbol("  Symbol to generate horizons for: ");

    std::vector<Trade*> buyTrades;
    for (auto& t : trades)
        if (t.symbol == sym && t.type == TradeType::Buy)
            buyTrades.push_back(&t);

    if (buyTrades.empty())
    {
        std::cout << "  No Buy trades for " << sym << ".\n";
        return;
    }

    std::cout << std::fixed << std::setprecision(2);
    for (const auto* bt : buyTrades)
    {
        std::cout << "    #" << bt->tradeId
                  << "  price=" << bt->value
                  << "  qty=" << bt->quantity
                  << "  cost=" << (bt->value * bt->quantity) << '\n';
    }

    std::cout << "  Enter trade IDs to include (comma-separated, or 0 for all): ";
    {
        clearInput();
        std::string line;
        std::getline(std::cin, line);

        if (line.find('0') == std::string::npos || line.size() > 1)
        {
            std::vector<int> selected;
            std::istringstream ss(line);
            std::string tok;
            while (std::getline(ss, tok, ','))
            {
                try { selected.push_back(std::stoi(tok)); }
                catch (...) {}
            }

            if (!selected.empty())
            {
                std::erase_if(buyTrades, [&](const Trade* bt) {
                    return std::find(selected.begin(), selected.end(), bt->tradeId) == selected.end();
                });
            }
        }
    }

    if (buyTrades.empty())
    {
        std::cout << "  No trades selected.\n";
        return;
    }

    std::cout << "  Selected " << buyTrades.size() << " trade(s) -> "
              << buyTrades.size() << " TP/SL level(s).\n";

    HorizonParams p = readHorizonParams(nullptr);

    int genSL = readInt("  Generate stop losses? (0=no, 1=yes): ");
    p.generateStopLosses = (genSL == 1);

    // compute total cost of all selected trades to budget the pump fairly
    double totalCost = 0.0;
    for (const auto* bt : buyTrades)
        totalCost += bt->value * bt->quantity;

    double walletBal    = db.loadWalletBalance();
    double pumpBudget   = p.portfolioPump;
    double fundingCap   = pumpBudget + walletBal;
    double remainingCap = fundingCap;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  Pump budget: " << pumpBudget
              << "  Wallet: " << walletBal
              << "  Funding cap: " << fundingCap << "\n\n";

    for (auto* bt : buyTrades)
    {
        double originalQty = bt->quantity;
        double maxFundable = (bt->value > 0.0) ? remainingCap / bt->value : 0.0;
        double fundedQty   = MultiHorizonEngine::fundedQuantity(bt->value, p);
        if (fundedQty > maxFundable)
        {
            std::cout << "  Trade #" << bt->tradeId
                      << "  funded qty capped: " << fundedQty
                      << " -> " << maxFundable
                      << " (remaining cap=" << remainingCap << ")\n";
            fundedQty = maxFundable;
        }
        double fundedCost = fundedQty * bt->value;
        remainingCap -= fundedCost;
        if (remainingCap < 0.0) remainingCap = 0.0;
        bt->quantity += fundedQty;

        auto levels = MultiHorizonEngine::generate(*bt, p);
        MultiHorizonEngine::applyFirstHorizon(*bt, levels, false);
        db.updateTrade(*bt);
        db.saveHorizonLevels(sym, bt->tradeId, levels);

        std::cout << "  Trade #" << bt->tradeId
                  << "  price=" << bt->value
                  << "  qty=" << originalQty;
        if (fundedQty > 0.0)
            std::cout << " + funded " << fundedQty
                      << " = " << bt->quantity;
        std::cout << '\n';

        for (const auto& lv : levels)
        {
            double tpPerUnit = lv.takeProfit / bt->quantity;
            double slPerUnit = (lv.stopLoss > 0.0) ? lv.stopLoss / bt->quantity : 0.0;
            std::cout << "    [" << lv.index << "]"
                      << "  TP=" << lv.takeProfit
                      << " (" << tpPerUnit << "/unit)"
                      << "  SL=" << lv.stopLoss;
            if (slPerUnit > 0.0)
                std::cout << " (" << slPerUnit << "/unit)";
            std::cout << (lv.stopLossActive ? " [ON]" : " [OFF]")
                      << '\n';
        }

        db.saveParamsSnapshot(
            TradeDatabase::ParamsRow::from("horizon", sym, bt->tradeId,
                                           bt->value, bt->quantity, p));
    }

    std::cout << "  -> Saved to DB for " << buyTrades.size()
              << " trades. SL is OFF by default.\n";
}

static void viewHorizons(TradeDatabase& db)
{
    auto sym = readSymbol("  Symbol (or trade ID with #, e.g. #5): ");

    auto trades = db.loadTrades();

    // if user typed a number, treat as trade ID for backward compat
    std::vector<HorizonLevel> levels;
    std::string displayLabel;

    bool foundById = false;
    if (!sym.empty())
    {
        // try to find a trade with this symbol
        for (const auto& t : trades)
        {
            if (t.symbol == sym && t.type == TradeType::Buy)
            {
                levels = db.loadHorizonLevels(sym, t.tradeId);
                if (!levels.empty())
                {
                    displayLabel = sym + " (from trade #" + std::to_string(t.tradeId) + ")";
                    foundById = true;
                    break;
                }
            }
        }
    }

    if (!foundById)
    {
        // fallback: ask for trade ID
        int id = readInt("  Trade ID: ");
        auto* tp = db.findTradeById(trades, id);
        if (!tp) { std::cout << "  Trade not found.\n"; return; }
        levels = db.loadHorizonLevels(tp->symbol, id);
        displayLabel = tp->symbol + " #" + std::to_string(id);
    }

    if (levels.empty()) { std::cout << "  (no levels)\n"; return; }

    std::cout << "  Horizons for " << displayLabel << ":\n";
    std::cout << std::fixed << std::setprecision(2);
    for (const auto& lv : levels)
    {
        std::cout << "    [" << lv.index << "]"
                  << "  TP=" << lv.takeProfit
                  << "  SL=" << lv.stopLoss
                  << (lv.stopLossActive ? " [ON]" : " [OFF]")
                  << '\n';
    }
}

static void toggleStopLoss(TradeDatabase& db)
{
    listTrades(db);
    int id = readInt("  Trade ID: ");

    auto trades = db.loadTrades();
    auto* tp = db.findTradeById(trades, id);
    if (!tp) { std::cout << "  Trade not found.\n"; return; }

    tp->stopLossActive = !tp->stopLossActive;
    db.updateTrade(*tp);
    std::cout << "  -> SL is now " << (tp->stopLossActive ? "ON" : "OFF") << "\n";
}

static void portfolioSummary(TradeDatabase& db)
{
    auto trades = db.loadTrades();
    if (trades.empty()) { std::cout << "  (no trades)\n"; return; }

    double totalCost = 0.0;
    int buyCount = 0, sellCount = 0;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  Symbol summary:\n";

    // group by symbol
    std::vector<std::string> seen;
    for (const auto& t : trades)
    {
        if (std::find(seen.begin(), seen.end(), t.symbol) != seen.end()) continue;
        seen.push_back(t.symbol);

        double symCost = 0.0;
        double symQty  = 0.0;
        int buys = 0, sells = 0;
        for (const auto& u : trades)
        {
            if (u.symbol != t.symbol) continue;
            if (u.type == TradeType::Buy)
            {
                symCost += u.value * u.quantity;
                symQty  += u.quantity;
                ++buys;
            }
            else { ++sells; }
        }
        std::cout << "    " << t.symbol
                  << "  buys=" << buys << " sells=" << sells
                  << "  total_qty=" << symQty
                  << "  total_cost=" << symCost << '\n';
        totalCost += symCost;
        buyCount  += buys;
        sellCount += sells;
    }

    std::cout << "  --------\n";
    std::cout << "  Total: " << buyCount << " buys, "
              << sellCount << " sells, cost=" << totalCost << '\n';

    double walBal = db.loadWalletBalance();
    if (walBal != 0.0 || totalCost != 0.0)
    {
        std::cout << "  Wallet: liquid=" << walBal
                  << "  deployed=" << totalCost
                  << "  total=" << (walBal + totalCost) << '\n';
    }
}

static void viewProfitHistory(TradeDatabase& db)
{
    auto rows = db.loadProfitHistory();
    if (rows.empty()) { std::cout << "  (no history)\n"; return; }

    std::cout << std::fixed << std::setprecision(2);
    for (const auto& r : rows)
    {
        std::cout << "  #" << r.tradeId
                  << "  " << r.symbol
                  << "  @" << r.currentPrice
                  << "  gross=" << r.grossProfit
                  << "  net=" << r.netProfit
                  << "  ROI=" << r.roi << "%"
                  << '\n';
    }
}

static void marketEntry(TradeDatabase& db)
{
    double walletBal = db.loadWalletBalance();
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  Wallet liquid balance: " << walletBal << "\n\n";

    auto sym     = readSymbol("  Symbol: ");
    double cur   = readDouble("  Current market price: ");
    double qty   = readDouble("  Quantity you plan to buy: ");

    HorizonParams p = readHorizonParams("  How many entry levels: ");

    int fundMode = readInt("  Funding source (1=pump only, 2=pump + liquid balance): ");
    double availableFunds = p.portfolioPump;
    if (fundMode == 2)
    {
        availableFunds += walletBal;
        std::cout << "  Available funds: " << p.portfolioPump
                  << " (pump) + " << walletBal
                  << " (liquid) = " << availableFunds << "\n";
    }
    else
    {
        std::cout << "  Available funds: " << availableFunds << " (pump only)\n";
    }

    double risk = readDouble("  Risk coefficient (0=low risk, 1=high risk): ");
    int shortTrade = readInt("  Short trade? (0=no/long, 1=yes/short): ");
    bool isShort = (shortTrade == 1);

    // use availableFunds for entry level generation
    HorizonParams entryParams = p;
    entryParams.portfolioPump = availableFunds;

    auto levels = MarketEntryCalculator::generate(cur, qty, entryParams, risk);
    double eo = MultiHorizonEngine::effectiveOverhead(cur, qty, p);

    db.saveParamsSnapshot(
        TradeDatabase::ParamsRow::from("entry", sym, -1, cur, qty, p, risk));

    printOverhead(MultiHorizonEngine::computeOverhead(cur, qty, p), p.surplusRate,
                  MultiHorizonEngine::positionDelta(cur, qty, p.portfolioPump));

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n  " << sym << " @ " << cur
              << "  qty=" << qty
              << "  funds=" << availableFunds
              << "  risk=" << risk << "\n";
    std::cout << "  Entry levels (best entry points below current price):\n\n";

    for (const auto& el : levels)
    {
        double discount = ((cur - el.entryPrice) / cur) * 100.0;
        std::cout << "    [" << el.index << "]\n"
                  << "        entry price      = " << el.entryPrice
                  << "  (" << discount << "% below market)\n"
                  << "        break-even price = " << el.breakEven
                  << "  (min price to recover all fees + overhead)\n"
                  << "        potential profit = " << el.potentialNet
                  << "  (net gain if price returns to " << cur << " after fees)\n"
                  << "        funding          = " << el.funding
                  << " of " << availableFunds
                  << "  (" << (el.fundingFraction * 100.0) << "% of total funds)\n"
                  << "        buys             = " << el.fundingQty
                  << " units at this entry price\n"
                  << "        cost coverage    = " << el.costCoverage
                  << "x  (layers of overhead baked into this entry)\n";
    }

    // Build entry points with linked exit TP/SL
    int nextEpId = db.nextEntryId();
    std::vector<TradeDatabase::EntryPoint> entryPoints;
    for (const auto& el : levels)
    {
        TradeDatabase::EntryPoint ep;
        ep.symbol             = sym;
        ep.entryId            = nextEpId++;
        ep.levelIndex         = el.index;
        ep.entryPrice         = el.entryPrice;
        ep.breakEven          = el.breakEven;
        ep.funding            = el.funding;
        ep.fundingQty         = el.fundingQty;
        ep.effectiveOverhead  = eo;
        ep.isShort            = isShort;
        if (isShort)
        {
            ep.exitTakeProfit = el.entryPrice * (1.0 - eo);
            ep.exitStopLoss   = el.entryPrice * (1.0 + eo);
        }
        else
        {
            ep.exitTakeProfit = el.entryPrice * (1.0 + eo);
            ep.exitStopLoss   = el.entryPrice * (1.0 - eo);
        }
        entryPoints.push_back(ep);
    }

    int confirm = readInt("\n  Execute this entry strategy? (1=yes, 0=no): ");
    if (confirm == 1)
    {
        // deposit the pump capital into wallet before executing buys
        if (p.portfolioPump > 0.0)
        {
            db.deposit(p.portfolioPump);
            std::cout << "  -> Deposited pump " << p.portfolioPump
                      << " into wallet. Balance: " << db.loadWalletBalance() << "\n";
        }

        std::cout << "\n  Enter buy fees for each entry:\n";
        std::cout << std::fixed << std::setprecision(2);
        for (size_t i = 0; i < levels.size(); ++i)
        {
            double buyQty = (levels[i].fundingQty > 0.0) ? levels[i].fundingQty : 0.0;
            if (buyQty <= 0.0) continue;

            double cost = levels[i].entryPrice * buyQty;
            std::ostringstream fp;
            fp << std::fixed << std::setprecision(2)
               << "    [" << levels[i].index << "] " << buyQty
               << " @ " << levels[i].entryPrice
               << " (cost=" << cost << ")  buy fee: ";
            double buyFee = readDouble(fp.str());

            double walBal = db.loadWalletBalance();
            double totalNeeded = cost + buyFee;
            if (totalNeeded > walBal)
            {
                double maxQty = (levels[i].entryPrice > 0.0)
                    ? (walBal - buyFee) / levels[i].entryPrice : 0.0;
                if (maxQty <= 0.0)
                {
                    std::cout << "    [" << levels[i].index
                              << "] SKIPPED: insufficient funds (need "
                              << totalNeeded << ", have " << walBal << ")\n";
                    continue;
                }
                std::cout << "    [" << levels[i].index
                          << "] capped: " << buyQty << " -> " << maxQty
                          << " (wallet=" << walBal << ")\n";
                buyQty = maxQty;
                cost   = levels[i].entryPrice * buyQty;
            }

            int bid = db.executeBuy(sym, levels[i].entryPrice, buyQty);
            if (buyFee > 0.0)
                db.withdraw(buyFee);

            entryPoints[i].traded        = true;
            entryPoints[i].linkedTradeId = bid;

            auto trades = db.loadTrades();
            auto* tradePtr = db.findTradeById(trades, bid);
            if (tradePtr)
            {
                tradePtr->takeProfit     = entryPoints[i].exitTakeProfit * tradePtr->quantity;
                tradePtr->stopLoss       = entryPoints[i].exitStopLoss * tradePtr->quantity;
                tradePtr->stopLossActive = false;
                db.updateTrade(*tradePtr);
            }

            std::cout << "    -> Buy #" << bid
                      << "  " << buyQty << " @ " << levels[i].entryPrice
                      << "  fee=" << buyFee
                      << "  TP=" << entryPoints[i].exitTakeProfit
                      << "  SL=" << entryPoints[i].exitStopLoss
                      << "  (funded " << levels[i].funding << ")\n";
        }
        std::cout << "  -> Entry orders created. Wallet debited (including fees).\n";
    }

    auto existingEp = db.loadEntryPoints();
    for (const auto& ep : entryPoints) existingEp.push_back(ep);
    db.saveEntryPoints(existingEp);
    std::cout << "  -> " << entryPoints.size() << " entry points saved.\n";
}

static void viewParams(TradeDatabase& db)
{
    auto rows = db.loadParamsHistory();
    if (rows.empty()) { std::cout << "  (no parameter history)\n"; return; }

    std::cout << std::fixed << std::setprecision(4);
    int idx = 0;
    for (const auto& r : rows)
    {
        std::cout << "  [" << idx++ << "] " << r.calcType
                  << "  " << r.symbol;
        if (r.tradeId >= 0)
            std::cout << "  trade=#" << r.tradeId;
        std::cout << "\n"
                  << "      price=" << r.currentPrice
                  << "  qty=" << r.quantity
                  << "  levels=" << r.horizonCount << "\n"
                  << "      buyFees=" << r.buyFees
                  << "  sellFees=" << r.sellFees
                  << "  hedging=" << r.feeHedgingCoefficient << "\n"
                  << "      pump=" << r.portfolioPump
                  << "  symbols=" << r.symbolCount << "\n"
                  << "      K=" << r.coefficientK
                  << "  spread=" << r.feeSpread
                  << "  dt=" << r.deltaTime
                  << "  surplus=" << r.surplusRate << "\n";
        if (r.calcType == "horizon")
            std::cout << "      stopLosses=" << (r.generateStopLosses ? "yes" : "no") << "\n";
        if (r.calcType == "entry" || r.calcType == "exit")
            std::cout << "      risk=" << r.riskCoefficient << "\n";
    }
}

static void exitStrategy(TradeDatabase& db)
{
    listTrades(db);

    std::cout << "  Trade IDs (comma-separated): ";
    clearInput();
    std::string line;
    std::getline(std::cin, line);

    std::vector<int> ids;
    {
        std::istringstream ss(line);
        std::string tok;
        while (std::getline(ss, tok, ','))
        {
            try { ids.push_back(std::stoi(tok)); }
            catch (...) {}
        }
    }

    if (ids.empty()) { std::cout << "  No trade IDs entered.\n"; return; }

    auto trades = db.loadTrades();
    std::vector<Trade*> selected;
    for (int id : ids)
    {
        auto* tp = db.findTradeById(trades, id);
        if (!tp) { std::cout << "  Trade #" << id << " not found.\n"; continue; }
        if (tp->type != TradeType::Buy) { std::cout << "  #" << id << " is not a Buy trade.\n"; continue; }
        selected.push_back(tp);
    }

    if (selected.empty()) { std::cout << "  No valid Buy trades selected.\n"; return; }

    std::cout << "  Selected " << selected.size() << " trade(s) -> "
              << selected.size() << " exit level(s).\n";

    HorizonParams p = readExitParams();

    double risk     = readDouble("  Risk coefficient (0=sell early, 1=hold for deeper): ");
    double exitFrac = readDouble("  Exit fraction (0.0-1.0, e.g. 0.5 = sell 50%): ");
    double steep    = readDouble("  Sigmoid steepness (0=uniform, 4=smooth S, 10+=step): ");

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n";

    // First pass: generate and display exit levels per trade
    struct TradeExit {
        Trade* tp;
        std::vector<ExitLevel> levels;
        double sellableQty;
        double remaining;
    };
    std::vector<TradeExit> tradeExits;

    // load pending exits to account for already-reserved qty
    auto existingPending = db.loadPendingExits();

    for (auto* tp : selected)
    {
        double sold = db.soldQuantityForParent(tp->tradeId);
        double pendingQty = 0.0;
        for (const auto& pe : existingPending)
            if (pe.tradeId == tp->tradeId)
                pendingQty += pe.sellQty;

        double remaining = tp->quantity - sold - pendingQty;
        if (remaining <= 0.0)
        {
            std::cout << "  #" << tp->tradeId << "  " << tp->symbol
                      << "  fully committed (sold=" << sold
                      << " pending=" << pendingQty << "), skipping.\n";
            continue;
        }

        // generate exits based on remaining qty, not total qty
        Trade tempTrade = *tp;
        tempTrade.quantity = remaining;

        auto levels = ExitStrategyCalculator::generate(tempTrade, p, risk, exitFrac, steep);
        double clampedFrac = (exitFrac < 0.0) ? 0.0 : (exitFrac > 1.0) ? 1.0 : exitFrac;
        double sellableQty = remaining * clampedFrac;

        std::cout << "  Exit for #" << tp->tradeId
                  << "  " << tp->symbol
                  << "  entry=" << tp->value
                  << "  total_qty=" << tp->quantity
                  << "  sold=" << sold
                  << "  pending=" << pendingQty
                  << "  available=" << remaining
                  << "  selling=" << sellableQty
                  << " (" << (clampedFrac * 100.0) << "%)\n";

        for (const auto& el : levels)
        {
            double pctGain = ((el.tpPrice - tp->value) / tp->value) * 100.0;
            std::cout << "    [" << el.index << "]"
                      << "  trigger=" << el.tpPrice
                      << "  (+" << pctGain << "%)"
                      << "  sell=" << el.sellQty
                      << "  value=" << el.sellValue << "\n";
        }
        std::cout << "    remaining after exit = " << (remaining - sellableQty) << "\n\n";

        tradeExits.push_back({tp, levels, sellableQty, remaining});
    }

    if (tradeExits.empty())
    {
        std::cout << "  No trades with available quantity.\n";
        return;
    }

    // Second pass: ask sell fees per trade exit and recalculate net
    std::cout << "  Enter sell fees for each exit:\n";
    std::vector<TradeDatabase::PendingExit> allOrders;
    int nextId = db.nextPendingId();
    double totalNet = 0.0;

    for (auto& te : tradeExits)
    {
        for (auto& el : te.levels)
        {
            if (el.sellQty <= 0.0) continue;

            std::ostringstream exitInfo;
            exitInfo << std::fixed << std::setprecision(2)
                     << "    #" << te.tp->tradeId
                     << " sell " << el.sellQty
                     << " @ " << el.tpPrice
                     << " (value=" << el.sellValue << ")";
            std::cout << exitInfo.str() << "\n";
            double buyFee  = readDouble("      Buy fee (original, proportional): ");
            double sellFee = readDouble("      Sell fee: ");

            el.netProfit    = el.grossProfit - buyFee - sellFee;
            totalNet       += el.netProfit;

            std::cout << "      -> gross=" << el.grossProfit
                      << "  buyFee=" << buyFee
                      << "  sellFee=" << sellFee
                      << "  net=" << el.netProfit << "\n";

            TradeDatabase::PendingExit pe;
            pe.symbol       = te.tp->symbol;
            pe.orderId      = nextId++;
            pe.tradeId      = te.tp->tradeId;
            pe.triggerPrice = el.tpPrice;
            pe.sellQty      = el.sellQty;
            pe.levelIndex   = el.index;
            allOrders.push_back(pe);
        }

        db.saveParamsSnapshot(
            TradeDatabase::ParamsRow::from("exit", te.tp->symbol, te.tp->tradeId,
                                           te.tp->value, te.tp->quantity, p, risk));
    }

    std::cout << "\n  Total net across all exits: " << totalNet << "\n";
    std::cout << "  (params saved)\n";

    if (!allOrders.empty())
    {
        double cur = readDouble("\n  Current market price: ");

        // split into immediately executable vs pending
        std::vector<TradeDatabase::PendingExit> hitOrders;
        std::vector<TradeDatabase::PendingExit> pendingOrders;
        for (const auto& pe : allOrders)
        {
            if (cur >= pe.triggerPrice)
                hitOrders.push_back(pe);
            else
                pendingOrders.push_back(pe);
        }

        // execute exits that have already hit their trigger
        if (!hitOrders.empty())
        {
            std::cout << "\n  " << hitOrders.size()
                      << " exit(s) already at or above trigger price:\n";
            for (const auto& pe : hitOrders)
            {
                std::cout << "    order#" << pe.orderId
                          << "  #" << pe.tradeId
                          << "  trigger=" << pe.triggerPrice
                          << "  qty=" << pe.sellQty
                          << "  << HIT\n";
            }
            int exec = readInt("  Execute these now? (1=yes, 0=save as pending): ");
            if (exec == 1)
            {
                for (const auto& pe : hitOrders)
                {
                    int sid = db.executeSell(pe.symbol, pe.triggerPrice, pe.sellQty);
                    if (sid >= 0)
                        std::cout << "    -> CoveredSell #" << sid
                                  << "  " << pe.sellQty << " @ " << pe.triggerPrice << "\n";
                    else
                        std::cout << "    -> Failed " << pe.symbol
                                  << " (insufficient holdings)\n";
                }
                std::cout << "  -> Executed. Wallet credited.\n";
            }
            else
            {
                // user chose not to execute, move to pending
                for (const auto& pe : hitOrders)
                    pendingOrders.push_back(pe);
            }
        }

        // save remaining as pending orders
        if (!pendingOrders.empty())
        {
            std::cout << "\n  " << pendingOrders.size()
                      << " exit(s) not yet triggered (price below trigger):\n";
            for (const auto& pe : pendingOrders)
            {
                double away = pe.triggerPrice - cur;
                std::cout << "    order#" << pe.orderId
                          << "  #" << pe.tradeId
                          << "  trigger=" << pe.triggerPrice
                          << "  qty=" << pe.sellQty
                          << "  (" << away << " away)\n";
            }
            int confirm = readInt("  Save as pending exit orders? (1=yes, 0=no): ");
            if (confirm == 1)
            {
                db.addPendingExits(pendingOrders);
                std::cout << "  -> " << pendingOrders.size()
                          << " pending exit orders saved.\n";
            }
        }
        else if (hitOrders.empty())
        {
            std::cout << "  (no exits to save)\n";
        }
    }
}

static void priceCheck(TradeDatabase& db)
{
    auto trades = db.loadTrades();
    bool anyBuy = false;
    for (const auto& t : trades)
        if (t.type == TradeType::Buy) { anyBuy = true; break; }
    if (!anyBuy) { std::cout << "  (no Buy trades)\n"; return; }

    // collect unique symbols and ask market price per symbol
    std::vector<std::string> symbols;
    for (const auto& t : trades)
        if (t.type == TradeType::Buy &&
            std::find(symbols.begin(), symbols.end(), t.symbol) == symbols.end())
            symbols.push_back(t.symbol);

    std::vector<std::pair<std::string, double>> symbolPrices;
    for (const auto& sym : symbols)
    {
        double price = readDouble(("  Current market price for " + sym + ": ").c_str());
        symbolPrices.push_back({sym, price});
    }

    auto priceForSymbol = [&](const std::string& sym) -> double {
        for (const auto& sp : symbolPrices)
            if (sp.first == sym) return sp.second;
        return 0.0;
    };

    std::cout << std::fixed << std::setprecision(2);
    std::cout << '\n';

    struct Trigger { int tradeId; std::string symbol; double price; double qty; std::string tag; };
    std::vector<Trigger> triggers;

    for (const auto& t : trades)
    {
        if (t.type != TradeType::Buy) continue;

        double remaining = t.quantity - db.soldQuantityForParent(t.tradeId);
        if (remaining <= 0.0) continue;

        double cur = priceForSymbol(t.symbol);

        double cost = t.value * remaining;
        std::cout << "  #" << t.tradeId << "  " << t.symbol
                  << "  entry=" << t.value << "  qty=" << remaining
                  << "  cost=" << cost
                  << "  market=" << cur << "\n";
        double buyFees  = readDouble("    Buy fee: ");
        double sellFees = readDouble("    Sell fee: ");

        auto r = ProfitCalculator::calculate(t, cur, buyFees, sellFees);

        std::cout << "    net=" << r.netProfit << "  ROI=" << r.roi << "%";

        // trade-level TP/SL
        if (t.takeProfit > 0.0)
        {
            double tpPrice = t.takeProfit / t.quantity;
            if (cur >= tpPrice)
            {
                std::cout << "  ** TP HIT (" << tpPrice << ") **";
                triggers.push_back({t.tradeId, t.symbol, cur, remaining, "TP"});
            }
        }
        if (t.stopLossActive && t.stopLoss > 0.0)
        {
            double slPrice = t.stopLoss / t.quantity;
            if (cur <= slPrice)
            {
                std::cout << "  !! SL BREACHED (" << slPrice << ") !!";
                triggers.push_back({t.tradeId, t.symbol, cur, remaining, "SL"});
            }
        }
        std::cout << '\n';

        // horizon levels
        auto levels = db.loadHorizonLevels(t.symbol, t.tradeId);
        if (levels.empty()) continue;

        for (const auto& lv : levels)
        {
            double tpPrice = lv.takeProfit / t.quantity;
            bool tpHit = (cur >= tpPrice);

            std::cout << "    [" << lv.index << "]  TP=" << lv.takeProfit
                      << " (" << tpPrice << "/unit)";
            if (tpHit)
            {
                double tpProfit = (cur - tpPrice) * t.quantity;
                std::cout << "  >> EXCEEDED  surplus=" << tpProfit;
            }
            else
            {
                std::cout << "  -- " << (tpPrice - cur) << " away";
            }

            if (lv.stopLoss != 0.0)
            {
                double slPrice = lv.stopLoss / t.quantity;
                bool slHit = (cur <= slPrice);
                std::cout << "  |  SL=" << lv.stopLoss
                          << " (" << slPrice << "/unit)";
                if (lv.stopLossActive)
                {
                    if (slHit)
                        std::cout << "  !! BREACHED";
                    else
                        std::cout << "  -- " << (cur - slPrice) << " above";
                }
                else
                {
                    std::cout << "  [OFF]";
                }
            }
            std::cout << '\n';
        }
    }

    if (!triggers.empty())
    {
        std::cout << "\n  Triggered:\n";
        for (size_t i = 0; i < triggers.size(); ++i)
        {
            const auto& tr = triggers[i];
            std::cout << "    [" << i << "] " << tr.tag
                      << " #" << tr.tradeId << "  " << tr.symbol
                      << "  qty=" << tr.qty << "  @ " << tr.price << "\n";
        }
        int exec = readInt("  Execute all triggered sells? (1=yes, 0=no): ");
        if (exec == 1)
        {
            for (const auto& tr : triggers)
            {
                int sid = db.executeSell(tr.symbol, tr.price, tr.qty);
                if (sid >= 0)
                    std::cout << "    -> CoveredSell #" << sid
                              << "  " << tr.qty << " @ " << tr.price << "\n";
                else
                    std::cout << "    -> Sell failed for " << tr.symbol << "\n";
            }
            std::cout << "  -> Executed. Wallet credited.\n";
        }
    }

    // ---- Pending exit orders ----
    auto pending = db.loadPendingExits();
    std::vector<TradeDatabase::PendingExit> triggered;
    for (const auto& pe : pending)
    {
        double cur = priceForSymbol(pe.symbol);
        if (cur >= pe.triggerPrice)
            triggered.push_back(pe);
    }

    if (!triggered.empty())
    {
        std::cout << "\n  Pending exits triggered:\n";
        for (const auto& pe : triggered)
        {
            double cur = priceForSymbol(pe.symbol);
            std::cout << "    order#" << pe.orderId
                      << "  " << pe.symbol << " #" << pe.tradeId
                      << "  [" << pe.levelIndex << "]"
                      << "  trigger=" << pe.triggerPrice
                      << "  market=" << cur
                      << "  qty=" << pe.sellQty << "\n";
        }
        int exec = readInt("  Execute triggered pending exits? (1=yes, 0=no): ");
        if (exec == 1)
        {
            for (const auto& pe : triggered)
            {
                double cur = priceForSymbol(pe.symbol);
                int sid = db.executeSell(pe.symbol, cur, pe.sellQty);
                if (sid >= 0)
                {
                    std::cout << "    -> CoveredSell #" << sid
                              << "  " << pe.sellQty << " @ " << cur
                              << "  (trigger was " << pe.triggerPrice << ")\n";
                    db.removePendingExit(pe.orderId);
                }
                else
                {
                    std::cout << "    -> Sell failed for " << pe.symbol
                              << " (insufficient holdings)\n";
                }
            }
            std::cout << "  -> Pending exits executed. Wallet credited.\n";
        }
    }
}

static void wipeDatabaseMenu(TradeDatabase& db)
{
    int c = readInt("  Are you sure? (1=yes, 0=no): ");
    if (c == 1) { db.clearAll(); std::cout << "  -> Database wiped.\n"; }
}

// ---- Replay params ----

static void replayParams(TradeDatabase& db)
{
    auto rows = db.loadParamsHistory();
    if (rows.empty()) { std::cout << "  (no parameter history)\n"; return; }

    std::cout << std::fixed << std::setprecision(2);
    for (int i = 0; i < static_cast<int>(rows.size()); ++i)
    {
        const auto& r = rows[i];
        std::cout << "  [" << i << "] " << r.calcType << "  " << r.symbol;
        if (r.tradeId >= 0) std::cout << " #" << r.tradeId;
        std::cout << "  levels=" << r.horizonCount << '\n';
    }

    int idx = readInt("  Replay which entry: ");
    if (idx < 0 || idx >= static_cast<int>(rows.size()))
    {
        std::cout << "  Invalid index.\n";
        return;
    }

    const auto& src = rows[idx];
    HorizonParams p = src.toHorizonParams();

    if (src.calcType == "horizon")
    {
        auto trades = db.loadTrades();
        auto* tp = db.findTradeById(trades, src.tradeId);
        if (!tp) { std::cout << "  Original trade #" << src.tradeId << " no longer exists.\n"; return; }

        auto levels = MultiHorizonEngine::generate(*tp, p);
        MultiHorizonEngine::applyFirstHorizon(*tp, levels, false);
        db.updateTrade(*tp);
        db.saveHorizonLevels(tp->symbol, tp->tradeId, levels);

        TradeDatabase::ParamsRow pr = src;
        db.saveParamsSnapshot(pr);

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "  Replayed horizon for " << tp->symbol << " #" << tp->tradeId << ":\n";
        for (const auto& lv : levels)
        {
            std::cout << "    [" << lv.index << "]"
                      << "  TP=" << lv.takeProfit
                      << "  SL=" << lv.stopLoss
                      << (lv.stopLossActive ? " [ON]" : " [OFF]")
                      << '\n';
        }
        std::cout << "  -> Saved to DB.\n";
    }
    else if (src.calcType == "exit")
    {
        auto trades = db.loadTrades();
        auto* tp = db.findTradeById(trades, src.tradeId);
        if (!tp) { std::cout << "  Original trade #" << src.tradeId << " no longer exists.\n"; return; }

        double risk = src.riskCoefficient;
        auto levels = ExitStrategyCalculator::generate(*tp, p, risk, 1.0, 4.0);

        TradeDatabase::ParamsRow pr = src;
        db.saveParamsSnapshot(pr);

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "  Replayed exit for " << tp->symbol << " #" << tp->tradeId << ":\n\n";
        for (const auto& el : levels)
        {
            std::cout << "    [" << el.index << "]"
                      << "  sell@" << el.tpPrice
                      << "  qty=" << el.sellQty
                      << " (" << (el.sellFraction * 100.0) << "%)"
                      << "  net=" << el.netProfit
                      << '\n';
        }
    }
    else // entry
    {
        double risk = src.riskCoefficient;
        auto levels = MarketEntryCalculator::generate(src.currentPrice, src.quantity, p, risk);

        TradeDatabase::ParamsRow pr = src;
        db.saveParamsSnapshot(pr);

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "  Replayed entry for " << src.symbol << " @ " << src.currentPrice << ":\n\n";
        for (const auto& el : levels)
        {
            double discount = ((src.currentPrice - el.entryPrice) / src.currentPrice) * 100.0;
            std::cout << "    [" << el.index << "]"
                      << "  entry=" << el.entryPrice
                      << "  (" << discount << "% below)"
                      << "  fund=" << el.funding
                      << " (" << (el.fundingFraction * 100.0) << "%)"
                      << '\n';
        }
    }
}

// ---- DCA tracker ----

static void dcaTracker(TradeDatabase& db)
{
    auto trades = db.loadTrades();
    if (trades.empty()) { std::cout << "  (no trades)\n"; return; }

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  DCA (Dollar Cost Average) per symbol:\n\n";

    std::vector<std::string> seen;
    for (const auto& t : trades)
    {
        if (t.type != TradeType::Buy) continue;
        if (std::find(seen.begin(), seen.end(), t.symbol) != seen.end()) continue;
        seen.push_back(t.symbol);

        double totalCost = 0.0;
        double totalQty  = 0.0;
        int    count     = 0;
        double minPrice  = 1e18, maxPrice = 0.0;

        for (const auto& u : trades)
        {
            if (u.symbol != t.symbol || u.type != TradeType::Buy) continue;
            totalCost += u.value * u.quantity;
            totalQty  += u.quantity;
            if (u.value < minPrice) minPrice = u.value;
            if (u.value > maxPrice) maxPrice = u.value;
            ++count;
        }

        double avgPrice = (totalQty != 0.0) ? totalCost / totalQty : 0.0;

        std::cout << "    " << t.symbol << "\n"
                  << "        buy trades     = " << count << "\n"
                  << "        total quantity = " << totalQty << "\n"
                  << "        total cost     = " << totalCost << "\n"
                  << "        avg entry (DCA)= " << avgPrice << "\n"
                  << "        lowest entry   = " << minPrice << "\n"
                  << "        highest entry  = " << maxPrice << "\n"
                  << "        spread         = " << (maxPrice - minPrice) << "\n\n";
    }
}

// ---- Unrealized P&L dashboard ----

static void unrealizedPnl(TradeDatabase& db)
{
    auto trades = db.loadTrades();
    if (trades.empty()) { std::cout << "  (no trades)\n"; return; }

    std::vector<std::string> symbols;
    for (const auto& t : trades)
        if (t.type == TradeType::Buy &&
            std::find(symbols.begin(), symbols.end(), t.symbol) == symbols.end())
            symbols.push_back(t.symbol);

    if (symbols.empty()) { std::cout << "  (no Buy trades)\n"; return; }

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n  Unrealized P&L:\n\n";

    double grandCost = 0.0, grandValue = 0.0, grandNet = 0.0;

    for (const auto& sym : symbols)
    {
        double curPrice = readDouble(("  Current price for " + sym + ": ").c_str());

        double symCost = 0.0, symQty = 0.0, symNet = 0.0;
        for (const auto& t : trades)
        {
            if (t.symbol != sym || t.type != TradeType::Buy) continue;

            double cost = t.value * t.quantity;
            std::cout << "    #" << t.tradeId << "  entry=" << t.value
                      << "  qty=" << t.quantity << "  cost=" << cost << "\n";
            double buyFees  = readDouble("      Buy fee: ");
            double sellFees = readDouble("      Sell fee: ");

            auto r = ProfitCalculator::calculate(t, curPrice, buyFees, sellFees);
            symCost += t.value * t.quantity;
            symQty  += t.quantity;
            symNet  += r.netProfit;
        }

        double symValue = curPrice * symQty;
        double symRoi   = (symCost != 0.0) ? (symNet / symCost) * 100.0 : 0.0;

        std::cout << "    " << sym
                  << "  qty=" << symQty
                  << "  cost=" << symCost
                  << "  value=" << symValue
                  << "  unrealized=" << symNet
                  << "  ROI=" << symRoi << "%\n";

        grandCost  += symCost;
        grandValue += symValue;
        grandNet   += symNet;
    }

    double grandRoi = (grandCost != 0.0) ? (grandNet / grandCost) * 100.0 : 0.0;
    std::cout << "    --------\n"
              << "    TOTAL"
              << "  cost=" << grandCost
              << "  value=" << grandValue
              << "  unrealized=" << grandNet
              << "  ROI=" << grandRoi << "%\n";
}

// ---- Export report ----

static void exportReport(TradeDatabase& db)
{
    int fmt = readInt("  Format (1=text, 2=HTML): ");
    if (fmt == 2)
    {
        auto path = readString("  Export file path (e.g. report.html): ");
        db.exportHtmlReport(path);
        std::cout << "  -> HTML report exported to " << path << "\n";
    }
    else
    {
        auto path = readString("  Export file path (e.g. report.txt): ");
        db.exportReport(path);
        std::cout << "  -> Text report exported to " << path << "\n";
    }
}

// ---- Duplicate trade ----

static void duplicateTrade(TradeDatabase& db)
{
    listTrades(db);
    int id = readInt("  Trade ID to duplicate: ");

    auto trades = db.loadTrades();
    auto* src = db.findTradeById(trades, id);
    if (!src) { std::cout << "  Trade not found.\n"; return; }

    Trade t = *src;
    t.tradeId      = db.nextTradeId();
    t.takeProfit   = 0.0;
    t.stopLoss     = 0.0;
    t.stopLossActive = false;

    db.addTrade(t);
    std::cout << "  -> Duplicated as trade #" << t.tradeId
              << " (" << t.symbol << " " << (t.type == TradeType::Buy ? "BUY" : "COVERED_SELL")
              << " price=" << t.value << " qty=" << t.quantity << ")\n";
}

// ---- Wallet ----

static void walletMenu(TradeDatabase& db)
{
    std::cout << std::fixed << std::setprecision(2);
    double balance  = db.loadWalletBalance();
    double deployed = db.deployedCapital();
    double total    = balance + deployed;

    std::cout << "  Wallet:\n"
              << "    liquid balance   = " << balance << "\n"
              << "    deployed capital = " << deployed << "\n"
              << "    total capital    = " << total << "\n\n";

    int c = readInt("  1=Deposit  2=Withdraw  0=Back: ");
    if (c == 1)
    {
        double amt = readDouble("  Deposit amount: ");
        if (amt <= 0.0) { std::cout << "  Amount must be positive.\n"; return; }
        db.deposit(amt);
        std::cout << "  -> Deposited " << amt
                  << ". Balance: " << db.loadWalletBalance() << "\n";
    }
    else if (c == 2)
    {
        double amt = readDouble("  Withdraw amount: ");
        if (amt <= 0.0) { std::cout << "  Amount must be positive.\n"; return; }
        if (amt > balance)
        {
            std::cout << "  Cannot withdraw " << amt
                      << ", only " << balance << " available.\n";
            return;
        }
        db.withdraw(amt);
        std::cout << "  -> Withdrew " << amt
                  << ". Balance: " << db.loadWalletBalance() << "\n";
    }
}

// ---- Pending exits ----

static void viewPendingExits(TradeDatabase& db)
{
    auto orders = db.loadPendingExits();
    if (orders.empty()) { std::cout << "  (no pending exit orders)\n"; return; }

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  Pending exit orders:\n";
    for (const auto& pe : orders)
    {
        std::cout << "    order#" << pe.orderId
                  << "  " << pe.symbol << " #" << pe.tradeId
                  << "  [" << pe.levelIndex << "]"
                  << "  trigger=" << pe.triggerPrice
                  << "  qty=" << pe.sellQty << "\n";
    }

    int c = readInt("  1=Clear all  2=Remove one  3=Execute  0=Back: ");
    if (c == 1)
    {
        db.savePendingExits({});
        std::cout << "  -> All pending exits cleared.\n";
    }
    else if (c == 2)
    {
        int oid = readInt("  Order ID to remove: ");
        db.removePendingExit(oid);
        std::cout << "  -> Removed order#" << oid << ".\n";
    }
    else if (c == 3)
    {
        std::cout << "  Order IDs to execute (comma-separated, or 0 for all): ";
        clearInput();
        std::string line;
        std::getline(std::cin, line);

        bool execAll = (line == "0");
        std::vector<int> execIds;
        if (!execAll)
        {
            std::istringstream ss(line);
            std::string tok;
            while (std::getline(ss, tok, ','))
            {
                try { execIds.push_back(std::stoi(tok)); }
                catch (...) {}
            }
        }

        double cur = readDouble("  Current market price: ");

        std::cout << std::fixed << std::setprecision(2);
        int executed = 0;
        int skipped  = 0;
        for (const auto& pe : orders)
        {
            if (!execAll &&
                std::find(execIds.begin(), execIds.end(), pe.orderId) == execIds.end())
                continue;

            if (cur < pe.triggerPrice)
            {
                std::cout << "    -- order#" << pe.orderId
                          << "  trigger=" << pe.triggerPrice
                          << " not reached (" << (pe.triggerPrice - cur) << " away)\n";
                ++skipped;
                continue;
            }

            int sid = db.executeSell(pe.symbol, pe.triggerPrice, pe.sellQty);
            if (sid >= 0)
            {
                std::cout << "    -> CoveredSell #" << sid
                          << "  " << pe.sellQty << " @ " << pe.triggerPrice
                          << "  (order#" << pe.orderId << ")\n";
                db.removePendingExit(pe.orderId);
                ++executed;
            }
            else
            {
                std::cout << "    -> Failed " << pe.symbol
                          << " (insufficient holdings)\n";
            }
        }
        if (skipped > 0)
            std::cout << "  " << skipped << " order(s) skipped (trigger not reached).\n";
        std::cout << "  -> Executed " << executed << " exit order(s) at trigger prices. Wallet credited.\n";
    }
}

// ---- Entry points ----

static void viewEntryPoints(TradeDatabase& db)
{
    auto points = db.loadEntryPoints();
    if (points.empty()) { std::cout << "  (no entry points)\n"; return; }

    std::cout << std::fixed << std::setprecision(2);
    for (const auto& ep : points)
    {
        std::cout << "  [" << ep.entryId << "]"
                  << "  " << ep.symbol
                  << "  lvl=" << ep.levelIndex
                  << "  entry=" << ep.entryPrice
                  << "  BE=" << ep.breakEven
                  << "  qty=" << ep.fundingQty
                  << (ep.isShort ? " SHORT" : " LONG")
                  << (ep.traded ? " [TRADED]" : " [OPEN]");
        if (ep.traded)
        {
            std::cout << "  TP=" << ep.exitTakeProfit
                      << "  SL=" << ep.exitStopLoss
                      << (ep.stopLossActive ? " [SL ON]" : " [SL OFF]");
            if (ep.linkedTradeId >= 0)
                std::cout << "  trade=#" << ep.linkedTradeId;
        }
        else
        {
            std::cout << "  exitTP=" << ep.exitTakeProfit
                      << "  exitSL=" << ep.exitStopLoss;
        }
        std::cout << '\n';
    }
}

static void markEntryTraded(TradeDatabase& db)
{
    auto points = db.loadEntryPoints();
    if (points.empty()) { std::cout << "  (no entry points)\n"; return; }

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  Open entry points:\n";
    bool anyOpen = false;
    for (const auto& ep : points)
    {
        if (ep.traded) continue;
        anyOpen = true;
        std::cout << "    [" << ep.entryId << "]"
                  << "  " << ep.symbol
                  << "  lvl=" << ep.levelIndex
                  << "  entry=" << ep.entryPrice
                  << "  qty=" << ep.fundingQty
                  << (ep.isShort ? " SHORT" : " LONG")
                  << "  exitTP=" << ep.exitTakeProfit
                  << "  exitSL=" << ep.exitStopLoss
                  << '\n';
    }
    if (!anyOpen) { std::cout << "  (all entry points already traded)\n"; return; }

    int id = readInt("  Entry ID to mark as traded: ");

    TradeDatabase::EntryPoint* target = nullptr;
    for (auto& ep : points)
        if (ep.entryId == id) { target = &ep; break; }

    if (!target) { std::cout << "  Entry point not found.\n"; return; }
    if (target->traded) { std::cout << "  Already marked as traded.\n"; return; }

    target->traded = true;

    int slActive = readInt("  Activate stop-loss? (0=no, 1=yes): ");
    target->stopLossActive = (slActive == 1);

    int createTrade = readInt("  Create a Buy trade for this entry? (1=yes, 0=no): ");
    if (createTrade == 1)
    {
        int tid = db.executeBuy(target->symbol, target->entryPrice, target->fundingQty);
        target->linkedTradeId = tid;

        auto trades = db.loadTrades();
        auto* tp = db.findTradeById(trades, tid);
        if (tp)
        {
            tp->takeProfit     = target->exitTakeProfit * tp->quantity;
            tp->stopLoss       = target->exitStopLoss * tp->quantity;
            tp->stopLossActive = target->stopLossActive;
            db.updateTrade(*tp);
        }
        std::cout << "  -> Trade #" << tid << " created. Wallet debited.\n";
    }

    db.saveEntryPoints(points);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  -> Entry [" << target->entryId << "] marked as TRADED.\n"
              << "     " << (target->isShort ? "SHORT" : "LONG")
              << "  entry=" << target->entryPrice
              << "  TP=" << target->exitTakeProfit
              << "  SL=" << target->exitStopLoss
              << (target->stopLossActive ? " [SL ON]" : " [SL OFF]") << '\n';
}

// ---- Main ----

int main()
{
    TradeDatabase db("db");
    std::mutex dbMutex;

    // Start HTTP API on a background thread
    int httpPort = 8080;
    std::thread httpThread([&]() {
        startHttpApi(db, httpPort, dbMutex);
        });
    httpThread.detach();

    bool running = true;
    while (running)
    {
        std::cout << "\n";
        printSep();
        std::cout << "  QUANT TRADE MANAGER\n";
        printSep();
        std::cout << "   1) List trades\n";
        std::cout << "   2) Add trade\n";
        std::cout << "   3) Edit trade\n";
        std::cout << "   4) Delete trade\n";
        std::cout << "   5) Duplicate trade\n";
        std::cout << "   6) Calculate profit\n";
        std::cout << "   7) Generate TP/SL horizons\n";
        std::cout << "   8) View horizons\n";
        std::cout << "   9) Toggle stop-loss on/off\n";
        std::cout << "  10) DCA tracker\n";
        std::cout << "  11) Portfolio summary\n";
        std::cout << "  12) Unrealized P&L\n";
        std::cout << "  13) Profit history\n";
        std::cout << "  14) Price check (TP/SL vs market)\n";
        std::cout << "  15) Market entry calculator\n";
        std::cout << "  16) Exit strategy\n";
        std::cout << "  17) Parameter history\n";
        std::cout << "  18) Replay params\n";
        std::cout << "  19) Export report\n";
        std::cout << "  20) Wallet\n";
        std::cout << "  21) Pending exit orders\n";
        std::cout << "  22) Wipe database\n";
        std::cout << "  23) View entry points\n";
        std::cout << "  24) Mark entry as traded\n";
        std::cout << "   0) Exit\n";
        printSep();

        int choice = readInt("  > ");

        std::cout << '\n';
        switch (choice)
        {
        case 1:  listTrades(db);         break;
        case 2:  addTrade(db);           break;
        case 3:  editTrade(db);          break;
        case 4:  deleteTrade(db);        break;
        case 5:  duplicateTrade(db);     break;
        case 6:  calculateProfit(db);    break;
        case 7:  generateHorizons(db);   break;
        case 8:  viewHorizons(db);       break;
        case 9:  toggleStopLoss(db);     break;
        case 10: dcaTracker(db);         break;
        case 11: portfolioSummary(db);   break;
        case 12: unrealizedPnl(db);      break;
        case 13: viewProfitHistory(db);  break;
        case 14: priceCheck(db);         break;
        case 15: marketEntry(db);        break;
        case 16: exitStrategy(db);        break;
        case 17: viewParams(db);         break;
        case 18: replayParams(db);       break;
        case 19: exportReport(db);       break;
        case 20: walletMenu(db);         break;
        case 21: viewPendingExits(db);   break;
        case 22: wipeDatabaseMenu(db);   break;
        case 23: viewEntryPoints(db);    break;
        case 24: markEntryTraded(db);    break;
        case 0:  running = false;        break;
        default: std::cout << "  Invalid choice.\n"; break;
        }
    }

    return 0;

}
