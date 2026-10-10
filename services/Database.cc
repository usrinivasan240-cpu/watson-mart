// ============================================================
// MODULE: Database
// PURPOSE: Shared PostgreSQL connection + schema bootstrap for ALL services.
// WHY: One libpq connection + one recursive mutex keeps every service
//      consistent and lets checkout run multi-statement transactions.
// INPUT: PGHOST/PGPORT/PGDATABASE/PGUSER/PGPASSWORD env vars.
// OUTPUT: Live PGconn*, parameterized exec helper, idempotent schema.
// ARCHITECTURE: Service (data access foundation)
// ============================================================

#include "Database.h"

#include <cstdlib>
#include <cstring>
#include <vector>

namespace Database
{

// ---------------------------------------------------------------------------
// Connection + mutex
// ---------------------------------------------------------------------------

std::recursive_mutex &mutex()
{
    static std::recursive_mutex m;
    return m;
}

// The single shared connection for the whole process.
static PGconn *g_conn = nullptr;

static std::string envOr(const char *key, const char *fallback)
{
    const char *v = std::getenv(key);
    return std::string(v != nullptr && *v != '\0' ? v : fallback);
}

// Same defaults as the original ProductService::sri_conninfo():
// localhost:5433 / sri_mart / postgres / postgres, 5s connect timeout.
static std::string conninfo()
{
    return "host=" + envOr("PGHOST", "localhost") +
           " port=" + envOr("PGPORT", "5433") +
           " dbname=" + envOr("PGDATABASE", "sri_mart") +
           " user=" + envOr("PGUSER", "postgres") +
           " password=" + envOr("PGPASSWORD", "postgres") +
           " connect_timeout=5";
}

PGconn *connection()
{
    std::lock_guard<std::recursive_mutex> lock(mutex());
    if (g_conn != nullptr && PQstatus(g_conn) == CONNECTION_OK)
        return g_conn;

    // Dead or never-established handle: drop it and retry once.
    if (g_conn != nullptr)
    {
        PQfinish(g_conn);
        g_conn = nullptr;
    }
    g_conn = PQconnectdb(conninfo().c_str());
    if (g_conn == nullptr || PQstatus(g_conn) != CONNECTION_OK)
    {
        if (g_conn != nullptr)
        {
            PQfinish(g_conn);
            g_conn = nullptr;
        }
        return nullptr;
    }
    return g_conn;
}

// ---------------------------------------------------------------------------
// Parameterized execution
// ---------------------------------------------------------------------------

PGresult *exec(const std::string &sql,
               const std::vector<std::string> &params,
               std::string &err)
{
    std::lock_guard<std::recursive_mutex> lock(mutex());
    PGconn *conn = connection();
    if (conn == nullptr)
    {
        err = "database connection unavailable";
        return nullptr;
    }

    std::vector<const char *> values;
    values.reserve(params.size());
    for (const std::string &p : params)
        values.push_back(p.c_str());

    // Single statement only (PQexecParams restriction); $1..$n placeholders.
    PGresult *res = PQexecParams(conn,
                                 sql.c_str(),
                                 static_cast<int>(params.size()),
                                 nullptr,
                                 params.empty() ? nullptr : values.data(),
                                 nullptr,
                                 nullptr,
                                 0);
    if (res == nullptr)
    {
        err = PQerrorMessage(conn);
        if (err.empty()) err = "database query failed";
        return nullptr;
    }
    if (PQresultStatus(res) != PGRES_TUPLES_OK &&
        PQresultStatus(res) != PGRES_COMMAND_OK)
    {
        // Failure: consume the result here so callers only ever see a
        // successful PGresult* (they PQclear that; nullptr => nothing to clear).
        err = PQresultErrorMessage(res);
        if (err.empty()) err = PQerrorMessage(conn);
        if (err.empty()) err = "database query failed";
        PQclear(res);
        return nullptr;
    }
    err.clear();
    return res;
}

bool cmdTuplesIs(PGresult *res, const char *expected)
{
    if (res == nullptr) return false;
    const char *tuples = PQcmdTuples(res);
    return tuples != nullptr && std::strcmp(tuples, expected) == 0;
}

// ---------------------------------------------------------------------------
// Schema bootstrap (idempotent; safe on every startup)
// ---------------------------------------------------------------------------

bool initialize(std::string &err)
{
    std::lock_guard<std::recursive_mutex> lock(mutex());

    static const char *kStatements[] = {
        // --- catalog -----------------------------------------------------
        "CREATE TABLE IF NOT EXISTS products ("
        " id VARCHAR(36) PRIMARY KEY,"
        " name VARCHAR(255) NOT NULL,"
        " description TEXT NOT NULL DEFAULT '',"
        " price DECIMAL(10,2) NOT NULL,"
        " stock INTEGER NOT NULL DEFAULT 0,"
        " owner_id VARCHAR(36) NOT NULL DEFAULT '',"
        " created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        " updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP)",

        // --- auth --------------------------------------------------------
        "CREATE TABLE IF NOT EXISTS users ("
        " id VARCHAR(36) PRIMARY KEY,"
        " username VARCHAR(32) NOT NULL UNIQUE,"
        " email VARCHAR(254) NOT NULL UNIQUE,"
        " password_hash TEXT NOT NULL,"
        " role VARCHAR(16) NOT NULL DEFAULT 'customer',"
        " created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP)",

        "CREATE TABLE IF NOT EXISTS sessions ("
        " token VARCHAR(64) PRIMARY KEY,"
        " user_id VARCHAR(36) NOT NULL,"
        " expires_at TIMESTAMP NOT NULL)",

        // --- shopping cart -----------------------------------------------
        "CREATE TABLE IF NOT EXISTS cart_items ("
        " user_id VARCHAR(36) NOT NULL,"
        " product_id VARCHAR(36) NOT NULL,"
        " quantity INTEGER NOT NULL DEFAULT 1,"
        " added_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        " PRIMARY KEY (user_id, product_id))",

