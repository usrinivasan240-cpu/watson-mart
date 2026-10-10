// ============================================================
// MODULE: CartController
// PURPOSE: HTTP layer for cart stub (Week 2).
// WHY: Front desk for GET/POST/DELETE cart; auth via Bearer token.
// INPUT: Bearer token + productId/quantity JSON.
// OUTPUT: JSON cart.
// ARCHITECTURE: Controller
// ============================================================

#pragma once

#include <drogon/drogon.h>

// Requires Authorization: Bearer <token> on all routes.
class CartController
{
public:
    static void getCart(const drogon::HttpRequestPtr &req,
                        std::function<void(const drogon::HttpResponsePtr &)> &&callback);
    static void addToCart(const drogon::HttpRequestPtr &req,
                          std::function<void(const drogon::HttpResponsePtr &)> &&callback);
    static void removeFromCart(const drogon::HttpRequestPtr &req,
                               std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                               std::string productId);
    static void initRoutes();
};
