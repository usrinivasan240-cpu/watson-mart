// ============================================================
// MODULE: CartService
// PURPOSE: PostgreSQL-backed shopping cart logic (cart_items table).
// WHY: Keeps cart rules out of controllers; cart lines persist in Postgres
//      so they survive restarts and are consistent across requests.
// INPUT: userId + productId + quantity.
// OUTPUT: JSON cart arrays or error objects.
// ARCHITECTURE: Service
// ============================================================

#pragma once

#include <json/json.h>
#include <string>
#include <vector>
#include "../models/CartItem.h"

// Cart lines live in Postgres cart_items(user_id, product_id, quantity),
// returned in added_at (insertion) order.
class CartService
{
public:
    // Returns all items for a user.
    static Json::Value getCart(const std::string &userId);

    // Adds qty of product to user's cart. Returns item or error.
    static Json::Value addItem(const std::string &userId, const std::string &productId, int quantity);

    // Removes one product from user's cart. Returns true if removed.
    static bool removeItem(const std::string &userId, const std::string &productId);

    // Clears all items for a user (used after checkout).
    static void clearCart(const std::string &userId);
};
