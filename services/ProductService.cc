// ============================================================
// MODULE: ProductService
// PURPOSE: Implementation of product business logic using PostgreSQL via libpq.
// WHY: Handles all database operations for products.
// INPUT: Product data from controllers.
// OUTPUT: JSON responses with product information.
// ARCHITECTURE: Service (all SQL goes through the shared Database module)
// ============================================================

#include "ProductService.h"
#include "Database.h"
#include "AuthService.h"
#include <random>
#include <sstream>
#include <iostream>
#include <cstdlib>
#include <mutex>

// ---------------------------------------------------------------------------
// Shared connection / lock (forwarders to the single process-wide Database)
// ---------------------------------------------------------------------------

std::recursive_mutex &ProductService::dbMutex()
{
    return Database::mutex();
}

PGconn *ProductService::getConnection()
{
    return Database::connection();
}

// Frees a PGresult after use (prevents memory leaks).
void ProductService::freeResult(PGresult *res)
{
    if (res != nullptr)
    {
        PQclear(res);
    }
}

// Generates a unique ID (UUID) for new products.
// (Product IDs are public identifiers, not secrets — a non-crypto PRNG
// is sufficient here; auth tokens use a CSPRNG in AuthService.)
std::string ProductService::generateUUID()
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

// Looks up owner_id for a product. Returns 1 = found, 0 = not found,
// -1 = database error. Caller must hold Database::mutex().
static int sri_lookupOwner(const std::string &id, std::string &ownerOut)
{
    std::string err;
    PGresult *res = Database::exec("SELECT owner_id FROM products WHERE id = $1",
                                   {id}, err);
    if (res == nullptr)
    {
        return -1;
    }
    int rc = 0;
    if (PQntuples(res) > 0)
    {
        ownerOut = PQgetvalue(res, 0, 0);
        rc = 1;
    }
    PQclear(res);
    return rc;
}

