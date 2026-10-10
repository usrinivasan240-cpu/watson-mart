// ============================================================
// MODULE: OrderService
// PURPOSE: Implementation of PostgreSQL-backed checkout + order status.
// WHY: Checkout runs in a single transaction (stock reservation, order
//      header, order lines and cart clearing) so a failure can never
//      leave a partially created order behind.
// INPUT: userId.
// OUTPUT: JSON order (with "items") or error.
// ARCHITECTURE: Service
// ============================================================

#include "OrderService.h"
#include "Database.h"

#include <random>
#include <sstream>
#include <string>
#include <vector>

// Maps Database::exec's raw driver message to the app's user-facing text.
static std::string sri_dbMessage(const std::string &raw)
{
    if (raw == "database connection unavailable")
    {
        return "Database connection failed";
    }
    return "Database error";
}

// SELECT helper: returns a PGRES_TUPLES_OK result the caller must PQclear(),
// or nullptr with err set to a user-facing database error message.
static PGresult *sri_select(const std::string &sql, const std::vector<std::string> &params, std::string &err)
{
    PGresult *res = Database::exec(sql, params, err);
    if (res == nullptr)
    {
        err = sri_dbMessage(err);
        return nullptr;
    }
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        PQclear(res);
        err = "Database error";
        return nullptr;
    }
    return res;
}

// Runs a statement expecting PGRES_COMMAND_OK. Returns false with err set
// to a user-facing database error message.
static bool sri_cmd(const std::string &sql, const std::vector<std::string> &params, std::string &err)
{
    PGresult *res = Database::exec(sql, params, err);
    if (res == nullptr)
    {
        err = sri_dbMessage(err);
        return false;
    }
    bool ok = (PQresultStatus(res) == PGRES_COMMAND_OK);
    PQclear(res);
    if (!ok)
    {
        err = "Database error";
        return false;
    }
    return true;
}

std::string OrderService::generateUUID()
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    static std::uniform_int_distribution<> dis2(8, 11);
    std::stringstream ss;
    ss << std::hex;
    for (int i = 0; i < 8; i++) ss << dis(gen);
    ss << "-";
    for (int i = 0; i < 4; i++) ss << dis(gen);
    ss << "-4";
    for (int i = 0; i < 3; i++) ss << dis(gen);
    ss << "-";
    ss << dis2(gen);
    for (int i = 0; i < 3; i++) ss << dis(gen);
    ss << "-";
    for (int i = 0; i < 12; i++) ss << dis(gen);
    return ss.str();
}

// Builds an Order from an orders row:
// id, user_id, status, total, item_count, created_at (formatted text).
static Order sri_orderRow(PGresult *res, int row)
{
    Order order;
    order.id = PQgetvalue(res, row, 0);
    order.userId = PQgetvalue(res, row, 1);
    order.status = PQgetvalue(res, row, 2);
    try { order.total = std::stod(PQgetvalue(res, row, 3)); }
    catch (...) { order.total = 0.0; }
    try { order.itemCount = std::stoi(PQgetvalue(res, row, 4)); }
    catch (...) { order.itemCount = 0; }
    order.created_at = PQgetvalue(res, row, 5);
    return order;
}

// Loads an order's lines into order.items: [{productId,name,price,quantity}].
// Returns false (and an empty array) when the query itself failed.
static bool sri_loadItems(Order &order, std::string &err)
{
    order.items = Json::Value(Json::arrayValue);
    PGresult *res = sri_select(
        "SELECT product_id, name, price, quantity FROM order_items "
        "WHERE order_id = $1 ORDER BY product_id",
        {order.id}, err);
    if (res == nullptr)
    {
        return false;
    }
    int rows = PQntuples(res);
    for (int i = 0; i < rows; i++)
    {
        Json::Value item;
        item["productId"] = PQgetvalue(res, i, 0);
        item["name"] = PQgetvalue(res, i, 1);
        try { item["price"] = std::stod(PQgetvalue(res, i, 2)); }
        catch (...) { item["price"] = 0.0; }
        try { item["quantity"] = std::stoi(PQgetvalue(res, i, 3)); }
        catch (...) { item["quantity"] = 0; }
        order.items.append(item);
    }
    PQclear(res);
    return true;
}

