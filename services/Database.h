// ============================================================
// MODULE: Database
// PURPOSE: Shared PostgreSQL connection + schema bootstrap for ALL services.
// WHY: Every service (products, auth, cart, orders, reviews) must talk to the
//      same single libpq connection, serialized by one recursive mutex, so
//      multi-statement transactions (checkout) can span services safely.
// INPUT: PGHOST/PGPORT/PGDATABASE/PGUSER/PGPASSWORD env vars.
// OUTPUT: A live PGconn* + helper functions for parameterized queries.
// ARCHITECTURE: Service (data access foundation)
// ============================================================

#pragma once

#include <libpq-fe.h>
#include <mutex>
#include <string>
#include <vector>

namespace Database
{
    // Recursive mutex serializing ALL libpq use on the shared connection.
    // Recursive so helpers can nest (e.g. checkout -> decrementStock -> get).
    // Every call that touches connection() MUST hold this lock.
    std::recursive_mutex &mutex();

    // The shared PostgreSQL connection. Reconnects automatically if the
    // handle is null or broken. Returns nullptr only if the server is
    // unreachable (callers must handle that gracefully).
    PGconn *connection();

    // Creates every table needed by the app (IF NOT EXISTS, idempotent):
    //   products (with owner_id), users, sessions, cart_items,
    //   orders, order_items, reviews.
    // Also adds missing columns to pre-existing tables (migration-safe).
    // Returns true on success; false with err set on failure.
    bool initialize(std::string &err);

    // Executes SQL with $1..$n placeholders via PQexecParams.
    // Caller MUST hold mutex(). Returns a PGresult* the caller must
    // PQclear(); check PQresultStatus(res) == PGRES_TUPLES_OK / PGRES_COMMAND_OK.
    // On any failure (including a null connection) sets err and returns
    // the failed result (or nullptr if no connection at all).
    PGresult *exec(const std::string &sql,
                   const std::vector<std::string> &params,
                   std::string &err);

    // True if the last command affected exactly `expected` rows.
    bool cmdTuplesIs(PGresult *res, const char *expected);
} // namespace Database