// Shared ownership rule: admins may touch any product; sellers only their
// own; rows with an empty owner_id (created before ownership existed)
// remain editable by any authenticated seller/admin.
static bool sri_mayModify(const std::string &owner, const std::string &actorId,
                          const std::string &actorRole)
{
    if (actorRole == "admin") return true;
    if (owner.empty()) return true;
    return !actorId.empty() && owner == actorId;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

// Initializes the full schema (Database::initialize) and seeds demo users
// (AuthService::initialize). Keeps the "Connected to PostgreSQL" log line
// that start.bat/dev.bat grep for.
bool ProductService::initializeDatabase()
{
    std::string err;
    if (!Database::initialize(err))
    {
        LOG_ERROR << "PostgreSQL initialization failed: " << err;
        return false;
    }
    // Seed admin/customer/seller demo accounts when the users table is empty.
    AuthService::initialize();
    LOG_INFO << "Connected to PostgreSQL: sri_mart";
    return true;
}

// ---------------------------------------------------------------------------
// Reads
// ---------------------------------------------------------------------------

// Returns products from PostgreSQL with LIMIT/OFFSET pagination.
Json::Value ProductService::getAllProducts(int limit, int offset)
{
    std::lock_guard<std::recursive_mutex> lock(dbMutex());
    if (limit <= 0) limit = 100;
    if (limit > 500) limit = 500;
    if (offset < 0) offset = 0;
    Json::Value result(Json::arrayValue);

    std::string lim = std::to_string(limit);
    std::string off = std::to_string(offset);
    std::string err;
    PGresult *res = Database::exec(
        "SELECT id, name, description, price, stock, "
        "created_at::text, updated_at::text FROM products "
        "ORDER BY created_at DESC LIMIT $1::integer OFFSET $2::integer",
        {lim, off}, err);
    if (res == nullptr)
    {
        LOG_ERROR << "Get products failed: " << err;
        return result;
    }

    int rows = PQntuples(res);
    for (int i = 0; i < rows; i++)
    {
        Json::Value item;
        item["id"] = PQgetvalue(res, i, 0);
        item["name"] = PQgetvalue(res, i, 1);
        item["description"] = PQgetvalue(res, i, 2);
        try { item["price"] = std::stod(PQgetvalue(res, i, 3)); }
        catch (...) { item["price"] = 0.0; }
        try { item["stock"] = std::stoi(PQgetvalue(res, i, 4)); }
        catch (...) { item["stock"] = 0; }
        item["created_at"] = PQgetvalue(res, i, 5);
        item["updated_at"] = PQgetvalue(res, i, 6);
        result.append(item);
    }

    freeResult(res);
    return result;
}

// Finds one product by its ID in the PostgreSQL database.
Json::Value ProductService::getProductById(const std::string &id)
{
    std::lock_guard<std::recursive_mutex> lock(dbMutex());
    std::string err;
    PGresult *res = Database::exec(
        "SELECT id, name, description, price, stock, "
        "created_at::text, updated_at::text "
        "FROM products WHERE id = $1",
        {id}, err);
    if (res == nullptr)
    {
        LOG_ERROR << "Get product failed: " << err;
        return Json::Value();
    }

    if (PQntuples(res) > 0)
    {
        Json::Value item;
        item["id"] = PQgetvalue(res, 0, 0);
        item["name"] = PQgetvalue(res, 0, 1);
        item["description"] = PQgetvalue(res, 0, 2);
        try { item["price"] = std::stod(PQgetvalue(res, 0, 3)); }
        catch (...) { item["price"] = 0.0; }
        try { item["stock"] = std::stoi(PQgetvalue(res, 0, 4)); }
        catch (...) { item["stock"] = 0; }
        item["created_at"] = PQgetvalue(res, 0, 5);
        item["updated_at"] = PQgetvalue(res, 0, 6);
        freeResult(res);
        return item;
    }

    freeResult(res);
    return Json::Value();
}

// Products owned by ownerId (same shape as getAllProducts).
Json::Value ProductService::getProductsByOwner(const std::string &ownerId,
                                               int limit, int offset)
{
    std::lock_guard<std::recursive_mutex> lock(dbMutex());
    if (limit <= 0) limit = 100;
    if (limit > 500) limit = 500;
    if (offset < 0) offset = 0;
    Json::Value result(Json::arrayValue);

    std::string lim = std::to_string(limit);
    std::string off = std::to_string(offset);
    std::string err;
    PGresult *res = Database::exec(
        "SELECT id, name, description, price, stock, "
        "created_at::text, updated_at::text FROM products "
        "WHERE owner_id = $1 "
        "ORDER BY created_at DESC LIMIT $2::integer OFFSET $3::integer",
        {ownerId, lim, off}, err);
    if (res == nullptr)
    {
        LOG_ERROR << "Get owned products failed: " << err;
        return result;
    }

    int rows = PQntuples(res);
    for (int i = 0; i < rows; i++)
    {
        Json::Value item;
        item["id"] = PQgetvalue(res, i, 0);
        item["name"] = PQgetvalue(res, i, 1);
        item["description"] = PQgetvalue(res, i, 2);
        try { item["price"] = std::stod(PQgetvalue(res, i, 3)); }
        catch (...) { item["price"] = 0.0; }
        try { item["stock"] = std::stoi(PQgetvalue(res, i, 4)); }
        catch (...) { item["stock"] = 0; }
        item["created_at"] = PQgetvalue(res, i, 5);
        item["updated_at"] = PQgetvalue(res, i, 6);
        result.append(item);
    }

    freeResult(res);
    return result;
}

// Case-insensitive SQL search: ILIKE + price range + pagination.
// minPrice/maxPrice < 0 means "no bound".
Json::Value ProductService::searchProducts(const std::string &query, double minPrice, double maxPrice,
                                           int limit, int offset)
{
    std::lock_guard<std::recursive_mutex> lock(dbMutex());
    if (limit <= 0) limit = 100;
    if (limit > 500) limit = 500;
    if (offset < 0) offset = 0;
    Json::Value result(Json::arrayValue);

    std::string q = query.size() > 200 ? query.substr(0, 200) : query;
    std::string minS = std::to_string(minPrice);
    std::string maxS = std::to_string(maxPrice);
    std::string limS = std::to_string(limit);
    std::string offS = std::to_string(offset);
    std::string err;
    PGresult *res = Database::exec(
        "SELECT id, name, description, price, stock, "
        "created_at::text, updated_at::text FROM products "
        "WHERE ($1 = '' OR name ILIKE '%' || $1 || '%') "
        "AND ($2::double precision < 0 OR price >= $2::decimal) "
        "AND ($3::double precision < 0 OR price <= $3::decimal) "
        "ORDER BY created_at DESC LIMIT $4::integer OFFSET $5::integer",
        {q, minS, maxS, limS, offS}, err);
    if (res == nullptr)
    {
        LOG_ERROR << "Search products failed: " << err;
        return result;
    }
    int rows = PQntuples(res);
    for (int i = 0; i < rows; i++)
    {
        Json::Value item;
        item["id"] = PQgetvalue(res, i, 0);
        item["name"] = PQgetvalue(res, i, 1);
        item["description"] = PQgetvalue(res, i, 2);
        try { item["price"] = std::stod(PQgetvalue(res, i, 3)); }
        catch (...) { item["price"] = 0.0; }
        try { item["stock"] = std::stoi(PQgetvalue(res, i, 4)); }
        catch (...) { item["stock"] = 0; }
        item["created_at"] = PQgetvalue(res, i, 5);
        item["updated_at"] = PQgetvalue(res, i, 6);
        result.append(item);
    }
    freeResult(res);
    return result;
}

// Returns SELECT COUNT(*) for admin stats endpoints.
int ProductService::countProducts()
{
    std::lock_guard<std::recursive_mutex> lock(dbMutex());
    std::string err;
    PGresult *res = Database::exec("SELECT COUNT(*) FROM products", {}, err);
    if (res == nullptr || PQntuples(res) == 0)
    {
        freeResult(res);
        return 0;
    }
    int count = 0;
    try { count = std::stoi(PQgetvalue(res, 0, 0)); } catch (...) { count = 0; }
    freeResult(res);
    return count;
}

// Returns how many products are owned by ownerId (seller stats).
int ProductService::countProductsOwnedBy(const std::string &ownerId)
{
    std::lock_guard<std::recursive_mutex> lock(dbMutex());
    std::string err;
    PGresult *res = Database::exec(
        "SELECT COUNT(*) FROM products WHERE owner_id = $1", {ownerId}, err);
    if (res == nullptr || PQntuples(res) == 0)
    {
        freeResult(res);
        return 0;
    }
    int count = 0;
    try { count = std::stoi(PQgetvalue(res, 0, 0)); } catch (...) { count = 0; }
    freeResult(res);
    return count;
}

// Every product with its lifetime sales (units, revenue) — admin analysis.
Json::Value ProductService::getProductSales()
{
    std::lock_guard<std::recursive_mutex> lock(dbMutex());
    std::string err;
    Json::Value result(Json::arrayValue);
    PGresult *res = Database::exec(
        "SELECT p.id, p.name, p.description, p.price, p.stock, p.owner_id, "
        "p.created_at::text, p.updated_at::text, "
        "COALESCE(s.units, 0), COALESCE(s.rev, 0) "
        "FROM products p "
        "LEFT JOIN (SELECT product_id, SUM(quantity) AS units, "
        "                  SUM(price * quantity) AS rev "
        "           FROM order_items GROUP BY product_id) s "
        "  ON s.product_id = p.id "
        "ORDER BY p.created_at DESC, p.id",
        {}, err);
    if (res == nullptr)
    {
        LOG_ERROR << "Get product sales failed: " << err;
        return result;
    }

    int rows = PQntuples(res);
    for (int i = 0; i < rows; i++)
    {
        Json::Value item;
        item["id"] = PQgetvalue(res, i, 0);
        item["name"] = PQgetvalue(res, i, 1);
        item["description"] = PQgetvalue(res, i, 2);
        try { item["price"] = std::stod(PQgetvalue(res, i, 3)); }
        catch (...) { item["price"] = 0.0; }
        try { item["stock"] = std::stoi(PQgetvalue(res, i, 4)); }
        catch (...) { item["stock"] = 0; }
        item["owner_id"] = PQgetvalue(res, i, 5);
        item["created_at"] = PQgetvalue(res, i, 6);
        item["updated_at"] = PQgetvalue(res, i, 7);
        try { item["units_sold"] = std::stoi(PQgetvalue(res, i, 8)); }
        catch (...) { item["units_sold"] = 0; }
        try { item["revenue"] = std::stod(PQgetvalue(res, i, 9)); }
        catch (...) { item["revenue"] = 0.0; }
        result.append(item);
    }

    freeResult(res);
    return result;
}

// Same sales join as getProductSales, scoped to one owner (seller page).
Json::Value ProductService::getProductSalesByOwner(const std::string &ownerId)
{
    std::lock_guard<std::recursive_mutex> lock(dbMutex());
    std::string err;
    Json::Value result(Json::arrayValue);
    PGresult *res = Database::exec(
        "SELECT p.id, p.name, p.description, p.price, p.stock, p.owner_id, "
        "p.created_at::text, p.updated_at::text, "
        "COALESCE(s.units, 0), COALESCE(s.rev, 0) "
        "FROM products p "
        "LEFT JOIN (SELECT product_id, SUM(quantity) AS units, "
        "                  SUM(price * quantity) AS rev "
        "           FROM order_items GROUP BY product_id) s "
        "  ON s.product_id = p.id "
        "WHERE p.owner_id = $1 "
        "ORDER BY p.created_at DESC, p.id",
        {ownerId}, err);
    if (res == nullptr)
    {
        LOG_ERROR << "Get product sales by owner failed: " << err;
        return result;
    }

    int rows = PQntuples(res);
    for (int i = 0; i < rows; i++)
    {
        Json::Value item;
        item["id"] = PQgetvalue(res, i, 0);
        item["name"] = PQgetvalue(res, i, 1);
        item["description"] = PQgetvalue(res, i, 2);
        try { item["price"] = std::stod(PQgetvalue(res, i, 3)); }
        catch (...) { item["price"] = 0.0; }
        try { item["stock"] = std::stoi(PQgetvalue(res, i, 4)); }
        catch (...) { item["stock"] = 0; }
        item["owner_id"] = PQgetvalue(res, i, 5);
        item["created_at"] = PQgetvalue(res, i, 6);
        item["updated_at"] = PQgetvalue(res, i, 7);
        try { item["units_sold"] = std::stoi(PQgetvalue(res, i, 8)); }
        catch (...) { item["units_sold"] = 0; }
        try { item["revenue"] = std::stod(PQgetvalue(res, i, 9)); }
        catch (...) { item["revenue"] = 0.0; }
        result.append(item);
    }

    freeResult(res);
    return result;
}

// ---------------------------------------------------------------------------
// Writes
// ---------------------------------------------------------------------------

// Creates a new product owned by ownerId.
Json::Value ProductService::createProduct(const Json::Value &productData,
                                          const std::string &ownerId)
{
    std::lock_guard<std::recursive_mutex> lock(dbMutex());

    std::string name = productData.get("name", "").asString();
    if (name.empty() || name.size() > 255)
    {
        Json::Value error;
        error["error"] = "Name is required (max 255 chars)";
        return error;
    }
    double priceVal = productData.get("price", -1.0).asDouble();
    int stockVal = productData.get("stock", 0).asInt();
    if (priceVal < 0 || priceVal > 100000000)
    {
        Json::Value error;
        error["error"] = "Price must be >= 0";
        return error;
    }
    if (stockVal < 0 || stockVal > 1000000)
    {
        Json::Value error;
        error["error"] = "Stock must be >= 0";
        return error;
    }

    std::string description = productData.get("description", "").asString();
    if (description.size() > 5000)
    {
        Json::Value error;
        error["error"] = "Description too long (max 5000 chars)";
        return error;
    }

    std::string id = generateUUID();
    std::string price = std::to_string(priceVal);
    std::string stock = std::to_string(stockVal);

    std::string err;
    PGresult *res = Database::exec(
        "INSERT INTO products (id, name, description, price, stock, owner_id) "
        "VALUES ($1, $2, $3, $4::decimal, $5::integer, $6)",
        {id, name, description, price, stock, ownerId}, err);
    if (res == nullptr)
    {
        LOG_ERROR << "Create product failed: " << err;
        Json::Value error;
        error["error"] = "Failed to create product";
        return error;
    }
    freeResult(res);
    return getProductById(id);
}

// Updates an existing product (merge semantics) with ownership enforcement.
Json::Value ProductService::updateProduct(const std::string &id,
                                          const Json::Value &productData,
                                          const std::string &actorId,
                                          const std::string &actorRole)
{
    std::lock_guard<std::recursive_mutex> lock(dbMutex());

    // Ownership check before any write: seller may only edit own products,
    // admins may edit any, unowned legacy rows stay editable.
    std::string owner;
    int found = sri_lookupOwner(id, owner);
    if (found < 0)
    {
        Json::Value error;
        error["error"] = "Database connection failed";
        return error;
    }
    if (found == 0)
    {
        return Json::Value(); // not found -> controller maps to 404
    }
    if (!sri_mayModify(owner, actorId, actorRole))
    {
        Json::Value error;
        error["error"] = "Forbidden";
        return error;
    }

    // Merge semantics: fetch existing first, keep old values when fields
    // are absent from the request.
    Json::Value existing = getProductById(id);
    if (existing.isNull())
    {
        return Json::Value();
    }
    std::string name = productData.isMember("name") && !productData["name"].asString().empty()
        ? productData["name"].asString() : existing.get("name", "").asString();
    std::string description = productData.isMember("description")
        ? productData["description"].asString() : existing.get("description", "").asString();
    double priceVal = productData.isMember("price")
        ? productData["price"].asDouble() : existing.get("price", 0.0).asDouble();
    int stockVal = productData.isMember("stock")
        ? productData["stock"].asInt() : existing.get("stock", 0).asInt();
    if (name.empty() || name.size() > 255)
    {
        Json::Value error;
        error["error"] = "Name is required (max 255 chars)";
        return error;
    }
    if (priceVal < 0 || stockVal < 0)
    {
        Json::Value error;
        error["error"] = "Price and stock must be >= 0";
        return error;
    }
    if (description.size() > 5000)
    {
        Json::Value error;
        error["error"] = "Description too long (max 5000 chars)";
        return error;
    }
    std::string price = std::to_string(priceVal);
    std::string stock = std::to_string(stockVal);

    std::string err;
    PGresult *res = Database::exec(
        "UPDATE products SET name = $1, description = $2, "
        "price = $3::decimal, stock = $4::integer, updated_at = CURRENT_TIMESTAMP "
        "WHERE id = $5",
        {name, description, price, stock, id}, err);
    if (res == nullptr)
    {
        LOG_ERROR << "Update product failed: " << err;
        Json::Value error;
        error["error"] = "Failed to update product";
        return error;
    }

    freeResult(res);
    return getProductById(id);
}

// Deletes a product with ownership enforcement.
// err out: "Forbidden" / "Product not found" / other failure message.
bool ProductService::deleteProduct(const std::string &id,
                                   const std::string &actorId,
                                   const std::string &actorRole,
                                   std::string &err)
{
    std::lock_guard<std::recursive_mutex> lock(dbMutex());

    std::string owner;
    int found = sri_lookupOwner(id, owner);
    if (found < 0)
    {
        err = "Database connection failed";
        return false;
    }
    if (found == 0)
    {
        err = "Product not found";
        return false;
    }
    if (!sri_mayModify(owner, actorId, actorRole))
    {
        err = "Forbidden";
        return false;
    }

    std::string dbErr;
    PGresult *res = Database::exec("DELETE FROM products WHERE id = $1", {id}, dbErr);
    if (res == nullptr)
    {
        LOG_ERROR << "Delete product failed: " << dbErr;
        err = "Delete failed";
        return false;
    }

    bool deleted = (PQcmdTuples(res)[0] != '0');
    freeResult(res);
    if (!deleted)
    {
        err = "Product not found";
        return false;
    }
    err.clear();
    return true;
}

// ---------------------------------------------------------------------------
// Stock / checkout
// ---------------------------------------------------------------------------

// Atomically decrements stock when sufficient. Safe to call inside a
// caller-managed transaction (checkout) or standalone.
bool ProductService::decrementStock(const std::string &id, int qty, std::string &err)
{
    std::lock_guard<std::recursive_mutex> lock(dbMutex());
    if (qty <= 0 || qty > 99) { err = "Quantity must be 1-99"; return false; }
    // NOTE: qtyS must outlive the exec call (never bind temporaries).
    std::string qtyS = std::to_string(qty);
    // Single-statement atomic guard: only decrements when stock >= qty.
    std::string dbErr;
    PGresult *res = Database::exec(
        "UPDATE products SET stock = stock - $2::integer, updated_at = CURRENT_TIMESTAMP "
        "WHERE id = $1 AND stock >= $2::integer",
        {id, qtyS}, dbErr);
    if (res == nullptr)
    {
        err = std::string("Stock update failed: ") + dbErr;
        return false;
    }
    std::string tuples = PQcmdTuples(res);
    freeResult(res);
    if (tuples.empty() || tuples == "0")
    {
        // Distinguish missing product vs insufficient stock.
        Json::Value p = getProductById(id);
        err = p.isNull() ? "Product not found" : "Insufficient stock";
        return false;
    }
    return true;
}

// All-or-nothing checkout reservation under a single lock + transaction.
// Nested getProductById/decrementStock calls re-lock safely (recursive).
bool ProductService::checkoutItems(const std::vector<std::pair<std::string, int>> &items,
                                  double &total, std::string &err)
{
    std::lock_guard<std::recursive_mutex> lock(dbMutex());
    total = 0.0;
    if (items.empty()) { err = "Cart is empty"; return false; }
    if (items.size() > 100) { err = "Too many cart lines (max 100)"; return false; }
    PGconn *conn = getConnection();
    if (conn == nullptr) { err = "Database connection failed"; return false; }
    // Transaction control uses PQexec with constant strings only (no user
    // input), on the same shared connection guarded by Database::mutex().
    auto execOk = [&](const char *sql) -> bool {
        PGresult *r = PQexec(conn, sql);
        bool ok = (r != nullptr && PQresultStatus(r) == PGRES_COMMAND_OK);
        if (r) PQclear(r);
        return ok;
    };
    if (!execOk("BEGIN")) { err = "Checkout transaction failed to start"; return false; }
    for (auto &it : items)
    {
        const std::string &pid = it.first;
        int qty = it.second;
        if (qty <= 0 || qty > 99) { execOk("ROLLBACK"); err = "Invalid quantity"; return false; }
        Json::Value product = getProductById(pid);
        if (product.isNull()) { execOk("ROLLBACK"); err = "Product not found: " + pid; return false; }
        double price = product.get("price", 0.0).asDouble();
        std::string derr;
        if (!decrementStock(pid, qty, derr)) { execOk("ROLLBACK"); err = derr; return false; }
        total += price * qty;
    }
    if (!execOk("COMMIT")) { execOk("ROLLBACK"); err = "Checkout commit failed"; return false; }
    return true;
}