// Loads one order owned by userId plus its "items" array.
// Returns null JSON when the order doesn't exist / isn't owned, or when it
// can't be read (err then carries the database failure message).
// anyUser=true (seller/admin flows) skips the ownership filter so staff can read
// back an order they just updated; normal callers stay scoped to the owner.
static Json::Value sri_loadOrder(const std::string &userId, const std::string &orderId,
                                 std::string &err, bool anyUser = false)
{
    PGresult *res = anyUser
        ? sri_select(
              "SELECT id, user_id, status, total, item_count, "
              "to_char(created_at, 'YYYY-MM-DD HH24:MI:SS') AS created_at "
              "FROM orders WHERE id = $1",
              {orderId}, err)
        : sri_select(
              "SELECT id, user_id, status, total, item_count, "
              "to_char(created_at, 'YYYY-MM-DD HH24:MI:SS') AS created_at "
              "FROM orders WHERE id = $1 AND user_id = $2",
              {orderId, userId}, err);
    if (res == nullptr)
    {
        return Json::Value();
    }
    if (PQntuples(res) == 0)
    {
        PQclear(res);
        err = "Order not found";
        return Json::Value();
    }
    Order order = sri_orderRow(res, 0);
    PQclear(res);
    if (!sri_loadItems(order, err))
    {
        return Json::Value();
    }
    return order.toJson();
}

Json::Value OrderService::checkout(const std::string &userId)
{
    std::lock_guard<std::recursive_mutex> lock(Database::mutex());
    std::string err;

    // 1. Read the cart (same "Cart is empty" rule as before).
    PGresult *cart = sri_select(
        "SELECT product_id, quantity FROM cart_items WHERE user_id = $1 ORDER BY added_at",
        {userId}, err);
    if (cart == nullptr)
    {
        Json::Value error;
        error["error"] = err;
        return error;
    }
    int lines = PQntuples(cart);
    if (lines == 0)
    {
        PQclear(cart);
        Json::Value error;
        error["error"] = "Cart is empty";
        return error;
    }
    if (lines > 100)
    {
        PQclear(cart);
        Json::Value error;
        error["error"] = "Too many cart lines (max 100)";
        return error;
    }
    std::vector<std::string> productIds;
    std::vector<int> quantities;
    productIds.reserve(lines);
    quantities.reserve(lines);
    for (int i = 0; i < lines; i++)
    {
        productIds.emplace_back(PQgetvalue(cart, i, 0));
        int qty = 1;
        try { qty = std::stoi(PQgetvalue(cart, i, 1)); }
        catch (...) { qty = 1; }
        quantities.push_back(qty);
    }
    PQclear(cart);

    // 2. One transaction: stock, order header, lines and cart clearing.
    if (!sri_cmd("BEGIN", {}, err))
    {
        Json::Value error;
        error["error"] = "Checkout transaction failed to start";
        return error;
    }
    auto rollback = [&](std::string msg) {
        std::string rbErr;
        sri_cmd("ROLLBACK", {}, rbErr);
        Json::Value error;
        error["error"] = msg;
        return error;
    };

    double total = 0.0;
    std::vector<std::string> names(productIds.size());
    std::vector<double> prices(productIds.size(), 0.0);
    std::vector<std::string> owners(productIds.size());
    for (size_t i = 0; i < productIds.size(); i++)
    {
        const std::string &pid = productIds[i];
        int qty = quantities[i];
        if (qty <= 0 || qty > 99)
        {
            return rollback("Invalid quantity");
        }
        PGresult *prod = sri_select("SELECT name, price, owner_id FROM products WHERE id = $1", {pid}, err);
        if (prod == nullptr)
        {
            return rollback(err);
        }
        if (PQntuples(prod) == 0)
        {
            PQclear(prod);
            return rollback("Product not found: " + pid);
        }
        names[i] = PQgetvalue(prod, 0, 0);
        try { prices[i] = std::stod(PQgetvalue(prod, 0, 1)); }
        catch (...) { prices[i] = 0.0; }
        owners[i] = PQgetvalue(prod, 0, 2); // seller snapshot for analytics
        PQclear(prod);

        // Atomic reservation: decrements only while stock is sufficient.
        std::string qtyS = std::to_string(qty);
        PGresult *upd = Database::exec(
            "UPDATE products SET stock = stock - $1::integer, updated_at = CURRENT_TIMESTAMP "
            "WHERE id = $2 AND stock >= $1::integer",
            {qtyS, pid}, err);
        if (upd == nullptr)
        {
            std::string msg = (err == "database connection unavailable")
                                  ? std::string("Database connection failed")
                                  : ("Stock update failed: " + err);
            return rollback(msg);
        }
        bool reserved = Database::cmdTuplesIs(upd, "1");
        PQclear(upd);
        if (!reserved)
        {
            // The product row exists (checked above): stock is too low.
            return rollback("Insufficient stock");
        }
        total += prices[i] * qty;
    }

    // 3. Order header.
    std::string orderId = generateUUID();
    if (!sri_cmd("INSERT INTO orders (id, user_id, status, total, item_count) "
                 "VALUES ($1, $2, 'created', $3::decimal, $4::integer)",
                 {orderId, userId, std::to_string(total),
                  std::to_string((int)productIds.size())}, err))
    {
        return rollback("Failed to create order");
    }

    // 4. Order lines: name/price/owner snapshotted from the products rows above.
    for (size_t i = 0; i < productIds.size(); i++)
    {
        if (!sri_cmd("INSERT INTO order_items (order_id, product_id, name, price, quantity, owner_id) "
                     "VALUES ($1, $2, $3, $4::decimal, $5::integer, $6)",
                     {orderId, productIds[i], names[i], std::to_string(prices[i]),
                      std::to_string(quantities[i]), owners[i]}, err))
        {
            return rollback("Failed to create order");
        }
    }

    // 5. Clear the cart inside the same transaction.
    if (!sri_cmd("DELETE FROM cart_items WHERE user_id = $1", {userId}, err))
    {
        return rollback(err);
    }

    // 6. Read the order back before COMMIT so read failures roll back too.
    Json::Value order = sri_loadOrder(userId, orderId, err);
    if (order.isNull())
    {
        return rollback(err.empty() ? std::string("Failed to create order") : err);
    }

    if (!sri_cmd("COMMIT", {}, err))
    {
        std::string rbErr;
        sri_cmd("ROLLBACK", {}, rbErr);
        Json::Value error;
        error["error"] = "Checkout commit failed";
        return error;
    }
    return order;
}

