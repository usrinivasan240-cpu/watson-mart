// ============================================================
// MODULE: CartService
// PURPOSE: Implementation of PostgreSQL-backed cart (cart_items table).
// WHY: Validates product existence/stock and the 99-unit line cap against
//      the shared database so carts persist across restarts.
// INPUT: userId, productId, quantity.
// OUTPUT: JSON cart or error.
// ARCHITECTURE: Service
// ============================================================

#include "CartService.h"
#include "Database.h"

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

Json::Value CartService::getCart(const std::string &userId)
{
    std::lock_guard<std::recursive_mutex> lock(Database::mutex());
    Json::Value result(Json::arrayValue);
    std::string err;
    // added_at keeps the previous insertion order of the cart lines.
    PGresult *res = sri_select(
        "SELECT product_id, quantity FROM cart_items WHERE user_id = $1 ORDER BY added_at",
        {userId}, err);
    if (res == nullptr)
    {
        // Database unavailable: same shape as an empty cart.
        return result;
    }
    int rows = PQntuples(res);
    for (int i = 0; i < rows; i++)
    {
        CartItem item;
        item.userId = userId;
        item.productId = PQgetvalue(res, i, 0);
        try { item.quantity = std::stoi(PQgetvalue(res, i, 1)); }
        catch (...) { item.quantity = 1; }
        result.append(item.toJson());
    }
    PQclear(res);
    return result;
}

Json::Value CartService::addItem(const std::string &userId, const std::string &productId, int quantity)
{
    std::lock_guard<std::recursive_mutex> lock(Database::mutex());
    if (quantity <= 0 || quantity > 99)
    {
        Json::Value error;
        error["error"] = "Quantity must be 1-99";
        return error;
    }
    std::string err;
    // Validate the product exists (same rule as before), read from Postgres.
    PGresult *product = sri_select("SELECT name, stock FROM products WHERE id = $1", {productId}, err);
    if (product == nullptr)
    {
        Json::Value error;
        error["error"] = err;
        return error;
    }
    if (PQntuples(product) == 0)
    {
        PQclear(product);
        Json::Value error;
        error["error"] = "Product not found";
        return error;
    }
    int stock = 0;
    try { stock = std::stoi(PQgetvalue(product, 0, 1)); }
    catch (...) { stock = 0; }
    PQclear(product);

    // Existing line? Same merge rules as before: a line may never exceed
    // 99 units and never the product's current stock.
    PGresult *line = sri_select(
        "SELECT quantity FROM cart_items WHERE user_id = $1 AND product_id = $2",
        {userId, productId}, err);
    if (line == nullptr)
    {
        Json::Value error;
        error["error"] = err;
        return error;
    }
    bool exists = PQntuples(line) > 0;
    int current = 0;
    if (exists)
    {
        try { current = std::stoi(PQgetvalue(line, 0, 0)); }
        catch (...) { current = 0; }
    }
    PQclear(line);

    CartItem item;
    item.userId = userId;
    item.productId = productId;

    if (exists)
    {
        int merged = current + quantity;
        if (merged > 99)
        {
            Json::Value error;
            error["error"] = "Cart line capped at 99 units";
            return error;
        }
        if (merged > stock)
        {
            Json::Value error;
            error["error"] = "Insufficient stock (only " + std::to_string(stock) + " available)";
            return error;
        }
        item.quantity = merged;
        if (!sri_cmd("UPDATE cart_items SET quantity = $3 WHERE user_id = $1 AND product_id = $2",
                     {userId, productId, std::to_string(merged)}, err))
        {
            Json::Value error;
            error["error"] = err;
            return error;
        }
        return item.toJson();
    }

    if (quantity > stock)
    {
        Json::Value error;
        error["error"] = "Insufficient stock (only " + std::to_string(stock) + " available)";
        return error;
    }
    item.quantity = quantity;
    if (!sri_cmd("INSERT INTO cart_items (user_id, product_id, quantity) "
                 "VALUES ($1, $2, $3::integer)",
                 {userId, productId, std::to_string(quantity)}, err))
    {
        Json::Value error;
        error["error"] = err;
        return error;
    }
    return item.toJson();
}

bool CartService::removeItem(const std::string &userId, const std::string &productId)
{
    std::lock_guard<std::recursive_mutex> lock(Database::mutex());
    std::string err;
    PGresult *res = Database::exec(
        "DELETE FROM cart_items WHERE user_id = $1 AND product_id = $2",
        {userId, productId}, err);
    if (res == nullptr)
    {
        // Database unavailable or statement failed: nothing was removed.
        return false;
    }
    bool removed = false;
    if (PQresultStatus(res) == PGRES_COMMAND_OK)
    {
        removed = !Database::cmdTuplesIs(res, "0");
    }
    PQclear(res);
    return removed;
}

void CartService::clearCart(const std::string &userId)
{
    std::lock_guard<std::recursive_mutex> lock(Database::mutex());
    std::string err;
    // Best effort (same as the old void in-memory clear): ignore failures.
    sri_cmd("DELETE FROM cart_items WHERE user_id = $1", {userId}, err);
}
