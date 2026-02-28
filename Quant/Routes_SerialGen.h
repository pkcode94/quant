#pragma once

#include "AppContext.h"
#include "HtmlHelpers.h"
#include "QuantMath.h"
#include <mutex>
#include <cmath>
#include <limits>

inline void registerSerialGenRoutes(httplib::Server& svr, AppContext& ctx)
{
    auto& db = ctx.defaultDb;
    auto& dbMutex = ctx.dbMutex;

    // ========== GET /serial-generator ==========
    svr.Get("/serial-generator", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        std::ostringstream h;
        h << std::fixed << std::setprecision(17);
        h << html::msgBanner(req) << html::errBanner(req);
        double walBal = db.loadWalletBalance();
        h << "<h1>Serial Generator</h1>"
             "<div style='color:#64748b;font-size:0.82em;margin-bottom:12px;'>Generate a full series of entry + horizon + exit tuples</div>"
             "<div class='row'><div class='stat'><div class='lbl'>Wallet</div><div class='val'>" << walBal << "</div></div></div><br>"
             "<form class='card' method='POST' action='/serial-generator'><h3>Parameters</h3>"
             "<label>Symbol</label><input type='text' name='symbol' required><br>"
             "<label>Current Price</label><input type='number' name='currentPrice' step='any' required><br>"
             "<label>Quantity</label><input type='number' name='quantity' step='any' required><br>"
             "<label>Entry Levels</label><input type='number' name='levels' value='4'><br>"
             "<label>Risk</label><input type='number' name='risk' step='any' value='0.5'><br>"
             "<label>Steepness</label><input type='number' name='steepness' step='any' value='6'><br>"
             "<label>Fee Hedging</label><input type='number' name='feeHedgingCoefficient' step='any' value='1'><br>"
             "<label>Pump</label><input type='number' name='portfolioPump' step='any' value='0'><br>"
             "<label>Symbol Count</label><input type='number' name='symbolCount' value='1'><br>"
             "<label>Coefficient K</label><input type='number' name='coefficientK' step='any' value='0'><br>"
             "<label>Fee Spread</label><input type='number' name='feeSpread' step='any' value='0'><br>"
             "<label>Delta Time</label><input type='number' name='deltaTime' step='any' value='1'><br>"
             "<label>Surplus Rate</label><input type='number' name='surplusRate' step='any' value='0.02'><br>"
             "<label>Max Risk</label><input type='number' name='maxRisk' step='any' value='0'><br>"
             "<label>Min Risk</label><input type='number' name='minRisk' step='any' value='0'><br>"
             "<label>Direction</label><select name='isShort'><option value='0'>LONG</option><option value='1'>SHORT</option></select><br>"
             "<label>Funding</label><select name='fundMode'><option value='1'>Pump only</option><option value='2'>Pump + Wallet</option></select><br>"
             "<label>Stop Losses</label><select name='generateStopLosses'><option value='0'>No</option><option value='1'>Yes</option></select><br>"
             "<label>SL Fraction</label><input type='number' name='stopLossFraction' step='any' value='1' title='Fraction of position to sell at SL (0-1, 1 = full exit)'><br>"
             "<label>Range Above</label><input type='number' name='rangeAbove' step='any' value='0'><br>"
             "<label>Range Below</label><input type='number' name='rangeBelow' step='any' value='0'><br>"
             "<label>DT Count</label><input type='number' name='downtrendCount' value='1' title='Number of future downturn cycles to pre-fund (0 = disabled)'><br>"
             "<label>SL Hedge Count</label><input type='number' name='stopLossHedgeCount' value='0' title='Future SL hits to pre-fund via TP inflation (0 = disabled)'><br>"
             "<label>Future Trade Fees</label><input type='number' name='futureTradeCount' value='0' title='Future chain trades whose fees this TP must cover (0 = self only)'><br>"
             "<button>Generate Series</button></form>";
        res.set_content(html::wrap("Serial Generator", h.str()), "text/html");
    });

    // ========== POST /serial-generator ==========
    svr.Post("/serial-generator", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        std::string sym = normalizeSymbol(fv(f, "symbol"));
        double cur = fd(f, "currentPrice");
        double qty = fd(f, "quantity");
        double risk = fd(f, "risk");
        double steepness = fd(f, "steepness", 6.0);
        bool isShort = (fv(f, "isShort") == "1");
        int fundMode = fi(f, "fundMode", 1);
        bool genSL = (fv(f, "generateStopLosses") == "1");
        double rangeAbove = fd(f, "rangeAbove");
        double rangeBelow = fd(f, "rangeBelow");
        int downtrendCount = fi(f, "downtrendCount", 1);
        HorizonParams p;
        p.horizonCount = fi(f, "levels", 4);
        p.feeHedgingCoefficient = fd(f, "feeHedgingCoefficient", 1.0);
        p.portfolioPump = fd(f, "portfolioPump");
        p.symbolCount = fi(f, "symbolCount", 1);
        p.coefficientK = fd(f, "coefficientK");
        p.feeSpread = fd(f, "feeSpread");
        p.deltaTime = fd(f, "deltaTime", 1.0);
        p.surplusRate = fd(f, "surplusRate");
        p.maxRisk = fd(f, "maxRisk");
        p.minRisk = fd(f, "minRisk");
        p.generateStopLosses = genSL;
        p.futureTradeCount = fi(f, "futureTradeCount", 0);
        p.stopLossFraction = fd(f, "stopLossFraction", 1.0);
        p.stopLossHedgeCount = fi(f, "stopLossHedgeCount", 0);
        double walBal = db.loadWalletBalance();
        double availableFunds = p.portfolioPump;
        if (fundMode == 2) availableFunds += walBal;
        std::ostringstream h;
        h << std::fixed << std::setprecision(17);
        if (sym.empty() || cur <= 0 || qty <= 0)
        { h << "<div class='msg err'>Symbol, price, and quantity are required</div><br><a class='btn' href='/serial-generator'>Back</a>";
          res.set_content(html::wrap("Serial Generator", h.str()), "text/html"); return; }

        // ?? Compute plan via QuantMath ??
        QuantMath::SerialParams sp;
        sp.currentPrice          = cur;
        sp.quantity              = qty;
        sp.levels                = p.horizonCount;
        sp.steepness             = steepness;
        sp.risk                  = risk;
        sp.isShort               = isShort;
        sp.availableFunds        = availableFunds;
        sp.rangeAbove            = rangeAbove;
        sp.rangeBelow            = rangeBelow;
        sp.feeSpread             = p.feeSpread;
        sp.feeHedgingCoefficient = p.feeHedgingCoefficient;
        sp.deltaTime             = p.deltaTime;
        sp.symbolCount           = p.symbolCount;
        sp.coefficientK          = p.coefficientK;
        sp.surplusRate           = p.surplusRate;
        sp.futureTradeCount      = p.futureTradeCount;
        sp.maxRisk               = p.maxRisk;
        sp.minRisk               = p.minRisk;
        sp.generateStopLosses    = genSL;
        sp.stopLossFraction      = p.stopLossFraction;
        sp.stopLossHedgeCount    = p.stopLossHedgeCount;
        sp.downtrendCount        = downtrendCount;

        auto plan = QuantMath::generateSerialPlan(sp);
        const auto& entries = plan.entries;
        double eo = plan.effectiveOH;
        double overhead = plan.overhead;
        double dtBuffer = plan.dtBuffer;
        double slBuffer = plan.slBuffer;
        double slFrac   = plan.slFraction;
        double totalSlLoss = plan.totalSlLoss;

        h << "<h1>Serial Plan: " << html::esc(sym) << " @ " << cur << "</h1>";
        h << "<div class='row'>"
             "<div class='stat'><div class='lbl'>Overhead</div><div class='val'>" << (overhead * 100) << "%</div></div>"
             "<div class='stat'><div class='lbl'>Effective</div><div class='val'>" << (eo * 100) << "%</div></div>"
             "<div class='stat'><div class='lbl'>Surplus</div><div class='val'>" << (p.surplusRate * 100) << "%</div></div>"
             "<div class='stat'><div class='lbl'>Direction</div><div class='val'>" << (isShort ? "SHORT" : "LONG") << "</div></div>"
             "<div class='stat'><div class='lbl'>Available</div><div class='val'>" << availableFunds << "</div></div>"
             "<div class='stat'><div class='lbl'>Steepness</div><div class='val'>" << steepness << "</div></div>"
             "<div class='stat'><div class='lbl'>DT Buffer</div><div class='val'>" << dtBuffer << "x</div></div>"
             "<div class='stat'><div class='lbl'>SL Buffer</div><div class='val'>" << slBuffer << "x</div></div>"
             "<div class='stat'><div class='lbl'>SL Frac</div><div class='val'>" << (slFrac * 100) << "%</div></div>"
             "<div class='stat'><div class='lbl'>Future Fees</div><div class='val'>" << p.futureTradeCount << "</div></div>";
        if (genSL)
             h << "<div class='stat'><div class='lbl'>Max SL Loss</div><div class='val sell'>" << totalSlLoss << "</div></div>"
                  "<div class='stat'><div class='lbl'>SL/Capital</div><div class='val" << (totalSlLoss > availableFunds ? " sell" : "") << "'>" << (availableFunds > 0 ? (totalSlLoss / availableFunds * 100) : 0) << "%</div></div>";
        h << "</div>";
        h << "<h2>Entry + Exit Tuples</h2><table><tr><th>Lvl</th><th>Entry</th><th>Discount</th><th>Qty</th><th>Cost</th>"
             "<th>Break Even</th><th>TP/unit</th><th>TP Total</th><th>TP Gross</th>";
        if (genSL) h << "<th>SL/unit</th><th>SL Qty</th><th>SL Total</th><th>SL Loss</th>";
        h << "</tr>";
        for (const auto& e : entries)
        {
            double entryCost = QuantMath::cost(e.entryPrice, e.fundQty);
            h << "<tr><td>" << e.index << "</td><td>" << e.entryPrice << "</td><td>" << e.discountPct << "%</td>"
              << "<td>" << e.fundQty << "</td><td>" << entryCost << "</td><td>" << e.breakEven << "</td>"
              << "<td class='buy'>" << e.tpUnit << "</td><td class='buy'>" << e.tpTotal << "</td><td class='buy'>" << e.tpGross << "</td>";
            if (genSL)
                h << "<td class='sell'>" << e.slUnit << "</td><td class='sell'>" << e.slQty << "</td><td class='sell'>" << e.slTotal << "</td><td class='sell'>" << e.slLoss << "</td>";
            h << "</tr>";
        }
        h << "</table>";
        h << "<h2>Save Series</h2><form class='card' method='POST' action='/save-serial'>"
             "<h3>Save all entries as pending (buy fees entered when price hits)</h3>"
             "<input type='hidden' name='symbol' value='" << html::esc(sym) << "'>"
             "<input type='hidden' name='isShort' value='" << (isShort ? "1" : "0") << "'>"
             "<input type='hidden' name='pump' value='" << p.portfolioPump << "'>"
             "<input type='hidden' name='entryCount' value='" << entries.size() << "'>";
        for (const auto& e : entries)
        {
            if (e.funding <= 0) continue;
            h << "<input type='hidden' name='ep_" << e.index << "' value='" << e.entryPrice << "'>"
              << "<input type='hidden' name='eq_" << e.index << "' value='" << e.fundQty << "'>"
              << "<input type='hidden' name='eb_" << e.index << "' value='" << e.breakEven << "'>"
              << "<input type='hidden' name='ef_" << e.index << "' value='" << e.funding << "'>"
              << "<input type='hidden' name='eov_" << e.index << "' value='" << eo << "'>"
              << "<input type='hidden' name='etp_" << e.index << "' value='" << e.tpUnit << "'>"
              << "<input type='hidden' name='esl_" << e.index << "' value='" << e.slUnit << "'>";
        }
        h << "<button>Save Entry Points</button></form>";
        h << "<h2>Export to hledger</h2><form class='card' method='POST' action='/export-hledger'>"
             "<h3>Generate hledger journal entries for this cycle</h3>"
             "<input type='hidden' name='symbol' value='" << html::esc(sym) << "'>"
             "<input type='hidden' name='feeSpread' value='" << p.feeSpread << "'>"
             "<input type='hidden' name='surplusRate' value='" << p.surplusRate << "'>"
             "<input type='hidden' name='eo' value='" << eo << "'>"
             "<input type='hidden' name='entryCount' value='" << entries.size() << "'>";
        for (const auto& e : entries)
        {
            if (e.funding <= 0) continue;
            h << "<input type='hidden' name='ep_" << e.index << "' value='" << e.entryPrice << "'>"
              << "<input type='hidden' name='eq_" << e.index << "' value='" << e.fundQty << "'>"
              << "<input type='hidden' name='ef_" << e.index << "' value='" << e.funding << "'>"
              << "<input type='hidden' name='etp_" << e.index << "' value='" << e.tpUnit << "'>";
        }
        h << "<label>Date</label><input type='date' name='journalDate' value='" << html::today() << "'><br>"
             "<label>Savings Rate</label><input type='number' name='savingsRate' step='any' value='0.10'><br>"
             "<button>Export Journal</button></form>";
        h << "<br><a class='btn' href='/serial-generator'>Back</a>";
        res.set_content(html::wrap("Serial Plan", h.str()), "text/html");
    });

    // ========== POST /export-hledger ==========
    svr.Post("/export-hledger", [&](const httplib::Request& req, httplib::Response& res) {
        auto f = parseForm(req.body);
        std::string sym = normalizeSymbol(fv(f, "symbol"));
        double feeSpread = fd(f, "feeSpread");
        double surplusRate = fd(f, "surplusRate");
        double eo = fd(f, "eo");
        int count = fi(f, "entryCount");
        std::string jdate = fv(f, "journalDate");
        double savingsRate = fd(f, "savingsRate", 0.10);
        if (jdate.empty()) jdate = html::today();
        if (sym.empty() || count <= 0) { res.set_redirect("/serial-generator?err=Invalid+parameters", 303); return; }

        struct JEntry { double price; double qty; double funding; double tp; };
        std::vector<JEntry> jentries;
        for (int i = 0; i < count; ++i)
        {
            std::string si = std::to_string(i);
            double ep = fd(f, "ep_" + si);
            double eq = fd(f, "eq_" + si);
            double ef = fd(f, "ef_" + si);
            double etp = fd(f, "etp_" + si);
            if (ef <= 0) continue;
            jentries.push_back({ep, eq, ef, etp});
        }

        std::string symLower = sym;
        for (auto& c : symLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        std::ostringstream j;
        j << std::fixed << std::setprecision(8);
        j << "; Quant cycle \xe2\x80\x94 " << sym << " \xe2\x80\x94 generated " << jdate << "\n\n";

        double totalCost = 0, totalRevenue = 0, totalFees = 0;

        for (size_t i = 0; i < jentries.size(); ++i)
        {
            auto& e = jentries[i];
            double fee = QuantMath::feeFromRate(e.funding, feeSpread);
            totalCost += e.funding;
            totalFees += fee;
            j << jdate << " Buy " << sym << " level " << i << "\n"
              << "    assets:crypto:" << symLower << "    " << e.qty << " " << sym << " @ $" << e.price << "\n"
              << "    expenses:fees:exchange       $" << fee << "\n"
              << "    assets:bank:trading         $-" << (e.funding + fee) << "\n\n";
        }

        for (size_t i = 0; i < jentries.size(); ++i)
        {
            auto& e = jentries[i];
            double revenue = QuantMath::cost(e.tp, e.qty);
            double fee = QuantMath::feeFromRate(revenue, feeSpread);
            totalRevenue += revenue;
            totalFees += fee;
            j << jdate << " Sell " << sym << " TP level " << i << "\n"
              << "    assets:bank:trading          $" << QuantMath::proceeds(e.tp, e.qty, fee) << "\n"
              << "    expenses:fees:exchange       $" << fee << "\n"
              << "    assets:crypto:" << symLower << "   -" << e.qty << " " << sym << " @ $" << e.tp << "\n\n";
        }

        double gross = totalRevenue - totalCost - totalFees;
        double savingsAmt = QuantMath::savings(gross, savingsRate);
        if (savingsAmt > 0)
        {
            j << jdate << " Quant savings extraction\n"
              << "    assets:savings:quant         $" << savingsAmt << "\n"
              << "    income:trading:quant        $-" << savingsAmt << "\n\n";
        }

        j << "; Total fees: $" << totalFees << "\n"
          << "; Net profit: $" << gross << "\n"
          << "; Savings:    $" << savingsAmt << "\n";

        std::ostringstream h;
        h << "<h1>hledger Journal: " << html::esc(sym) << "</h1>"
             "<div class='msg'>Copy the journal below and append to your <code>~/.hledger.journal</code> inside docker-finance</div>"
             "<pre style='background:#0f172a;color:#e2e8f0;padding:16px;border-radius:8px;overflow-x:auto;font-size:0.85em;white-space:pre-wrap;'>"
          << html::esc(j.str()) << "</pre>"
             "<button onclick='navigator.clipboard.writeText(document.querySelector(\"pre\").textContent)' class='btn' style='margin-bottom:12px;'>Copy to Clipboard</button>"
             "<br><a class='btn' href='/serial-generator'>Back</a>";
        res.set_content(html::wrap("hledger Export", h.str()), "text/html");
    });

    // ========== POST /save-serial ==========
    svr.Post("/save-serial", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(dbMutex);
        auto f = parseForm(req.body);
        std::string sym = normalizeSymbol(fv(f, "symbol"));
        bool isShort = (fv(f, "isShort") == "1");
        double pump = fd(f, "pump");
        int count = fi(f, "entryCount");
        if (sym.empty() || count <= 0) { res.set_redirect("/serial-generator?err=Invalid+parameters", 303); return; }
        if (pump > 0) db.deposit(pump);
        int nextEpId = db.nextEntryId();
        std::vector<TradeDatabase::EntryPoint> newPoints;
        std::ostringstream h;
        h << std::fixed << std::setprecision(17);
        h << "<h1>Serial Entries Saved: " << html::esc(sym) << "</h1>";
        if (pump > 0) h << "<div class='msg'>Deposited pump " << pump << " into wallet. Balance: " << db.loadWalletBalance() << "</div>";
        h << "<table><tr><th>Lvl</th><th>Entry</th><th>Qty</th><th>Cost</th><th>Exit TP</th><th>Exit SL</th><th>Status</th></tr>";
        for (int i = 0; i < count; ++i)
        {
            std::string si = std::to_string(i);
            double entryPrice = fd(f, "ep_" + si);
            double fundQty = fd(f, "eq_" + si);
            double breakEven = fd(f, "eb_" + si);
            double funding = fd(f, "ef_" + si);
            double effOh = fd(f, "eov_" + si);
            double exitTP = fd(f, "etp_" + si);
            double exitSL = fd(f, "esl_" + si);
            if (funding <= 0) continue;
            double saveCost = QuantMath::cost(entryPrice, fundQty);
            TradeDatabase::EntryPoint ep;
            ep.symbol = sym; ep.entryId = nextEpId++; ep.levelIndex = i;
            ep.entryPrice = entryPrice; ep.breakEven = breakEven;
            ep.funding = funding; ep.fundingQty = fundQty;
            ep.effectiveOverhead = effOh; ep.isShort = isShort;
            ep.exitTakeProfit = exitTP; ep.exitStopLoss = exitSL;
            ep.traded = false; ep.linkedTradeId = -1;
            newPoints.push_back(ep);
            h << "<tr><td>" << i << "</td><td>" << entryPrice << "</td><td>" << fundQty << "</td>"
              << "<td>" << saveCost << "</td><td>" << exitTP << "</td><td>" << exitSL << "</td><td class='buy'>PENDING</td></tr>";
        }
        h << "</table>";
        auto existing = db.loadEntryPoints();
        for (const auto& ep : newPoints) existing.push_back(ep);
        db.saveEntryPoints(existing);
        h << "<div class='row'><div class='stat'><div class='lbl'>Saved</div><div class='val'>" << newPoints.size() << "</div></div>"
             "<div class='stat'><div class='lbl'>Wallet</div><div class='val'>" << db.loadWalletBalance() << "</div></div></div>";
        h << "<div class='msg'>" << newPoints.size() << " entry point(s) saved as pending.</div>";
        h << "<br><a class='btn' href='/entry-points'>Entry Points</a> <a class='btn' href='/price-check'>Price Check</a> <a class='btn' href='/serial-generator'>New Series</a>";
        res.set_content(html::wrap("Serial Entries Saved", h.str()), "text/html");
    });
}
