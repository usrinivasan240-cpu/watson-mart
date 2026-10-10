// ============================================================
// MODULE: ReviewService
// PURPOSE: Implementation of PostgreSQL-backed reviews (reviews table).
// WHY: Validates rating/comment, snapshots the reviewer's username from
//      users, and upserts on (product_id, user_id) to kill duplicates.
// ARCHITECTURE: Service
// ============================================================

#include "ReviewService.h"
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

std::string ReviewService::generateUUID()
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

// Builds a Review from a reviews row:
// id, product_id, user_id, username, rating, comment, created_at (text).
static Review sri_reviewRow(PGresult *res, int row)
{
    Review review;
    review.id = PQgetvalue(res, row, 0);
    review.productId = PQgetvalue(res, row, 1);
    review.userId = PQgetvalue(res, row, 2);
    review.username = PQgetvalue(res, row, 3);
    try { review.rating = std::stoi(PQgetvalue(res, row, 4)); }
    catch (...) { review.rating = 0; }
    review.comment = PQgetvalue(res, row, 5);
    review.created_at = PQgetvalue(res, row, 6);
    return review;
}

Json::Value ReviewService::getByProduct(const std::string &productId)
{
    std::lock_guard<std::recursive_mutex> lock(Database::mutex());
    std::string err;
    Json::Value result(Json::arrayValue);
    // created_at keeps the previous chronological (insertion) order.
    PGresult *res = sri_select(
        "SELECT id, product_id, user_id, username, rating, comment, "
        "to_char(created_at, 'YYYY-MM-DD HH24:MI:SS') AS created_at "
        "FROM reviews WHERE product_id = $1 ORDER BY created_at, id",
        {productId}, err);
    if (res == nullptr)
    {
        // Database unavailable: same shape as "no reviews yet".
        return result;
    }
    int rows = PQntuples(res);
    for (int i = 0; i < rows; i++)
    {
        result.append(sri_reviewRow(res, i).toJson());
    }
    PQclear(res);
    return result;
}

// All reviews on products owned by ownerId (seller feedback inbox).
Json::Value ReviewService::getForOwner(const std::string &ownerId)
{
    std::lock_guard<std::recursive_mutex> lock(Database::mutex());
    std::string err;
    Json::Value result(Json::arrayValue);
    PGresult *res = sri_select(
        "SELECT r.id, r.product_id, r.username, r.rating, r.comment, "
        "to_char(r.created_at, 'YYYY-MM-DD HH24:MI:SS') AS created_at, "
        "p.name "
        "FROM reviews r JOIN products p ON p.id = r.product_id "
        "WHERE p.owner_id = $1 ORDER BY r.created_at DESC, r.id",
        {ownerId}, err);
    if (res == nullptr)
    {
        return result;
    }
    int rows = PQntuples(res);
    for (int i = 0; i < rows; i++)
    {
        Json::Value row;
        row["id"] = PQgetvalue(res, i, 0);
        row["productId"] = PQgetvalue(res, i, 1);
        row["username"] = PQgetvalue(res, i, 2);
        try { row["rating"] = std::stoi(PQgetvalue(res, i, 3)); }
        catch (...) { row["rating"] = 0; }
        row["comment"] = PQgetvalue(res, i, 4);
        row["created_at"] = PQgetvalue(res, i, 5);
        row["productName"] = PQgetvalue(res, i, 6);
        result.append(row);
    }
    PQclear(res);
    return result;
}

Json::Value ReviewService::addReview(const std::string &productId, const std::string &userId, int rating, const std::string &comment)
{
    std::lock_guard<std::recursive_mutex> lock(Database::mutex());
    if (rating < 1 || rating > 5)
    {
        Json::Value e; e["error"] = "Rating must be 1-5";
        return e;
    }
    if (comment.size() > 1000)
    {
        Json::Value e; e["error"] = "Comment too long (max 1000 chars)";
        return e;
    }
    std::string err;

    // Product must exist (same rule as before), read straight from Postgres.
    PGresult *product = sri_select("SELECT id FROM products WHERE id = $1", {productId}, err);
    if (product == nullptr)
    {
        Json::Value e; e["error"] = err;
        return e;
    }
    if (PQntuples(product) == 0)
    {
        PQclear(product);
        Json::Value e; e["error"] = "Product not found";
        return e;
    }
    PQclear(product);

    // Snapshot the reviewer's display name ("" when it can't be read).
    std::string username;
    PGresult *user = sri_select("SELECT username FROM users WHERE id = $1", {userId}, err);
    if (user != nullptr)
    {
        if (PQntuples(user) > 0)
        {
            username = PQgetvalue(user, 0, 0);
        }
        PQclear(user);
    }

    // One review per (product, user): repeat submissions update in place.
    std::string reviewId = generateUUID();
    if (!sri_cmd(
            "INSERT INTO reviews (id, product_id, user_id, username, rating, comment) "
            "VALUES ($1, $2, $3, $4, $5::integer, $6) "
            "ON CONFLICT (product_id, user_id) DO UPDATE SET "
            "rating = EXCLUDED.rating, comment = EXCLUDED.comment, "
            "username = EXCLUDED.username",
            {reviewId, productId, userId, username, std::to_string(rating), comment}, err))
    {
        Json::Value e; e["error"] = err;
        return e;
    }

    // Read the row back so the response carries the stored id + created_at.
    PGresult *row = sri_select(
        "SELECT id, product_id, user_id, username, rating, comment, "
        "to_char(created_at, 'YYYY-MM-DD HH24:MI:SS') AS created_at "
        "FROM reviews WHERE product_id = $1 AND user_id = $2",
        {productId, userId}, err);
    if (row == nullptr)
    {
        Json::Value e; e["error"] = err;
        return e;
    }
    if (PQntuples(row) == 0)
    {
        PQclear(row);
        Json::Value e; e["error"] = "Database error";
        return e;
    }
    Json::Value result = sri_reviewRow(row, 0).toJson();
    PQclear(row);
    return result;
}
