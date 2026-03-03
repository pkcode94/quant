#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>
#include <limits>
#include <algorithm>
#include <map>
#include <fstream>
#include <ctime>

#include "Trade.h"
#include "ProfitCalculator.h"
#include "MultiHorizonEngine.h"
#include "MarketEntryCalculator.h"
#include "ExitStrategyCalculator.h"
#include "TradeDatabase.h"
#include "HttpApi.h"
#include "QuantMath.h"
#include "Simulator.h"

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
                  << "  qty=" << t.quantity;
        if (sold > 0.0)
            std::cout << "  sold=" << sold
                      << "  remaining=" << remaining;
        std::cout << '\n';

        // show executed child CoveredSell trades
        for (const auto& c : trades)
        {
            if (c.parentTradeId != t.tradeId) continue;
            double exitPnl = QuantMath::grossProfit(t.value, c.value, c.quantity);
            std::cout << "    -> #" << c.tradeId
                      << "  EXIT"
                      << "  price=" << c.value
                      << "  qty=" << c.quantity
                      << "  P&L=" << exitPnl
                      << " (" << QuantMath::pctGain(t.value, c.value) << "%)\n";
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
            double tpPerUnit = (bt->quantity > 0.0) ? lv.takeProfit / bt->quantity : 0.0;
            double slPerUnit = (bt->quantity > 0.0 && lv.stopLoss > 0.0) ? lv.stopLoss / bt->quantity : 0.0;
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
    std::cout << "  Trade-level stop-loss removed. Use exit strategies instead.\n";
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
        double disc = QuantMath::discount(cur, el.entryPrice);
        std::cout << "    [" << el.index << "]\n"
                  << "        entry price      = " << el.entryPrice
                  << "  (" << disc << "% below market)\n"
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
        ep.exitTakeProfit = QuantMath::breakEven(el.entryPrice, isShort ? -eo : eo);
        ep.exitStopLoss   = QuantMath::levelSL(el.entryPrice, eo, isShort);
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

            double cost = QuantMath::cost(levels[i].entryPrice, buyQty);
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
                double maxQty = QuantMath::fundedQty(levels[i].entryPrice, walBal - buyFee);
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
                cost   = QuantMath::cost(levels[i].entryPrice, buyQty);
            }

            int bid = db.executeBuy(sym, levels[i].entryPrice, buyQty);
            if (buyFee > 0.0)
                db.withdraw(buyFee);

            entryPoints[i].traded        = true;
            entryPoints[i].linkedTradeId = bid;

            // Create exit point for this trade
            if (entryPoints[i].exitTakeProfit > 0 || entryPoints[i].exitStopLoss > 0)
            {
                auto exits = db.loadExitPoints();
                TradeDatabase::ExitPoint xp;
                xp.exitId    = db.nextExitId();
                xp.tradeId   = bid;
                xp.symbol    = sym;
                xp.levelIndex = 0;
                xp.tpPrice   = entryPoints[i].exitTakeProfit;
                xp.slPrice   = entryPoints[i].exitStopLoss;
                xp.sellQty   = buyQty;
                xp.slActive  = (entryPoints[i].exitStopLoss > 0);
                exits.push_back(xp);
                db.saveExitPoints(exits);
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
            double pctGain = QuantMath::pctGain(tp->value, el.tpPrice);
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

        double tradeCost = QuantMath::cost(t.value, remaining);
        std::cout << "  #" << t.tradeId << "  " << t.symbol
                  << "  entry=" << t.value << "  qty=" << remaining
                  << "  cost=" << tradeCost
                  << "  market=" << cur << "\n";
        double buyFees  = readDouble("    Buy fee: ");
        double sellFees = readDouble("    Sell fee: ");

        auto r = ProfitCalculator::calculate(t, cur, buyFees, sellFees);

        std::cout << "    net=" << r.netProfit << "  ROI=" << r.roi << "%";
        std::cout << '\n';

        // horizon levels
        auto levels = db.loadHorizonLevels(t.symbol, t.tradeId);
        if (levels.empty()) continue;

        for (const auto& lv : levels)
        {
            double tpPrice = (t.quantity > 0.0) ? lv.takeProfit / t.quantity : 0.0;
            bool tpHit = (tpPrice > 0.0 && cur >= tpPrice);

            std::cout << "    [" << lv.index << "]  TP=" << lv.takeProfit
                      << " (" << tpPrice << "/unit)";
            if (tpHit)
            {
                double tpProfit = QuantMath::grossProfit(tpPrice, cur, t.quantity);
                std::cout << "  >> EXCEEDED  surplus=" << tpProfit;
            }
            else
            {
                std::cout << "  -- " << (tpPrice - cur) << " away";
            }

            if (lv.stopLoss != 0.0 && t.quantity > 0.0)
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
                int sid = db.executeSellForTrade(tr.symbol, tr.price, tr.qty, 0.0, tr.tradeId);
                if (sid >= 0)
                    std::cout << "    -> CoveredSell #" << sid
                              << "  " << tr.qty << " @ " << tr.price
                              << " (parent #" << tr.tradeId << ")\n";
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
            totalCost += QuantMath::cost(u.value, u.quantity);
            totalQty  += u.quantity;
            if (u.value < minPrice) minPrice = u.value;
            if (u.value > maxPrice) maxPrice = u.value;
            ++count;
        }

        double avgPrice = QuantMath::avgEntry(totalCost, totalQty);

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
    target->stopLossFraction = (slActive == 1) ? 1.0 : 0.0;
    target->stopLossActive = (target->stopLossFraction > 0.0);

    int createTrade = readInt("  Create a Buy trade for this entry? (1=yes, 0=no): ");
    if (createTrade == 1)
    {
        int tid = db.executeBuy(target->symbol, target->entryPrice, target->fundingQty);
        target->linkedTradeId = tid;

        // Create exit point for this trade
        if (target->exitTakeProfit > 0 || target->exitStopLoss > 0)
        {
            auto exits = db.loadExitPoints();
            TradeDatabase::ExitPoint xp;
            xp.exitId    = db.nextExitId();
            xp.tradeId   = tid;
            xp.symbol    = target->symbol;
            xp.levelIndex = 0;
            xp.tpPrice   = target->exitTakeProfit;
            xp.slPrice   = target->exitStopLoss;
            xp.sellQty   = target->fundingQty;
            xp.slActive  = target->stopLossActive;
            exits.push_back(xp);
            db.saveExitPoints(exits);
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

// ---- Serial generator ----

static void serialGenerator(TradeDatabase& db)
{
    std::string sym = readSymbol("  Symbol: ");
    double cur      = readDouble("  Current price: ");
    double qty      = readDouble("  Quantity: ");
    int    levels   = readInt("  Entry levels: ");
    if (levels < 1) levels = 1;
    double risk       = readDouble("  Risk (0-1): ");
    double steepness  = readDouble("  Steepness: ");
    int    isShortI   = readInt("  Direction (0=LONG, 1=SHORT): ");
    bool   isShort    = (isShortI == 1);
    int    fundMode   = readInt("  Funding (1=Pump only, 2=Pump+Wallet): ");

    HorizonParams p = readHorizonParams(nullptr);
    p.horizonCount = levels;
    p.generateStopLosses = (readInt("  Generate stop losses? (0=no, 1=yes): ") == 1);
    p.stopLossFraction = readDouble("  SL fraction (0-1): ");
    int dtCount = readInt("  Downtrend count (0=disabled): ");

    double walBal = db.loadWalletBalance();
    double availableFunds = p.portfolioPump;
    if (fundMode == 2) availableFunds += walBal;

    QuantMath::SerialParams sp;
    sp.currentPrice          = cur;
    sp.quantity              = qty;
    sp.levels                = levels;
    sp.steepness             = steepness;
    sp.risk                  = risk;
    sp.isShort               = isShort;
    sp.availableFunds        = availableFunds;
    sp.feeSpread             = p.feeSpread;
    sp.feeHedgingCoefficient = p.feeHedgingCoefficient;
    sp.deltaTime             = p.deltaTime;
    sp.symbolCount           = p.symbolCount;
    sp.coefficientK          = p.coefficientK;
    sp.surplusRate           = p.surplusRate;
    sp.maxRisk               = p.maxRisk;
    sp.minRisk               = p.minRisk;
    sp.generateStopLosses    = p.generateStopLosses;
    sp.stopLossFraction      = p.stopLossFraction;
    sp.downtrendCount        = dtCount;

    auto plan = QuantMath::generateSerialPlan(sp);

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "\n  Serial Plan: " << sym << " @ " << cur << "\n";
    std::cout << "  Overhead=" << (plan.overhead * 100) << "%"
              << "  Effective=" << (plan.effectiveOH * 100) << "%"
              << "  DT=" << plan.dtBuffer << "x"
              << "  SL=" << plan.slBuffer << "x"
              << "  Dir=" << (isShort ? "SHORT" : "LONG") << "\n\n";

    std::cout << "  Lvl  Entry          Discount  Qty            Cost             TP/unit          TP Total";
    if (p.generateStopLosses) std::cout << "         SL/unit";
    std::cout << "\n";

    for (const auto& e : plan.entries)
    {
        double cost = QuantMath::cost(e.entryPrice, e.fundQty);
        std::cout << "  " << std::setw(3) << e.index
                  << "  " << std::setw(14) << e.entryPrice
                  << "  " << std::setw(7) << e.discountPct << "%"
                  << "  " << std::setw(14) << e.fundQty
                  << "  " << std::setw(14) << cost
                  << "  " << std::setw(14) << e.tpUnit
                  << "  " << std::setw(14) << e.tpTotal;
        if (p.generateStopLosses)
            std::cout << "  " << std::setw(14) << e.slUnit;
        std::cout << "\n";
    }

    int save = readInt("\n  Save entries to DB? (1=yes, 0=no): ");
    if (save == 1)
    {
        if (p.portfolioPump > 0) db.deposit(p.portfolioPump);
        auto existing = db.loadEntryPoints();
        int nextId = db.nextEntryId();
        int saved = 0;
        for (const auto& e : plan.entries)
        {
            if (e.funding <= 0) continue;
            TradeDatabase::EntryPoint ep;
            ep.symbol = sym; ep.entryId = nextId++; ep.levelIndex = e.index;
            ep.entryPrice = e.entryPrice; ep.breakEven = e.breakEven;
            ep.funding = e.funding; ep.fundingQty = e.fundQty;
            ep.effectiveOverhead = plan.effectiveOH; ep.isShort = isShort;
            ep.exitTakeProfit = e.tpUnit; ep.exitStopLoss = e.slUnit;
            ep.traded = false; ep.linkedTradeId = -1;
            existing.push_back(ep);
            ++saved;
        }
        db.saveEntryPoints(existing);
        std::cout << "  -> " << saved << " entry point(s) saved.\n";
    }
}

// ---- Chain operations ----

static void chainMenu(TradeDatabase& db)
{
    std::cout << "  Chain Operations:\n"
              << "    1) Preview chain\n"
              << "    2) Save chain\n"
              << "    3) Chain status\n"
              << "    0) Back\n";
    int c = readInt("  > ");

    if (c == 1 || c == 2)
    {
        std::string sym = readSymbol("  Symbol: ");
        double cur  = readDouble("  Current price: ");
        double qty  = readDouble("  Quantity: ");
        int cycles  = readInt("  Chain cycles (1-10): ");
        if (cycles < 1) cycles = 1;
        if (cycles > 10) cycles = 10;

        double risk      = readDouble("  Risk (0-1): ");
        double steepness = readDouble("  Steepness: ");
        int    isShortI  = readInt("  Direction (0=LONG, 1=SHORT): ");
        bool   isShort   = (isShortI == 1);
        int    fundMode  = readInt("  Funding (1=Pump only, 2=Pump+Wallet): ");

        HorizonParams p = readHorizonParams(nullptr);
        p.horizonCount = readInt("  Levels per cycle: ");
        if (p.horizonCount < 1) p.horizonCount = 1;
        double savingsRate = readDouble("  Savings rate (e.g. 0.10): ");

        double walBal = db.loadWalletBalance();
        double availableFunds = p.portfolioPump;
        if (fundMode == 2) availableFunds += walBal;

        QuantMath::SerialParams sp;
        sp.currentPrice          = cur;
        sp.quantity              = qty;
        sp.levels                = p.horizonCount;
        sp.steepness             = steepness;
        sp.risk                  = risk;
        sp.isShort               = isShort;
        sp.availableFunds        = availableFunds;
        sp.feeSpread             = p.feeSpread;
        sp.feeHedgingCoefficient = p.feeHedgingCoefficient;
        sp.deltaTime             = p.deltaTime;
        sp.symbolCount           = p.symbolCount;
        sp.coefficientK          = p.coefficientK;
        sp.surplusRate           = p.surplusRate;
        sp.maxRisk               = p.maxRisk;
        sp.minRisk               = p.minRisk;
        sp.savingsRate           = savingsRate;

        auto chain = QuantMath::generateChain(sp, cycles);

        std::cout << std::fixed << std::setprecision(6);
        std::cout << "\n  Chain: " << sym << " @ " << cur
                  << "  (" << cycles << " cycles)\n\n";

        for (const auto& cc : chain.cycles)
        {
            std::cout << "  Cycle " << cc.cycle
                      << "  Capital=" << cc.capital
                      << "  Profit=" << cc.result.grossProfit
                      << "  Savings=" << cc.result.savingsAmount
                      << "  Next=" << cc.result.nextCycleFunds
                      << "  (" << cc.plan.entries.size() << " levels)\n";
        }

        if (c == 2)
        {
            db.saveChainMembers({});
            auto existingEps = db.loadEntryPoints();
            int totalEntries = 0;

            for (const auto& cc : chain.cycles)
            {
                std::vector<int> cycleEntryIds;
                for (const auto& e : cc.plan.entries)
                {
                    if (e.funding <= 0) continue;
                    TradeDatabase::EntryPoint ep;
                    ep.symbol = sym; ep.entryId = db.nextEntryId();
                    ep.levelIndex = e.index; ep.entryPrice = e.entryPrice;
                    ep.breakEven = e.breakEven; ep.funding = e.funding;
                    ep.fundingQty = e.fundQty; ep.effectiveOverhead = cc.plan.effectiveOH;
                    ep.isShort = isShort; ep.exitTakeProfit = e.tpUnit;
                    ep.exitStopLoss = sp.generateStopLosses ? e.slUnit : 0;
                    ep.traded = false; ep.linkedTradeId = -1;
                    existingEps.push_back(ep);
                    cycleEntryIds.push_back(ep.entryId);
                    totalEntries++;
                }
                db.addChainMembers(cc.cycle, cycleEntryIds);
            }
            db.saveEntryPoints(existingEps);

            TradeDatabase::ChainState state;
            state.symbol = sym; state.currentCycle = 0;
            state.totalSavings = 0; state.savingsRate = savingsRate;
            state.active = true;
            db.saveChainState(state);

            std::cout << "  -> Saved " << totalEntries << " entries across "
                      << cycles << " cycles.\n";
        }
    }
    else if (c == 3)
    {
        auto state = db.loadChainState();
        auto members = db.loadChainMembers();
        if (!state.active)
        {
            std::cout << "  (no active chain)\n";
            return;
        }
        std::cout << std::fixed << std::setprecision(6);
        std::cout << "  Chain: " << state.symbol
                  << "  Cycle=" << state.currentCycle
                  << "  Savings=" << state.totalSavings
                  << "  Rate=" << (state.savingsRate * 100) << "%"
                  << "  Active=" << (state.active ? "YES" : "NO") << "\n";

        // Count members per cycle
        std::map<int, int> cycleCount;
        for (const auto& m : members)
            cycleCount[m.cycle]++;
        for (const auto& [cy, cnt] : cycleCount)
            std::cout << "    Cycle " << cy << ": " << cnt << " entries\n";
    }
}

// ---- Simulator ----

static void runSimulator(TradeDatabase& db)
{
    SimConfig cfg;
    cfg.symbol          = readSymbol("  Symbol: ");
    cfg.startingCapital = readDouble("  Starting capital: ");

    int priceCount = readInt("  Number of price points: ");
    for (int i = 0; i < priceCount; ++i)
    {
        double p = readDouble("    Price " + std::to_string(i + 1) + ": ");
        cfg.prices.set(cfg.symbol, static_cast<long long>(i + 1), p);
    }

    cfg.horizonParams = readHorizonParams("  Entry levels: ");
    cfg.entryRisk     = readDouble("  Entry risk (0-1): ");
    cfg.entrySteepness = readDouble("  Entry steepness: ");
    cfg.exitRisk      = readDouble("  Exit risk (0-1): ");
    cfg.exitFraction  = readDouble("  Exit fraction (0-1): ");
    cfg.exitSteepness = readDouble("  Exit steepness: ");
    cfg.buyFeeRate    = readDouble("  Buy fee rate: ");
    cfg.sellFeeRate   = readDouble("  Sell fee rate: ");
    cfg.chainCycles   = (readInt("  Chain mode? (0=no, 1=yes): ") == 1);
    if (cfg.chainCycles)
        cfg.savingsRate = readDouble("  Savings rate: ");

    auto result = Simulator::run(cfg);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n  Simulation Results:\n";
    std::cout << "    Starting capital:  " << cfg.startingCapital << "\n";
    std::cout << "    Final capital:     " << result.finalCapital << "\n";
    std::cout << "    Total realized:    " << result.totalRealized << "\n";
    std::cout << "    Total fees:        " << result.totalFees << "\n";
    std::cout << "    Trades opened:     " << result.tradesOpened << "\n";
    std::cout << "    Trades closed:     " << result.tradesClosed << "\n";
    std::cout << "    Wins/Losses:       " << result.wins << "/" << result.losses << "\n";
    double winRate = (result.wins + result.losses > 0)
        ? static_cast<double>(result.wins) / (result.wins + result.losses) * 100 : 0;
    std::cout << "    Win rate:          " << winRate << "%\n";
    std::cout << "    Best trade:        " << result.bestTrade << "\n";
    std::cout << "    Worst trade:       " << result.worstTrade << "\n";
    std::cout << "    Fee hedging:       " << result.feeHedgingCoverage << "x\n";
    if (cfg.chainCycles)
    {
        std::cout << "    Cycles completed:  " << result.cyclesCompleted << "\n";
        std::cout << "    Total savings:     " << result.totalSavings << "\n";
    }
}

// ---- P&L ledger ----

static void viewPnlLedger(TradeDatabase& db)
{
    auto entries = db.loadPnl();
    if (entries.empty()) { std::cout << "  (no P&L entries)\n"; return; }

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  Realized P&L Ledger:\n";
    for (const auto& e : entries)
    {
        std::cout << "    " << e.symbol
                  << "  sell#" << e.sellTradeId
                  << "  parent#" << e.parentTradeId
                  << "  " << e.entryPrice << " -> " << e.sellPrice
                  << "  qty=" << e.quantity
                  << "  gross=" << e.grossProfit
                  << "  net=" << e.netProfit
                  << "  cum=" << e.cumProfit << "\n";
    }
    double totalNet = entries.back().cumProfit;
    std::cout << "    --------\n    Cumulative net P&L: " << totalNet << "\n";
}

// ---- Parameter models ----

static void paramModelsMenu(TradeDatabase& db)
{
    std::cout << "  Parameter Models:\n"
              << "    1) List models\n"
              << "    2) Save current as model\n"
              << "    3) Delete model\n"
              << "    0) Back\n";
    int c = readInt("  > ");

    if (c == 1)
    {
        auto models = db.loadParamModels();
        if (models.empty()) { std::cout << "  (no saved models)\n"; return; }
        std::cout << std::fixed << std::setprecision(4);
        for (const auto& m : models)
        {
            std::cout << "  [" << m.name << "]"
                      << "  levels=" << m.levels
                      << "  risk=" << m.risk
                      << "  steep=" << m.steepness
                      << "  spread=" << m.feeSpread
                      << "  surplus=" << m.surplusRate
                      << "  maxR=" << m.maxRisk
                      << "  minR=" << m.minRisk
                      << (m.isShort ? " SHORT" : " LONG")
                      << "\n";
        }
    }
    else if (c == 2)
    {
        std::string name = readString("  Model name: ");
        TradeDatabase::ParamModel m;
        m.name      = name;
        m.levels    = readInt("  Levels: ");
        m.risk      = readDouble("  Risk: ");
        m.steepness = readDouble("  Steepness: ");
        m.feeHedgingCoefficient = readDouble("  Fee hedging coefficient: ");
        m.portfolioPump         = readDouble("  Portfolio pump: ");
        m.symbolCount           = readInt("  Symbol count: ");
        m.coefficientK          = readDouble("  Coefficient K: ");
        m.feeSpread             = readDouble("  Fee spread: ");
        m.deltaTime             = readDouble("  Delta time: ");
        m.surplusRate           = readDouble("  Surplus rate: ");
        m.maxRisk               = readDouble("  Max risk: ");
        m.minRisk               = readDouble("  Min risk: ");
        m.isShort               = (readInt("  Short? (0/1): ") == 1);
        m.fundMode              = readInt("  Fund mode (1/2): ");
        m.generateStopLosses    = (readInt("  Stop losses? (0/1): ") == 1);
        m.stopLossFraction      = readDouble("  SL fraction: ");
        db.addParamModel(m);
        std::cout << "  -> Saved model '" << name << "'.\n";
    }
    else if (c == 3)
    {
        auto models = db.loadParamModels();
        for (const auto& m : models)
            std::cout << "    " << m.name << "\n";
        std::string name = readString("  Model name to delete: ");
        db.removeParamModel(name);
        std::cout << "  -> Deleted model '" << name << "'.\n";
    }
}

// ---- Execute buy/sell (wallet-integrated) ----

static void executeBuySell(TradeDatabase& db)
{
    std::cout << std::fixed << std::setprecision(2);
    double walBal = db.loadWalletBalance();
    std::cout << "  Wallet balance: " << walBal << "\n\n";

    int action = readInt("  1=Buy  2=Sell  0=Back: ");
    if (action == 1)
    {
        auto sym    = readSymbol("  Symbol: ");
        double price = readDouble("  Price: ");
        double qty   = readDouble("  Quantity: ");
        double fee   = readDouble("  Buy fee: ");

        double cost = QuantMath::cost(price, qty) + fee;
        if (cost > walBal)
        {
            std::cout << "  Insufficient funds (need " << cost << ", have " << walBal << ").\n";
            return;
        }

        int tid = db.executeBuy(sym, price, qty, fee);
        std::cout << "  -> Buy #" << tid << "  " << qty << " @ " << price
                  << "  fee=" << fee << "  cost=" << cost
                  << "  balance=" << db.loadWalletBalance() << "\n";
        std::cout << "  Add exit strategies via menu option 31 or the web UI.\n";
    }
    else if (action == 2)
    {
        auto sym     = readSymbol("  Symbol: ");
        double price  = readDouble("  Sell price: ");
        double qty    = readDouble("  Quantity: ");
        double fee    = readDouble("  Sell fee: ");

        double holdings = db.holdingsForSymbol(sym);
        if (qty > holdings + 1e-9)
        {
            std::cout << "  Insufficient holdings (have " << holdings << ").\n";
            return;
        }

        int sid = db.executeSell(sym, price, qty, fee);
        if (sid >= 0)
            std::cout << "  -> Sell #" << sid << "  " << qty << " @ " << price
                      << "  fee=" << fee << "  balance=" << db.loadWalletBalance() << "\n";
        else
            std::cout << "  -> Sell failed.\n";
    }
}

// ---- Add exit strategy ----

static void setTpSl(TradeDatabase& db)
{
    listTrades(db);
    int id = readInt("  Trade ID: ");

    auto trades = db.loadTrades();
    auto* tp = db.findTradeById(trades, id);
    if (!tp) { std::cout << "  Trade not found.\n"; return; }
    if (tp->type != TradeType::Buy) { std::cout << "  Only Buy trades support exit strategies.\n"; return; }

    double remaining = tp->quantity - db.soldQuantityForParent(tp->tradeId);
    if (remaining <= 0) { std::cout << "  No remaining quantity.\n"; return; }

    // Show existing exit points
    auto exits = db.loadExitPoints();
    int existingCount = 0;
    for (const auto& xp : exits)
        if (xp.tradeId == id && !xp.executed) ++existingCount;
    if (existingCount > 0)
    {
        std::cout << "  Existing exit points:\n";
        for (const auto& xp : exits)
        {
            if (xp.tradeId != id) continue;
            std::cout << "    X[" << xp.levelIndex << "]  TP=" << xp.tpPrice
                      << "  SL=" << xp.slPrice << "  qty=" << xp.sellQty
                      << "  " << (xp.executed ? "DONE" : (xp.slActive ? "SL:ON" : "SL:OFF")) << "\n";
        }
    }

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  Remaining qty: " << remaining << "\n";
    double tpPrice = readDouble("  TP price (per unit): ");
    double slPrice = readDouble("  SL price (per unit, 0=none): ");
    double sellQty = readDouble("  Sell quantity (0=all remaining): ");
    if (sellQty <= 0) sellQty = remaining;
    int slOn = (slPrice > 0) ? readInt("  Activate SL? (1=yes, 0=no): ") : 0;

    // Determine next level index
    int maxIdx = -1;
    for (const auto& xp : exits)
        if (xp.tradeId == id && xp.levelIndex > maxIdx) maxIdx = xp.levelIndex;

    TradeDatabase::ExitPoint xp;
    xp.exitId    = db.nextExitId();
    xp.tradeId   = id;
    xp.symbol    = tp->symbol;
    xp.levelIndex = maxIdx + 1;
    xp.tpPrice   = tpPrice;
    xp.slPrice   = slPrice;
    xp.sellQty   = sellQty;
    xp.slActive  = (slOn == 1);
    exits.push_back(xp);
    db.saveExitPoints(exits);

    std::cout << "  -> Exit X[" << xp.levelIndex << "] added: TP=" << tpPrice
              << " SL=" << slPrice << " qty=" << sellQty << "\n";
}

// ---- Deallocate holdings ----

static void deallocateHoldings(TradeDatabase& db)
{
    auto trades = db.loadTrades();
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  Buy trades with remaining holdings:\n";
    bool any = false;
    for (const auto& t : trades)
    {
        if (t.type != TradeType::Buy) continue;
        double sold = db.soldQuantityForParent(t.tradeId);
        double released = db.releasedForTrade(t.tradeId);
        double allocated = t.quantity - sold - released;
        if (allocated <= 0) continue;
        any = true;
        std::cout << "    #" << t.tradeId << "  " << t.symbol
                  << "  total=" << t.quantity
                  << "  sold=" << sold
                  << "  released=" << released
                  << "  allocated=" << allocated << "\n";
    }
    if (!any) { std::cout << "  (no allocated holdings)\n"; return; }

    int id = readInt("  Trade ID to deallocate from: ");
    double qty = readDouble("  Quantity to release: ");

    if (db.releaseFromTrade(id, qty))
        std::cout << "  -> Released " << qty << " from trade #" << id << ".\n";
    else
        std::cout << "  -> Failed (invalid trade or qty exceeds allocated).\n";
}

// ---- Chain advance ----

static void chainAdvance(TradeDatabase& db)
{
    auto state = db.loadChainState();
    if (!state.active)
    {
        std::cout << "  (no active chain)\n";
        return;
    }

    auto members = db.loadChainMembers();
    auto allEntries = db.loadEntryPoints();
    auto allTrades = db.loadTrades();

    bool anyTraded = false;
    bool allSold = true;
    double cyclePnl = 0;

    for (const auto& m : members)
    {
        if (m.cycle != state.currentCycle) continue;
        for (const auto& e : allEntries)
        {
            if (e.entryId != m.entryId) continue;
            if (!e.traded || e.linkedTradeId < 0) continue;
            anyTraded = true;
            for (const auto& t : allTrades)
            {
                if (t.tradeId == e.linkedTradeId && t.type == TradeType::Buy)
                {
                    double sq = db.soldQuantityForParent(t.tradeId);
                    if (sq < t.quantity - 1e-9) allSold = false;
                    for (const auto& s : allTrades)
                        if (s.type == TradeType::CoveredSell && s.parentTradeId == t.tradeId)
                            cyclePnl += QuantMath::netProfit(
                                QuantMath::grossProfit(t.value, s.value, s.quantity),
                                0.0, s.sellFee);
                    break;
                }
            }
            break;
        }
    }

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  Chain: " << state.symbol
              << "  Cycle=" << state.currentCycle
              << "  P&L this cycle=" << cyclePnl << "\n";

    if (!anyTraded || !allSold)
    {
        std::cout << "  Cycle not complete (traded=" << anyTraded
                  << " allSold=" << allSold << ").\n";
        return;
    }

    // Divert savings
    double savingsDiv = (cyclePnl > 0 && state.savingsRate > 0) ? cyclePnl * state.savingsRate : 0;
    if (savingsDiv > 0)
    {
        db.withdraw(savingsDiv);
        state.totalSavings += savingsDiv;
        std::cout << "  Savings diverted: " << savingsDiv << "\n";
    }

    double curPrice = readDouble("  Current price for next cycle entries: ");
    double qty      = readDouble("  Quantity: ");
    int    levels   = readInt("  Levels: ");
    double risk     = readDouble("  Risk (0-1): ");
    double steepness = readDouble("  Steepness: ");

    HorizonParams p = readHorizonParams(nullptr);

    QuantMath::SerialParams sp;
    sp.currentPrice          = curPrice;
    sp.quantity              = qty;
    sp.levels                = levels;
    sp.steepness             = steepness;
    sp.risk                  = risk;
    sp.availableFunds        = db.loadWalletBalance();
    sp.feeSpread             = p.feeSpread;
    sp.feeHedgingCoefficient = p.feeHedgingCoefficient;
    sp.deltaTime             = p.deltaTime;
    sp.symbolCount           = p.symbolCount;
    sp.coefficientK          = p.coefficientK;
    sp.surplusRate           = p.surplusRate;
    sp.maxRisk               = p.maxRisk;
    sp.minRisk               = p.minRisk;

    auto plan = QuantMath::generateSerialPlan(sp);

    int nextCycle = state.currentCycle + 1;
    std::vector<int> newEntryIds;
    auto existingEps = db.loadEntryPoints();

    for (const auto& e : plan.entries)
    {
        if (e.funding <= 0) continue;
        TradeDatabase::EntryPoint ep;
        ep.symbol = state.symbol;
        ep.entryId = db.nextEntryId();
        ep.levelIndex = e.index;
        ep.entryPrice = e.entryPrice;
        ep.breakEven = e.breakEven;
        ep.funding = e.funding;
        ep.fundingQty = e.fundQty;
        ep.effectiveOverhead = plan.effectiveOH;
        ep.exitTakeProfit = e.tpUnit;
        ep.exitStopLoss = e.slUnit;
        ep.traded = false;
        ep.linkedTradeId = -1;
        existingEps.push_back(ep);
        newEntryIds.push_back(ep.entryId);
    }

    db.saveEntryPoints(existingEps);
    db.addChainMembers(nextCycle, newEntryIds);
    state.currentCycle = nextCycle;
    db.saveChainState(state);

    std::cout << "  -> Advanced to cycle " << nextCycle
              << " with " << newEntryIds.size() << " new entries.\n";
}

// ---- Chain reset ----

static void chainReset(TradeDatabase& db)
{
    auto state = db.loadChainState();
    if (!state.active) { std::cout << "  (no active chain to reset)\n"; return; }

    int c = readInt("  Reset active chain? (1=yes, 0=no): ");
    if (c == 1)
    {
        TradeDatabase::ChainState empty;
        db.saveChainState(empty);
        db.saveChainMembers({});
        std::cout << "  -> Chain reset.\n";
    }
}

// ---- P&L backfill ----

static void pnlBackfill(TradeDatabase& db)
{
    auto trades = db.loadTrades();
    int filled = 0;
    for (const auto& t : trades)
    {
        if (t.type != TradeType::CoveredSell || t.parentTradeId < 0) continue;
        const Trade* parent = nullptr;
        for (const auto& p : trades)
            if (p.tradeId == t.parentTradeId) { parent = &p; break; }
        if (!parent) continue;
        double gp = (t.value - parent->value) * t.quantity;
        double np = gp - t.sellFee;
        db.recordPnl(t.symbol, t.tradeId, t.parentTradeId,
                     parent->value, t.value, t.quantity, gp, np);
        ++filled;
    }
    std::cout << "  -> Backfilled " << filled << " P&L entries.\n";
}

// ---- Execute triggered entries ----

static void executeTriggeredEntries(TradeDatabase& db)
{
    auto points = db.loadEntryPoints();
    std::cout << std::fixed << std::setprecision(2);

    // group by symbol to ask prices
    std::map<std::string, double> symPrices;
    for (const auto& ep : points)
    {
        if (ep.traded) continue;
        if (symPrices.find(ep.symbol) == symPrices.end())
            symPrices[ep.symbol] = readDouble(("  Current price for " + ep.symbol + ": ").c_str());
    }

    // find triggered entries
    std::vector<TradeDatabase::EntryPoint*> triggered;
    for (auto& ep : points)
    {
        if (ep.traded) continue;
        auto it = symPrices.find(ep.symbol);
        if (it == symPrices.end()) continue;
        double cur = it->second;

        bool hit = ep.isShort ? (cur >= ep.entryPrice) : (cur <= ep.entryPrice);
        if (hit) triggered.push_back(&ep);
    }

    if (triggered.empty()) { std::cout << "  (no entries triggered)\n"; return; }

    std::cout << "  Triggered entries:\n";
    for (const auto* ep : triggered)
    {
        double cur = symPrices[ep->symbol];
        std::cout << "    [" << ep->entryId << "]  " << ep->symbol
                  << "  entry=" << ep->entryPrice
                  << "  market=" << cur
                  << "  qty=" << ep->fundingQty
                  << (ep->isShort ? " SHORT" : " LONG") << "\n";
    }

    int exec = readInt("  Execute all? (1=yes, 0=no): ");
    if (exec != 1) return;

    int done = 0;
    for (auto* ep : triggered)
    {
        double fee = readDouble(("  Buy fee for [" + std::to_string(ep->entryId) + "]: ").c_str());
        double cost = ep->entryPrice * ep->fundingQty + fee;
        double walBal = db.loadWalletBalance();
        if (cost > walBal)
        {
            std::cout << "    [" << ep->entryId << "] SKIPPED (need "
                      << cost << ", have " << walBal << ")\n";
            continue;
        }
        int bid = db.executeBuy(ep->symbol, ep->entryPrice, ep->fundingQty, fee);
        ep->traded = true;
        ep->linkedTradeId = bid;

        // Create exit point for this trade
        if (ep->exitTakeProfit > 0 || ep->exitStopLoss > 0)
        {
            auto exits = db.loadExitPoints();
            TradeDatabase::ExitPoint xp;
            xp.exitId    = db.nextExitId();
            xp.tradeId   = bid;
            xp.symbol    = ep->symbol;
            xp.levelIndex = 0;
            xp.tpPrice   = ep->exitTakeProfit;
            xp.slPrice   = ep->exitStopLoss;
            xp.sellQty   = ep->fundingQty;
            xp.slActive  = (ep->exitStopLoss > 0);
            exits.push_back(xp);
            db.saveExitPoints(exits);
        }
        std::cout << "    -> Buy #" << bid << "  " << ep->fundingQty
                  << " @ " << ep->entryPrice << "\n";
        ++done;
    }
    db.saveEntryPoints(points);
    std::cout << "  -> Executed " << done << " entries.\n";
}

// ---- hledger export ----

static void exportHledger(TradeDatabase& db)
{
    auto sym   = readSymbol("  Symbol: ");
    auto points = db.loadEntryPoints();

    std::vector<const TradeDatabase::EntryPoint*> matching;
    for (const auto& ep : points)
        if (ep.symbol == sym) matching.push_back(&ep);

    if (matching.empty()) { std::cout << "  (no entries for " << sym << ")\n"; return; }

    auto path = readString("  Output file path (e.g. journal.txt): ");
    std::ofstream f(path, std::ios::trunc);
    if (!f) { std::cout << "  Cannot open " << path << "\n"; return; }

    f << std::fixed << std::setprecision(8);
    f << "; hledger journal for " << sym << "\n\n";

    for (const auto* ep : matching)
    {
        double cost = ep->entryPrice * ep->fundingQty;
        f << "; Entry [" << ep->entryId << "] lvl=" << ep->levelIndex
          << (ep->isShort ? " SHORT" : " LONG") << "\n";

        if (ep->isShort)
        {
            f << "~  " << sym << " short entry\n"
              << "    liabilities:short:" << sym << "    "
              << ep->fundingQty << " " << sym << " @ " << ep->entryPrice << "\n"
              << "    assets:exchange                     " << cost << "\n\n";
        }
        else
        {
            f << "~  " << sym << " buy entry\n"
              << "    assets:exchange:" << sym << "       "
              << ep->fundingQty << " " << sym << " @ " << ep->entryPrice << "\n"
              << "    assets:exchange                     " << (-cost) << "\n\n";
        }

        if (ep->exitTakeProfit > 0)
        {
            double tpVal = ep->exitTakeProfit * ep->fundingQty;
            double tpProfit = tpVal - cost;
            f << "~  " << sym << " take-profit\n"
              << "    assets:exchange                      " << tpVal << "\n"
              << "    assets:exchange:" << sym << "       "
              << (-static_cast<double>(ep->fundingQty)) << " " << sym << "\n"
              << "    income:trading:" << sym << "         " << (-tpProfit) << "\n\n";
        }
    }
    std::cout << "  -> Exported " << matching.size() << " entries to " << path << "\n";
}

// ---- Price series ----

static void priceSeriesMenu(TradeDatabase& db)
{
    PriceSeries ps;
    db.seedPriceSeries(ps);

    std::cout << "  Price Series:\n"
              << "    1) View prices\n"
              << "    2) Record price\n"
              << "    0) Back\n";
    int c = readInt("  > ");

    if (c == 1)
    {
        auto sym = readSymbol("  Symbol (or * for all): ");
        const auto& data = ps.data();
        if (sym == "*")
        {
            for (const auto& [s, pts] : data)
            {
                std::cout << "  " << s << ": " << pts.size() << " points\n";
                int show = std::min(static_cast<int>(pts.size()), 10);
                for (int i = static_cast<int>(pts.size()) - show; i < static_cast<int>(pts.size()); ++i)
                    std::cout << "    ts=" << pts[i].timestamp << "  " << pts[i].price << "\n";
            }
        }
        else
        {
            auto it = data.find(sym);
            if (it == data.end()) { std::cout << "  (no data for " << sym << ")\n"; return; }
            std::cout << std::fixed << std::setprecision(2);
            for (const auto& pt : it->second)
                std::cout << "    ts=" << pt.timestamp << "  " << pt.price << "\n";
        }
    }
    else if (c == 2)
    {
        auto sym   = readSymbol("  Symbol: ");
        double price = readDouble("  Price: ");
        long long ts = static_cast<long long>(std::time(nullptr));

        // Record as a zero-qty trade to seed the price series
        Trade t;
        t.tradeId  = db.nextTradeId();
        t.symbol   = sym;
        t.type     = TradeType::Buy;
        t.value    = price;
        t.quantity = 0;
        t.timestamp = ts;
        db.addTrade(t);
        std::cout << "  -> Price " << price << " recorded for " << sym << ".\n";
    }
}

// ---- Main ----

int main()
{
    TradeDatabase db("db");
    std::mutex dbMutex;

    // Start HTTP API on a background thread
    int httpPort = 8080;
    std::thread httpThread([&]() {
        try {
            startHttpApi(db, httpPort, dbMutex);
        } catch (const std::exception& e) {
            std::cerr << "  [HTTP] FATAL: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "  [HTTP] FATAL: unknown exception" << std::endl;
        }
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
        std::cout << "  25) Serial generator\n";
        std::cout << "  26) Chain operations\n";
        std::cout << "  27) Simulator\n";
        std::cout << "  28) P&L ledger\n";
        std::cout << "  29) Parameter models\n";
        std::cout << "  30) Execute buy/sell\n";
        std::cout << "  31) Add exit strategy\n";
        std::cout << "  32) Deallocate holdings\n";
        std::cout << "  33) Chain advance\n";
        std::cout << "  34) Chain reset\n";
        std::cout << "  35) P&L backfill\n";
        std::cout << "  36) Execute triggered entries\n";
        std::cout << "  37) Export hledger\n";
        std::cout << "  38) Price series\n";
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
        case 25: serialGenerator(db);    break;
        case 26: chainMenu(db);          break;
        case 27: runSimulator(db);       break;
        case 28: viewPnlLedger(db);      break;
        case 29: paramModelsMenu(db);    break;
        case 30: executeBuySell(db);      break;
        case 31: setTpSl(db);             break;
        case 32: deallocateHoldings(db);   break;
        case 33: chainAdvance(db);         break;
        case 34: chainReset(db);           break;
        case 35: pnlBackfill(db);          break;
        case 36: executeTriggeredEntries(db); break;
        case 37: exportHledger(db);        break;
        case 38: priceSeriesMenu(db);      break;
        case 0:  running = false;        break;
        default: std::cout << "  Invalid choice.\n"; break;
        }
    }

    return 0;

}
