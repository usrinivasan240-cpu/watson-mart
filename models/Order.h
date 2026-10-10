// ============================================================
// MODULE: Order
// PURPOSE: Represents a billing order (checkout + status tracking).
// WHY: Checkout needs id, owner, lines, total, status.
// INPUT: Checkout data or a row from the orders/order_items tables.
// OUTPUT: JSON order, including its "items" array.
// ARCHITECTURE: Model
// ============================================================

#pragma once

#include <string>
#include <drogon/drogon.h>

// Order header + snapshotted lines: id, owner, total, status, item count.
struct Order
{
    std::string id;        // UUID
    std::string userId;    // Owner
    double total;          // Sum(price*qty) as stored in orders.total
    std::string status;    // e.g. created, paid, shipped
    int itemCount;         // Number of lines
    std::string created_at;
    Json::Value items;     // [{productId, name, price, quantity}]

    Order() : total(0.0), status("created"), itemCount(0), items(Json::arrayValue) {}

    Json::Value toJson() const
    {
        Json::Value json;
        json["id"] = id;
        json["userId"] = userId;
        json["total"] = total;
        json["status"] = status;
        json["itemCount"] = itemCount;
        json["created_at"] = created_at;
        json["items"] = items;
        return json;
    }
};
