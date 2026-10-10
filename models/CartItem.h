// ============================================================
// MODULE: CartItem
// PURPOSE: Represents one line in a user's shopping cart.
// WHY: Cart needs user + product + quantity before checkout.
// INPUT: userId, productId, quantity.
// OUTPUT: JSON for cart APIs.
// ARCHITECTURE: Model
// ============================================================

#pragma once

#include <string>
#include <drogon/drogon.h>

// One cart line: who owns it, what product, how many.
struct CartItem
{
    std::string userId;    // Owner (AuthService user id)
    std::string productId; // Product id (ProductService)
    int quantity;          // Qty >= 1

    CartItem() : quantity(1) {}

    // Converts to JSON for HTTP responses.
    Json::Value toJson() const
    {
        Json::Value json;
        json["userId"] = userId;
        json["productId"] = productId;
        json["quantity"] = quantity;
        return json;
    }
};
