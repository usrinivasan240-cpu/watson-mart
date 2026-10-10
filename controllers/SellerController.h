// ============================================================
// MODULE: SellerController
// PURPOSE: Seller-facing analytics, fulfillment and feedback.
// WHY: Sellers need their own scoped views (sales, orders containing
//      their items, reviews on their products) separate from the
//      admin control plane.
// INPUT: Bearer token (seller or admin); ownerId taken from the token.
// OUTPUT: JSON aggregates/lists; 401/403 when not staff.
// ARCHITECTURE: Controller
// ============================================================

#pragma once

#include <drogon/drogon.h>

class SellerController
{
public:
    // GET /api/v1/seller/analytics
    static void sellerAnalytics(const drogon::HttpRequestPtr &req,
                                std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    // GET /api/v1/seller/orders
    static void sellerOrders(const drogon::HttpRequestPtr &req,
                             std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    // GET /api/v1/seller/reviews
    static void sellerReviews(const drogon::HttpRequestPtr &req,
                              std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    static void initRoutes();
};
