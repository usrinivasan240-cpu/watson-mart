// ============================================================
// MODULE: OrderService
// PURPOSE: PostgreSQL-backed checkout/billing logic + order status.
// WHY: Checkout must be atomic (stock, order header, order lines and cart
//      clearing commit together) and orders must persist in Postgres.
// INPUT: userId (cart owner).
// OUTPUT: JSON orders, each carrying an "items" array.
// ARCHITECTURE: Service
// ============================================================

#pragma once

#include <json/json.h>
#include <string>
#include <vector>
#include "../models/Order.h"

// Postgres-backed billing: totals are summed from live products.price at
// checkout and each line is snapshotted into order_items.
class OrderService
{
public:
    // Checkout user's cart -> creates order, clears cart.
    static Json::Value checkout(const std::string &userId);

    // Lists orders for a user.
    static Json::Value getOrders(const std::string &userId);

    // Admin: lists ALL orders (newest first), each with the buyer's
    // username and its "items" array. statusFilter "" = every status,
    // otherwise only that status (created/paid/shipped/delivered/cancelled).
    static Json::Value getAllOrders(const std::string &statusFilter = "");

    // Admin analytics: per-status {status, count, revenue} rows.
    static Json::Value getStatusCounts();

    // Admin analytics: per-owner sales rolled up from order_items
    // (owner snapshot) -> [{ownerId, units, revenue, orders}].
    static Json::Value getSalesByOwner();

    // Seller logistics: flat rows of MY items inside orders
    // -> [{orderId, buyer, orderStatus, orderTotal, createdAt,
    //      productId, productName, price, quantity, lineRevenue}].
    static Json::Value getOrdersForOwner(const std::string &ownerId);

    // Seller analytics: daily rollup for one owner, last `days` days
    // -> [{day, revenue, units, orders}] (oldest first).
    static Json::Value getDailySalesForOwner(const std::string &ownerId, int days);

    // Gets one order if owned by user.
    static Json::Value getOrderById(const std::string &userId, const std::string &orderId);

    // Updates status (created/paid/shipped). Returns order or error.
    // actorRole: controller already gates seller/admin; service re-applies the
    // rule (defense in depth) - sellers/admins may update any order, others only their own.
    static Json::Value updateStatus(const std::string &actorId, const std::string &actorRole,
                                    const std::string &orderId, const std::string &status);

private:
    static std::string generateUUID();
};
