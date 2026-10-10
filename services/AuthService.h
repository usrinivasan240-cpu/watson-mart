// ============================================================
// MODULE: AuthService
// PURPOSE: Contains business logic for user authentication.
// WHY: Controllers should call this service for login/register/token operations.
// INPUT: User data from controllers (username, email, password).
// OUTPUT: JSON responses with user info or authentication tokens.
// ARCHITECTURE: Service (PostgreSQL-backed: users + sessions tables)
// ============================================================

#pragma once

#include <drogon/drogon.h>
#include <json/json.h>
#include "../models/User.h"
#include <string>

// This class handles user registration, login, and token validation.
// Think of it as the "security guard" that checks who you are.
// All persistent state lives in PostgreSQL (users, sessions); the only
// in-memory state is the login rate-limit window (per IP+username).
class AuthService
{
public:
    // Seeds the demo accounts (admin/customer/seller) when the users table
    // is empty. Called once at startup, right after Database::initialize().
    static void initialize();

    // Registers a new user. Returns the user JSON or an error.
    static Json::Value registerUser(const Json::Value &userData);

    // Logs in a user. Returns a token on success, or an error.
    // clientIp is used only to key the rate limiter (avoids locking a
    // username for every caller when a single IP is brute-forcing).
    static Json::Value loginUser(const std::string &username, const std::string &password,
                                 const std::string &clientIp = "");

    // Validates a token and returns the user if valid.
    static Json::Value validateToken(const std::string &token);

    // Returns all users (admin use).
    static Json::Value getAllUsers();

    // Admin: edits a user (any subset of role/username/email/password).
    // Validates like register; returns the updated user JSON or
    // {"error": ...} ("User not found" -> 404, validation -> 400).
    // A password change revokes that user's sessions (forces re-login).
    static Json::Value updateUser(const std::string &actorId,
                                  const std::string &targetId,
                                  const Json::Value &data);

    // Admin: deletes a user plus their sessions and cart rows.
    // Orders and products are kept (order history / catalog integrity).
    // err out: "Cannot delete your own account" / "User not found" / DB error.
    static bool deleteUser(const std::string &actorId,
                           const std::string &targetId,
                           std::string &err);

    // Finds a user by ID.
    static Json::Value getUserById(const std::string &id);

    // Logs out a token. Returns true if token existed.
    static bool logoutToken(const std::string &token);

    // Returns user_id for a token, or empty string if invalid.
    static std::string getUserIdFromToken(const std::string &token);

    // Returns role ("admin"/"seller"/"customer") for a token, or "" if invalid.
    static std::string getRoleFromToken(const std::string &token);

private:
    // Generates a unique ID (UUID).
    static std::string generateUUID();

    // Password hashing: PBKDF2-HMAC-SHA256 (100k iterations) when OpenSSL is
    // available, stored as "p1$<iters>$<saltHex>$<hashHex>". Falls back to a
    // salted stretched std::hash chain ("f1$...") only if OpenSSL is absent.
    static std::string hashPassword(const std::string &password);
    static bool verifyPassword(const std::string &stored, const std::string &plain);

    // Generates a 64-hex-char authentication token from a CSPRNG (OpenSSL
    // RAND_bytes; random_device fallback when OpenSSL is unavailable).
    static std::string generateToken();

    // 24h token TTL (mirrored into the sessions INSERT interval).
    static constexpr int kTokenTtlHours = 24;

    // Deletes expired session rows. Caller must hold Database::mutex();
    // runs at most once per 5 minutes.
    static void purgeExpiredSessions();
};
