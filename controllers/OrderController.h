// ============================================================
// MODULE: OrderController
// PURPOSE: HTTP layer for checkout/billing + status (Weeks 2,5).
// WHY: Bearer-auth order APIs.
// INPUT: Bearer token, order id/status JSON.
// OUTPUT: JSON orders.
// ARCHITECTURE: Controller
// ============================================================

#pragma once

#include <drogon/drogon.h>

class OrderController
{
public:
    static void checkout(const drogon::HttpRequestPtr &req,
                         std::function<void(const drogon::HttpResponsePtr &)> &&callback);
    static void listOrders(const drogon::HttpRequestPtr &req,
                           std::function<void(const drogon::HttpResponsePtr &)> &&callback);
    static void getOrder(const drogon::HttpRequestPtr &req,
                         std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                         std::string orderId);
    static void updateStatus(const drogon::HttpRequestPtr &req,
                             std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                             std::string orderId);
    static void initRoutes();
};