Json::Value OrderService::getOrders(const std::string &userId)
{
    std::lock_guard<std::recursive_mutex> lock(Database::mutex());
    std::string err;
    Json::Value result(Json::arrayValue);
    PGresult *res = sri_select(
        "SELECT id, user_id, status, total, item_count, "
        "to_char(created_at, 'YYYY-MM-DD HH24:MI:SS') AS created_at "
        "FROM orders WHERE user_id = $1 ORDER BY created_at, id",
        {userId}, err);
    if (res == nullptr)
    {
        // Database unavailable: same shape as "no orders yet".
        return result;
    }
    int rows = PQntuples(res);
    for (int i = 0; i < rows; i++)
    {
        Order order = sri_orderRow(res, i);
        sri_loadItems(order, err); // best effort: empty array if unreadable
        result.append(order.toJson());
    }
    PQclear(res);
    return result;
}

Json::Value OrderService::getOrderById(const std::string &userId, const std::string &orderId)
{
    std::lock_guard<std::recursive_mutex> lock(Database::mutex());
    std::string err;
    // Null when missing/not owned (controller maps null to 404); orders
    // always include their "items" array.
    return sri_loadOrder(userId, orderId, err);
}

Json::Value OrderService::updateStatus(const std::string &actorId, const std::string &actorRole,
                                        const std::string &orderId, const std::string &status)
{
    if (status != "created" && status != "paid" && status != "shipped" &&
        status != "delivered" && status != "cancelled")
    {
        Json::Value error;
        error["error"] = "Status must be created, paid, shipped, delivered, or cancelled";
        return error;
    }
    // Sellers/admins may transition any order; anyone else only their own
    // (the controller already 403s non-staff, this is defense in depth).
    bool privileged = (actorRole == "seller" || actorRole == "admin");
    std::lock_guard<std::recursive_mutex> lock(Database::mutex());
    std::string err;
    // Fix: buyers may only pay an unpaid (created) order or cancel their own
    // still-open order; shipped/delivered remain staff-only. This guard is
    // also hit by the assistant's pay/cancel intents (single source of truth).
    if (!privileged)
    {
        Json::Value current = sri_loadOrder(actorId, orderId, err, false);
        if (current.isNull())
            return Json::Value(); // not found / not owned -> 404
        std::string cur = current.get("status", "created").asString();
        bool canPay = (status == "paid" && cur == "created");
        bool canCancel = (status == "cancelled" && (cur == "created" || cur == "paid"));
        if (!canPay && !canCancel)
        {
            Json::Value error;
            error["error"] = "Buyers can only pay unpaid orders or cancel their own active orders";
            return error;
        }
    }
    PGresult *upd = privileged
        ? Database::exec(
              "UPDATE orders SET status = $1 WHERE id = $2",
              {status, orderId}, err)
        : Database::exec(
              "UPDATE orders SET status = $1 WHERE id = $2 AND user_id = $3",
              {status, orderId, actorId}, err);
    if (upd == nullptr)
    {
        Json::Value error;
        error["error"] = sri_dbMessage(err);
        return error;
    }
    bool touched = (PQresultStatus(upd) == PGRES_COMMAND_OK) &&
                   !Database::cmdTuplesIs(upd, "0");
    PQclear(upd);
    if (!touched)
    {
        // No such order (or not owned by a non-privileged actor) -> 404.
        return Json::Value();
    }
    Json::Value order = sri_loadOrder(actorId, orderId, err, privileged);
    if (order.isNull())
    {
        Json::Value error;
        error["error"] = err.empty() ? std::string("Database error") : err;
        return error;
    }
    return order;
}

