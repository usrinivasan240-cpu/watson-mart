// ============================================================
// MODULE: AdminController
// PURPOSE: Seller stats + the full admin control plane.
// WHY: Role-gated from validateToken; every admin operation
//      (overview, logistics, seller analysis, user management)
//      lives behind one admin gate helper.
// INPUT: Bearer token, JSON bodies, query params (status).
// OUTPUT: JSON stats/lists or 401/403/400/404.
// ARCHITECTURE: Controller
// ============================================================

#pragma once
#include <drogon/drogon.h>

class AdminController
{
public:
    // GET /api/v1/seller/stats -> own product count (seller/admin).
    static void sellerStats(const drogon::HttpRequestPtr &req,
        std::function<void(const drogon::HttpResponsePtr &)> &&callback);
    // GET /api/v1/admin/stats -> users + products counts (admin only).
    static void adminStats(const drogon::HttpRequestPtr &req,
        std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    // GET /api/v1/admin/overview -> dashboard rollup: users by role,
    // catalog value, order status counts + revenue (admin only).
    static void adminOverview(const drogon::HttpRequestPtr &req,
        std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    // GET /api/v1/admin/orders?status= -> every order with buyer name
    // and items; optional status filter (admin only).
    static void adminOrders(const drogon::HttpRequestPtr &req,
        std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    // GET /api/v1/admin/customers -> buyers with order stats
    // (count / total spent / last order) (admin only).
    static void adminCustomers(const drogon::HttpRequestPtr &req,
        std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    // GET /api/v1/admin/sellers -> each seller with catalog + sales
    // aggregates (products, stock, revenue) (admin only).
    static void adminSellers(const drogon::HttpRequestPtr &req,
        std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    // GET /api/v1/admin/analytics -> seller analysis payload:
    // per-seller breakdown (products/prices/stock/sales), per-product
    // performance, price stats (admin only).
    static void adminAnalytics(const drogon::HttpRequestPtr &req,
        std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    // PUT /api/v1/admin/users/{id} -> edit role/username/email/password.
    // Drogon passes each {path} segment as a trailing argument.
    static void adminUpdateUser(const drogon::HttpRequestPtr &req,
        std::function<void(const drogon::HttpResponsePtr &)> &&callback,
        std::string userId);

    // DELETE /api/v1/admin/users/{id} -> remove user + sessions + cart.
    static void adminDeleteUser(const drogon::HttpRequestPtr &req,
        std::function<void(const drogon::HttpResponsePtr &)> &&callback,
        std::string userId);

    static void initRoutes();
};
