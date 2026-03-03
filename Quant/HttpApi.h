#pragma once

#include "HtmlHelpers.h"
#include "TradeDatabase.h"
#include "UserManager.h"
#include "AdminConfig.h"
#include "AppContext.h"

#include "Routes_Auth.h"
#include "Routes_Core.h"
#include "Routes_Trades.h"
#include "Routes_Profit.h"
#include "Routes_Horizons.h"
#include "Routes_Entry.h"
#include "Routes_Exit.h"
#include "Routes_PriceCheck.h"
#include "Routes_SerialGen.h"
#include "Routes_Chart.h"
#include "Routes_Api.h"
#include "Routes_Admin.h"
#include "Routes_Premium.h"
#include "Routes_Simulator.h"
#include "Routes_Symbols.h"

#include <mutex>
#include <thread>
#include <iostream>

// ---- HTTP server ----
inline void startHttpApi(TradeDatabase& db, int port, std::mutex& dbMutex)
{
    static UserManager     users("users");
    static AdminConfig     config("admin_config.json");
    static SymbolRegistry  symbols;
    static PriceSeries     prices;

    // Seed the symbol registry from existing trades
    {
        auto trades = db.loadTrades();
        std::vector<std::string> syms;
        for (const auto& t : trades)
            if (std::find(syms.begin(), syms.end(), t.symbol) == syms.end())
                syms.push_back(t.symbol);
        symbols.seed(syms);
    }

    AppContext ctx{ users, dbMutex, db, config, symbols, prices };

    httplib::Server svr;

    // Error handler for unhandled exceptions in route handlers
    svr.set_exception_handler([](const httplib::Request& req, httplib::Response& res, std::exception_ptr ep) {
        try { if (ep) std::rethrow_exception(ep); }
        catch (const std::exception& e) {
            std::cerr << "  [HTTP] exception on " << req.path << ": " << e.what() << std::endl;
            res.set_content("<h1>500 Internal Server Error</h1><pre>" + std::string(e.what()) + "</pre>", "text/html");
            res.status = 500;
        }
    });

    // Auth middleware — allow login/register/logout without session
    svr.set_pre_routing_handler([&](const httplib::Request& req, httplib::Response& res) {
        if (req.path == "/login" || req.path == "/register" || req.path == "/logout")
            return httplib::Server::HandlerResponse::Unhandled;
        auto user = ctx.currentUser(req);
        if (user.empty())
        {
            res.set_redirect("/login", 303);
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    registerAuthRoutes(svr, ctx);
    registerCoreRoutes(svr, ctx);
    registerTradeRoutes(svr, ctx);
    registerProfitRoutes(svr, ctx);
    registerHorizonRoutes(svr, ctx);
    registerEntryRoutes(svr, ctx);
    registerExitRoutes(svr, ctx);
    registerPriceCheckRoutes(svr, ctx);
    registerSerialGenRoutes(svr, ctx);
    registerChartRoutes(svr, ctx);
    registerApiRoutes(svr, ctx);
    registerAdminRoutes(svr, ctx);
    registerPremiumRoutes(svr, ctx);
    registerSimulatorRoutes(svr, ctx);
    registerSymbolRoutes(svr, ctx);

    std::cout << "  [HTTP] listening on http://localhost:" << port << "\n";
    if (!svr.listen("0.0.0.0", port))
    {
        std::cerr << "  [HTTP] ERROR: failed to bind to port " << port
                  << " (already in use?)\n";
    }
}