// ---------------------------------------------------------------------------
// Admin logistics + analytics queries
// ---------------------------------------------------------------------------

// All orders (newest first) with buyer username + items; optional status filter.
Json::Value OrderService::getAllOrders(const std::string &statusFilter)
{
    std::lock_guard<std::recursive_mutex> lock(Database::mutex());
    std::string err;
    Json::Value result(Json::arrayValue);
    PGresult *res = sri_select(
        "SELECT o.id, o.user_id, o.status, o.total, o.item_count, "
        "to_char(o.created_at, 'YYYY-MM-DD HH24:MI:SS') AS created_at, "
        "COALESCE(u.username, '(deleted user)') AS username "
        "FROM orders o LEFT JOIN users u ON u.id = o.user_id "
        "WHERE ($1 = '' OR o.status = $1) "
        "ORDER BY o.created_at DESC, o.id",
        {statusFilter}, err);
    if (res == nullptr)
    {
        // Database unavailable: same shape as "no orders yet".
        return result;
    }
    int rows = PQntuples(res);
    for (int i = 0; i < rows; i++)
    {
        Order order = sri_orderRow(res, i);
        sri_loadItems(order, err); // best effort: empty array if unreadable
        Json::Value j = order.toJson();
        j["username"] = PQgetvalue(res, i, 6);
        result.append(j);
    }
    PQclear(res);
    return result;
}

// Per-status order counts + revenue (drives the dashboard cards).
Json::Value OrderService::getStatusCounts()
{
    std::lock_guard<std::recursive_mutex> lock(Database::mutex());
    std::string err;
    Json::Value result(Json::arrayValue);
    PGresult *res = sri_select(
        "SELECT status, COUNT(*), COALESCE(SUM(total), 0) "
        "FROM orders GROUP BY status ORDER BY status",
        {}, err);
    if (res == nullptr)
    {
        return result;
    }
    int rows = PQntuples(res);
    for (int i = 0; i < rows; i++)
    {
        Json::Value row;
        row["status"] = PQgetvalue(res, i, 0);
        try { row["count"] = std::stoi(PQgetvalue(res, i, 1)); }
        catch (...) { row["count"] = 0; }
        try { row["revenue"] = std::stod(PQgetvalue(res, i, 2)); }
        catch (...) { row["revenue"] = 0.0; }
        result.append(row);
    }
    PQclear(res);
    return result;
}

