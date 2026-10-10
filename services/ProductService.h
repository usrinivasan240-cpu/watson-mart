// ============================================================
// MODULE: ProductService
// PURPOSE: Contains business logic for product operations.
// WHY: Controllers should call this service for all product data operations.
// INPUT: Product data from controllers.
// OUTPUT: JSON responses with product information from PostgreSQL.
// ARCHITECTURE: Service (all SQL goes through the shared Database module)
// ============================================================

#pragma once

#include <drogon/drogon.h>
#include <json/json.h>
#include <libpq-fe.h>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

// This class handles all product operations (CRUD) using PostgreSQL
// through the shared Database connection/mutex (single libpq handle
// for the whole process, so checkout transactions span services safely).
class ProductService
{
public:
    // Initializes the full PostgreSQL schema (Database::initialize) and
    // seeds the demo users (AuthService::initialize). Connection reads
    // PGHOST/PGPORT/PGDATABASE/PGUSER/PGPASSWORD env vars.
    static bool initializeDatabase();

    // The shared PostgreSQL database connection (forwarder to
    // Database::connection(); kept public for checkout/transaction users).
    static PGconn *getConnection();

    // Returns products as a JSON array with pagination (defaults: 100/0).
    static Json::Value getAllProducts(int limit = 100, int offset = 0);

    // Returns one product by its ID, or empty JSON if not found.
    static Json::Value getProductById(const std::string &id);

    // Creates a new product owned by ownerId and returns it (or error JSON).
    static Json::Value createProduct(const Json::Value &productData,
                                     const std::string &ownerId);

    // Updates an existing product (merge semantics) and returns it.
    // Ownership: actors may only edit their own products; admins may edit
    // any. Products with an empty owner_id (legacy rows) are editable by
    // any seller/admin. Returns {"error":"Forbidden"} when not allowed,
    // null JSON when the product does not exist.
    static Json::Value updateProduct(const std::string &id,
                                     const Json::Value &productData,
                                     const std::string &actorId,
                                     const std::string &actorRole);

    // Deletes a product by ID. Returns true if deleted.
    // err out: "Forbidden" / "Product not found" / other failure message.
    static bool deleteProduct(const std::string &id,
                              const std::string &actorId,
                              const std::string &actorRole,
                              std::string &err);

    // Case-insensitive SQL search by name substring + price range + pagination.
    static Json::Value searchProducts(const std::string &query, double minPrice, double maxPrice,
                                      int limit = 100, int offset = 0);

    // Products owned by ownerId (same JSON shape as getAllProducts).
    static Json::Value getProductsByOwner(const std::string &ownerId,
                                          int limit = 100, int offset = 0);

    // Atomically decrements stock if sufficient. Returns true on success,
    // false with err message on insufficient stock / missing product / DB error.
    // Caller must hold the checkout transaction (BEGIN/COMMIT) for atomicity
    // across multiple lines; this helper works both inside and outside one.
    static bool decrementStock(const std::string &id, int qty, std::string &err);

    // Returns total product count (for admin stats, avoids LIMIT cap).
    static int countProducts();

    // Admin analytics: every product joined with its lifetime sales
    // (units, revenue) from order_items. Same base fields as
    // getAllProducts plus owner_id, units_sold, revenue.
    static Json::Value getProductSales();

    // Seller analytics: same shape as getProductSales, filtered to
    // products owned by ownerId.
    static Json::Value getProductSalesByOwner(const std::string &ownerId);

    // Returns how many products are owned by ownerId (seller stats).
    static int countProductsOwnedBy(const std::string &ownerId);

    // Recursive mutex serializing all libpq use on the shared connection
    // (forwarder to Database::mutex()).
    static std::recursive_mutex &dbMutex();

    // All-or-nothing stock reservation for checkout: verifies each
    // (productId, qty), decrements stock atomically, sums price*qty into
    // total. Returns false with err set on any failure (nothing decremented).
    static bool checkoutItems(const std::vector<std::pair<std::string, int>> &items,
                              double &total, std::string &err);

private:
    // Generates a unique ID (UUID) for new products.
    static std::string generateUUID();

    // Frees a PGresult after use.
    static void freeResult(PGresult *res);
};