        // --- orders ------------------------------------------------------
        "CREATE TABLE IF NOT EXISTS orders ("
        " id VARCHAR(36) PRIMARY KEY,"
        " user_id VARCHAR(36) NOT NULL,"
        " status VARCHAR(16) NOT NULL DEFAULT 'created',"
        " total DECIMAL(12,2) NOT NULL DEFAULT 0,"
        " item_count INTEGER NOT NULL DEFAULT 0,"
        " created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP)",

        "CREATE TABLE IF NOT EXISTS order_items ("
        " order_id VARCHAR(36) NOT NULL,"
        " product_id VARCHAR(36) NOT NULL,"
        " name VARCHAR(255) NOT NULL,"
        " price DECIMAL(10,2) NOT NULL,"
        " quantity INTEGER NOT NULL,"
        " owner_id VARCHAR(36) NOT NULL DEFAULT '',"
        " PRIMARY KEY (order_id, product_id))",

        // --- reviews -----------------------------------------------------
        "CREATE TABLE IF NOT EXISTS reviews ("
        " id VARCHAR(36) PRIMARY KEY,"
        " product_id VARCHAR(36) NOT NULL,"
        " user_id VARCHAR(36) NOT NULL,"
        " username VARCHAR(32) NOT NULL DEFAULT '',"
        " rating INTEGER NOT NULL,"
        " comment TEXT NOT NULL DEFAULT '',"
        " created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        " UNIQUE (product_id, user_id))",

        // --- migration for products tables created before owner_id -------
        "ALTER TABLE products ADD COLUMN IF NOT EXISTS owner_id"
        " VARCHAR(36) NOT NULL DEFAULT ''",

        // --- migration for order_items created before owner snapshot ------
        "ALTER TABLE order_items ADD COLUMN IF NOT EXISTS owner_id"
        " VARCHAR(36) NOT NULL DEFAULT ''",

        // Backfill owner from the products row (best effort; deleted
        // products stay ''). Only rows whose product still exists match,
        // so once backfilled this is a fast no-op on later startups.
        "UPDATE order_items SET owner_id = "
        " (SELECT p.owner_id FROM products p WHERE p.id = order_items.product_id)"
        " WHERE owner_id = '' AND EXISTS ("
        " SELECT 1 FROM products p2 WHERE p2.id = order_items.product_id)",
    };

    if (connection() == nullptr)
    {
        err = "cannot connect to PostgreSQL (check PGHOST/PGPORT/PGDATABASE/"
              "PGUSER/PGPASSWORD and that the server is running)";
        return false;
    }

    for (const char *sql : kStatements)
    {
        PGresult *res = exec(sql, {}, err);
        if (res == nullptr)
        {
            // exec consumed any failed result; err holds the message.
            if (err.empty()) err = "schema initialization failed";
            return false;
        }
        PQclear(res);
    }
    err.clear();
    return true;
}

} // namespace Database