// Sales rolled up by the owner_id snapshot stored on each order line,
// so revenue stays attributed to the seller even after a product edit.
// Flat rows of one seller's items inside orders (drives the seller
// fulfillment table): order context + my line, newest orders first.
Json::Value OrderService::getOrdersForOwner(const std::string &ownerId)
{
    std::lock_guard<std::recursive_mutex> lock(Database::mutex());
    std::string err;
    Json::Value result(Json::arrayValue);
    PGresult *res = sri_select(
        "SELECT o.id, o.status, o.total, "
        "to_char(o.created_at, 'YYYY-MM-DD HH24:MI:SS') AS created_at, "
        "COALESCE(u.username, '(deleted user)') AS buyer, "
        "oi.product_id, oi.name, oi.price, oi.quantity "
        "FROM order_items oi "
        "JOIN orders o ON o.id = oi.order_id "
        "LEFT JOIN users u ON u.id = o.user_id "
        "WHERE oi.owner_id = $1 "
        "ORDER BY o.created_at DESC, o.id, oi.product_id",
        {ownerId}, err);
    if (res == nullptr)
    {
        return result;
    }
    int rows = PQntuples(res);
    for (int i = 0; i < rows; i++)
    {
        Json::Value row;
        row["orderId"] = PQgetvalue(res, i, 0);
        row["orderStatus"] = PQgetvalue(res, i, 1);
        try { row["orderTotal"] = std::stod(PQgetvalue(res, i, 2)); }
        catch (...) { row["orderTotal"] = 0.0; }
        row["createdAt"] = PQgetvalue(res, i, 3);
        row["buyer"] = PQgetvalue(res, i, 4);
        row["productId"] = PQgetvalue(res, i, 5);
        row["productName"] = PQgetvalue(res, i, 6);
        double price = 0.0;
        int qty = 0;
        try { price = std::stod(PQgetvalue(res, i, 7)); }
        catch (...) { price = 0.0; }
        try { qty = std::stoi(PQgetvalue(res, i, 8)); }
        catch (...) { qty = 0; }
        row["price"] = price;
        row["quantity"] = qty;
        row["lineRevenue"] = price * qty;
        result.append(row);
    }
    PQclear(res);
    return result;
}

// Daily revenue/units/orders for one seller (drives the sales-by-day bars).
Json::Value OrderService::getDailySalesForOwner(const std::string &ownerId, int days)
{
    std::lock_guard<std::recursive_mutex> lock(Database::mutex());
    std::string err;
    Json::Value result(Json::arrayValue);
    if (days < 1) days = 14;
    if (days > 90) days = 90;
    PGresult *res = sri_select(
        "SELECT to_char(o.created_at, 'YYYY-MM-DD') AS day, "
        "COALESCE(SUM(oi.price * oi.quantity), 0), "
        "COALESCE(SUM(oi.quantity), 0), COUNT(DISTINCT o.id) "
        "FROM order_items oi JOIN orders o ON o.id = oi.order_id "
        "WHERE oi.owner_id = $1 AND o.created_at >= CURRENT_DATE - ($2::int - 1) "
        "GROUP BY 1 ORDER BY 1",
        {ownerId, std::to_string(days)}, err);
    if (res == nullptr)
    {
        return result;
    }
    int rows = PQntuples(res);
    for (int i = 0; i < rows; i++)
    {
        Json::Value row;
        row["day"] = PQgetvalue(res, i, 0);
        try { row["revenue"] = std::stod(PQgetvalue(res, i, 1)); }
        catch (...) { row["revenue"] = 0.0; }
        try { row["units"] = std::stoi(PQgetvalue(res, i, 2)); }
        catch (...) { row["units"] = 0; }
        try { row["orders"] = std::stoi(PQgetvalue(res, i, 3)); }
        catch (...) { row["orders"] = 0; }
        result.append(row);
    }
    PQclear(res);
    return result;
}

Json::Value OrderService::getSalesByOwner()
{
    std::lock_guard<std::recursive_mutex> lock(Database::mutex());
    std::string err;
    Json::Value result(Json::arrayValue);
    PGresult *res = sri_select(
        "SELECT owner_id, COALESCE(SUM(quantity), 0), "
        "COALESCE(SUM(price * quantity), 0), COUNT(DISTINCT order_id) "
        "FROM order_items GROUP BY owner_id ORDER BY owner_id",
        {}, err);
    if (res == nullptr)
    {
        return result;
    }
    int rows = PQntuples(res);
    for (int i = 0; i < rows; i++)
    {
        Json::Value row;
        row["ownerId"] = PQgetvalue(res, i, 0);
        try { row["units"] = std::stoi(PQgetvalue(res, i, 1)); }
        catch (...) { row["units"] = 0; }
        try { row["revenue"] = std::stod(PQgetvalue(res, i, 2)); }
        catch (...) { row["revenue"] = 0.0; }
        try { row["orders"] = std::stoi(PQgetvalue(res, i, 3)); }
        catch (...) { row["orders"] = 0; }
        result.append(row);
    }
    PQclear(res);
    return result;
}
