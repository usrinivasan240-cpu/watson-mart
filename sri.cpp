// ============================================================
// MODULE: sri.cpp
// PURPOSE: Application entry point for Sri Mart.
// WHY: Starts the Drogon server, registers routes, listens on port 8080.
// INPUT: None (program start).
// OUTPUT: Running HTTP server with registered API endpoints.
// ARCHITECTURE: Entry point
// ============================================================

#include <drogon/drogon.h>
#include "controllers/ProductController.h"
#include "controllers/AuthController.h"
#include "controllers/CartController.h"
#include "controllers/OrderController.h"
#include "controllers/ReviewController.h"
#include "controllers/AdminController.h"
#include "controllers/SellerController.h"
#include "controllers/ChatController.h"
#include "services/ProductService.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

int main()
{
    // Port is configurable via PORT env var (default 8080) for deploy parity.
    int port = 8080;
    if (const char *envPort = std::getenv("PORT"))
    {
        try { int p = std::stoi(envPort); if (p > 0 && p < 65536) port = p; }
        catch (...) { /* keep default */ }
    }
    // Start the server and listen for HTTP requests.
    drogon::app().addListener("0.0.0.0", static_cast<uint16_t>(port));

    // CORS: the UI is statically hosted (e.g. Vercel) on a different origin,
    // so browsers preflight every JSON/Authorization request with OPTIONS.
    // Answer preflights globally and tag every response for cross-origin use.
    drogon::app().registerPreRoutingAdvice(
        [](const drogon::HttpRequestPtr &req,
           drogon::AdviceCallback &&aboutToStop,
           drogon::AdviceChainCallback &&passOn)
        {
            if (req->getMethod() == drogon::Options)
            {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k204NoContent);
                resp->addHeader("Access-Control-Allow-Origin", "*");
                resp->addHeader("Access-Control-Allow-Methods",
                                "GET, POST, PUT, DELETE, OPTIONS");
                resp->addHeader("Access-Control-Allow-Headers",
                                "Content-Type, Authorization");
                resp->addHeader("Access-Control-Max-Age", "86400");
                aboutToStop(resp);
            }
            else
            {
                passOn();
            }
        });
    drogon::app().registerPostHandlingAdvice(
        [](const drogon::HttpRequestPtr &,
           const drogon::HttpResponsePtr &resp)
        {
            resp->addHeader("Access-Control-Allow-Origin", "*");
        });

    // Health check endpoint: tells us if the server is alive.
    drogon::app().registerHandler(
        "/api/v1/health",
        [](const drogon::HttpRequestPtr &req,
           std::function<void(const drogon::HttpResponsePtr &)> &&callback)
        {
            Json::Value json;
            json["status"] = "UP";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
            callback(resp);
        },
        {drogon::Get});

    // Hello endpoint: simple test to prove the API is working.
    drogon::app().registerHandler(
        "/api/v1/hello",
        [](const drogon::HttpRequestPtr &req,
           std::function<void(const drogon::HttpResponsePtr &)> &&callback)
        {
            Json::Value json;
            json["message"] = "Welcome to Sri Mart API";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
            callback(resp);
        },
        {drogon::Get});

    // Helper to serve HTML files from config folder
    auto serveHtml = [](const std::string &path, std::function<void(const drogon::HttpResponsePtr &)> &&callback){
        std::ifstream file(path);
        if (file.is_open()){
            std::stringstream buffer; buffer << file.rdbuf();
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setBody(buffer.str());
            resp->setContentTypeCode(drogon::CT_TEXT_HTML);
            callback(resp);
        } else {
            Json::Value json; json["message"] = "Page not found: " + path;
            auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
            resp->setStatusCode(drogon::k404NotFound);
            callback(resp);
        }
    };

    // Login page: serves the HTML login/register form.
    drogon::app().registerHandler(
        "/",
        [serveHtml](const drogon::HttpRequestPtr &req,
           std::function<void(const drogon::HttpResponsePtr &)> &&callback)
        { serveHtml("config/login.html", std::move(callback)); },
        {drogon::Get});

    // Buyer dashboard — separate page for customer role, lively DB
    drogon::app().registerHandler(
        "/buyer",
        [serveHtml](const drogon::HttpRequestPtr &req,
           std::function<void(const drogon::HttpResponsePtr &)> &&callback)
        { serveHtml("config/buyer.html", std::move(callback)); },
        {drogon::Get});
    drogon::app().registerHandler(
        "/buyer.html",
        [serveHtml](const drogon::HttpRequestPtr &req,
           std::function<void(const drogon::HttpResponsePtr &)> &&callback)
        { serveHtml("config/buyer.html", std::move(callback)); },
        {drogon::Get});

    // Seller dashboard — separate page for seller role, lively DB CRUD
    drogon::app().registerHandler(
        "/seller",
        [serveHtml](const drogon::HttpRequestPtr &req,
           std::function<void(const drogon::HttpResponsePtr &)> &&callback)
        { serveHtml("config/seller.html", std::move(callback)); },
        {drogon::Get});
    drogon::app().registerHandler(
        "/seller.html",
        [serveHtml](const drogon::HttpRequestPtr &req,
           std::function<void(const drogon::HttpResponsePtr &)> &&callback)
        { serveHtml("config/seller.html", std::move(callback)); },
        {drogon::Get});

    // Admin dashboard — proper admin login
    drogon::app().registerHandler(
        "/admin",
        [serveHtml](const drogon::HttpRequestPtr &req,
           std::function<void(const drogon::HttpResponsePtr &)> &&callback)
        { serveHtml("config/admin.html", std::move(callback)); },
        {drogon::Get});
    drogon::app().registerHandler(
        "/admin.html",
        [serveHtml](const drogon::HttpRequestPtr &req,
           std::function<void(const drogon::HttpResponsePtr &)> &&callback)
        { serveHtml("config/admin.html", std::move(callback)); },
        {drogon::Get});

    // Order success page — standalone bill shown after checkout.
    drogon::app().registerHandler(
        "/order",
        [serveHtml](const drogon::HttpRequestPtr &req,
           std::function<void(const drogon::HttpResponsePtr &)> &&callback)
        { serveHtml("config/order.html", std::move(callback)); },
        {drogon::Get});
    drogon::app().registerHandler(
        "/order.html",
        [serveHtml](const drogon::HttpRequestPtr &req,
           std::function<void(const drogon::HttpResponsePtr &)> &&callback)
        { serveHtml("config/order.html", std::move(callback)); },
        {drogon::Get});

    // Frontend API base config — loaded by every page via
    // <script src="/config/api-config.js">. Served explicitly so the file
    // resolves regardless of Drogon's static-file router configuration.
    drogon::app().registerHandler(
        "/config/api-config.js",
        [](const drogon::HttpRequestPtr &,
           std::function<void(const drogon::HttpResponsePtr &)> &&callback)
        {
            std::ifstream file("config/api-config.js");
            if (file.is_open())
            {
                std::stringstream buffer;
                buffer << file.rdbuf();
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setBody(buffer.str());
                resp->setContentTypeCode(drogon::CT_TEXT_JAVASCRIPT);
                callback(resp);
            }
            else
            {
                Json::Value json;
                json["message"] = "Page not found: config/api-config.js";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
                resp->setStatusCode(drogon::k404NotFound);
                callback(resp);
            }
        },
        {drogon::Get});

    // Register all product-related routes (GET, POST, PUT, DELETE, search).
    ProductController::initRoutes();

    // Register all authentication routes (login, register, validate, logout).
    AuthController::initRoutes();

    // Register Week 2-11 stubs: cart, orders, reviews, seller/admin, chatbot.
    CartController::initRoutes();
    OrderController::initRoutes();
    ReviewController::initRoutes();
    AdminController::initRoutes();
    SellerController::initRoutes();
    ChatController::initRoutes();

    // Initialize the PostgreSQL database table for products.
    ProductService::initializeDatabase();

    // Keep the server running and handle requests.
    std::cout << "Sri Mart API running on http://0.0.0.0:" << port << std::endl;
    std::cout << "Login page: http://localhost:" << port << "/" << std::endl;
    drogon::app().run();

    return 0;
}
