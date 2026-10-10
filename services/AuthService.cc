// ============================================================
// MODULE: AuthService
// PURPOSE: Implementation of user authentication logic.
// WHY: Handles user registration, login, and token validation.
// INPUT: User data from controllers.
// OUTPUT: JSON responses with user info or authentication tokens.
// ARCHITECTURE: Service (PostgreSQL-backed: users + sessions tables)
// ============================================================

#include "AuthService.h"
#include "Database.h"

#include <libpq-fe.h>
#include <cctype>
#include <chrono>
#include <iomanip>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#if __has_include(<openssl/evp.h>) && __has_include(<openssl/rand.h>)
#include <openssl/evp.h>
#include <openssl/rand.h>
#define SRI_HAVE_OPENSSL 1
#endif

// PBKDF2 iteration count for new hashes ("p1$" format).
static constexpr int kPbkdf2Iterations = 100000;

// ---------------------------------------------------------------------------
// Small crypto/utility helpers
// ---------------------------------------------------------------------------

static std::string sri_hexEncode(const unsigned char *data, size_t len)
{
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; i++)
        oss << std::setw(2) << static_cast<int>(data[i]);
    return oss.str();
}

static bool sri_hexDecode(const std::string &in, std::string &out)
{
    if (in.size() % 2 != 0) return false;
    auto hexval = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    out.clear();
    out.reserve(in.size() / 2);
    for (size_t i = 0; i < in.size(); i += 2)
    {
        int hi = hexval(in[i]);
        int lo = hexval(in[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out.push_back(static_cast<char>((hi << 4) | lo));
    }
    return true;
}

// Length-independent-where-possible constant-time comparison.
static bool sri_ctEquals(const std::string &a, const std::string &b)
{
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); i++)
        diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    return diff == 0;
}

// Builds the public user JSON (never includes the password hash).
// The users table has no updated_at column, so updated_at mirrors created_at.
static Json::Value sri_userJson(const std::string &id, const std::string &username,
                                const std::string &email, const std::string &role,
                                const std::string &createdAt)
{
    Json::Value json;
    json["id"] = id;
    json["username"] = username;
    json["email"] = email;
    json["role"] = role;
    json["created_at"] = createdAt;
    json["updated_at"] = createdAt;
    return json;
}

// ---------------------------------------------------------------------------
// ID + password + token primitives
// ---------------------------------------------------------------------------

// Generates a unique ID (UUID) for new users.
std::string AuthService::generateUUID()
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

// Password hash. Primary format: "p1$<iters>$<saltHex>$<hashHex>"
// (PBKDF2-HMAC-SHA256, per-user salt, 100k iterations).
// Fallback format "f1$<saltHex>$<chainHex>" only if OpenSSL is unavailable.
std::string AuthService::hashPassword(const std::string &password)
{
#if defined(SRI_HAVE_OPENSSL)
    unsigned char salt[16];
    if (RAND_bytes(salt, sizeof(salt)) == 1)
    {
        unsigned char dk[32];
        if (PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()),
                              salt, static_cast<int>(sizeof(salt)),
                              kPbkdf2Iterations,
                              EVP_sha256(), static_cast<int>(sizeof(dk)), dk) == 1)
        {
            return "p1$" + std::to_string(kPbkdf2Iterations) + "$" +
                   sri_hexEncode(salt, sizeof(salt)) + "$" +
                   sri_hexEncode(dk, sizeof(dk));
        }
    }
#endif
    // Fallback: salted, stretched std::hash chain (dev-grade, no OpenSSL).
    // NOTE: named saltHex (not salt) so this block compiles even when the
    // OpenSSL branch above declared its own `salt` array in the same scope.
    static thread_local std::mt19937 gen(std::random_device{}());
    static thread_local std::uniform_int_distribution<> dis(0, 255);
    unsigned char saltBuf[16];
    for (int i = 0; i < 16; i++)
        saltBuf[i] = static_cast<unsigned char>(dis(gen));
    std::string saltHex = sri_hexEncode(saltBuf, sizeof(saltBuf));
    std::hash<std::string> hasher;
    size_t h = hasher(saltHex + password);
    for (int i = 1; i < 10000; i++)
        h ^= hasher(std::to_string(h) + saltHex + password + std::to_string(i));
    std::ostringstream oss;
    oss << std::hex << h << hasher(saltHex + std::to_string(h));
    return "f1$" + saltHex + "$" + oss.str();
}

bool AuthService::verifyPassword(const std::string &stored, const std::string &plain)
{
    // --- p1$<iters>$<saltHex>$<hashHex> (PBKDF2) ---
    if (stored.rfind("p1$", 0) == 0)
    {
        size_t s1 = stored.find('$', 3);
        if (s1 == std::string::npos) return false;
        size_t s2 = stored.find('$', s1 + 1);
        if (s2 == std::string::npos) return false;
        int iters = 0;
        try
        {
            iters = std::stoi(stored.substr(3, s1 - 3));
        }
        catch (...)
        {
            return false;
        }
        if (iters < 1000 || iters > 5000000) return false;
        std::string salt;
        if (!sri_hexDecode(stored.substr(s1 + 1, s2 - s1 - 1), salt)) return false;
        std::string expected = stored.substr(s2 + 1);
#if defined(SRI_HAVE_OPENSSL)
        unsigned char dk[32];
        if (PKCS5_PBKDF2_HMAC(plain.c_str(), static_cast<int>(plain.size()),
                              reinterpret_cast<const unsigned char *>(salt.data()),
                              static_cast<int>(salt.size()),
                              iters, EVP_sha256(), static_cast<int>(sizeof(dk)), dk) != 1)
            return false;
        return sri_ctEquals(sri_hexEncode(dk, sizeof(dk)), expected);
#else
        return false; // hash was made with OpenSSL; cannot verify without it
#endif
    }

    // --- f1$<saltHex>$<chainHex> (no-OpenSSL fallback) ---
    if (stored.rfind("f1$", 0) == 0)
    {
        size_t s1 = stored.find('$', 3);
        if (s1 == std::string::npos) return false;
        std::string salt = stored.substr(3, s1 - 3);
        std::string expected = stored.substr(s1 + 1);
        std::hash<std::string> hasher;
        size_t h = hasher(salt + plain);
        for (int i = 1; i < 10000; i++)
            h ^= hasher(std::to_string(h) + salt + plain + std::to_string(i));
        std::ostringstream oss;
        oss << std::hex << h << hasher(salt + std::to_string(h));
        return sri_ctEquals(oss.str(), expected);
    }

    // Unknown/legacy formats fail closed (users table only ever holds p1/f1).
    return false;
}

// Generates a 64-hex-char authentication token from a CSPRNG.
std::string AuthService::generateToken()
{
#if defined(SRI_HAVE_OPENSSL)
    unsigned char buf[32];
    if (RAND_bytes(buf, sizeof(buf)) == 1)
        return sri_hexEncode(buf, sizeof(buf));
#endif
    // Fallback: direct random_device draws (best effort without OpenSSL).
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < 32; i++)
    {
        static thread_local std::random_device rd;
        oss << std::setw(2) << (static_cast<unsigned>(rd()) & 0xFFu);
    }
    return oss.str();
}

// ---------------------------------------------------------------------------
// Session maintenance
// ---------------------------------------------------------------------------

// Deletes expired session rows. Caller must hold Database::mutex();
// runs at most once per 5 minutes.
void AuthService::purgeExpiredSessions()
{
    static std::chrono::steady_clock::time_point lastPurge{};
    auto now = std::chrono::steady_clock::now();
    if (now - lastPurge < std::chrono::minutes(5)) return;
    lastPurge = now;
    std::string err;
    PGresult *res = Database::exec("DELETE FROM sessions WHERE expires_at < NOW()", {}, err);
    if (res != nullptr) PQclear(res);
}

// Seeds demo accounts when the users table is empty.
// Called once at startup after Database::initialize().
void AuthService::initialize()
{
    std::lock_guard<std::recursive_mutex> lock(Database::mutex());

    std::string err;
    PGresult *res = Database::exec("SELECT COUNT(*) FROM users", {}, err);
    if (res == nullptr) return; // DB/schema problem already reported at startup
    std::string count = PQntuples(res) > 0 ? PQgetvalue(res, 0, 0) : "0";
    PQclear(res);
    if (count != "0") return;

    struct Seed
    {
        const char *username;
        const char *email;
        const char *password;
        const char *role;
    };
    static const Seed kSeeds[] = {
        {"admin", "admin@srimart.com", "admin123", "admin"},
        {"customer", "customer@srimart.com", "customer123", "customer"},
        {"seller", "seller@srimart.com", "seller123", "seller"},
    };

    for (const Seed &s : kSeeds)
    {
        std::string hash = hashPassword(s.password);
        std::string e2;
        PGresult *ins = Database::exec(
            "INSERT INTO users (id, username, email, password_hash, role) "
            "VALUES ($1, $2, $3, $4, $5)",
            {generateUUID(), s.username, s.email, hash, s.role}, e2);
        if (ins != nullptr) PQclear(ins);
    }
}

// ---------------------------------------------------------------------------
// Input validation (file-local, no <regex> dependency)
// ---------------------------------------------------------------------------

static bool sri_validUsername(const std::string &u)
{
    if (u.size() < 3 || u.size() > 32) return false;
    for (char c : u)
    {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-')
            return false;
    }
    return true;
}

static bool sri_validEmail(const std::string &e)
{
    if (e.size() < 5 || e.size() > 254) return false;
    if (e.find(' ') != std::string::npos) return false;
    auto at = e.find('@');
    if (at == std::string::npos || at == 0 || at == e.size() - 1) return false;
    if (e.find('@', at + 1) != std::string::npos) return false;
    auto dot = e.find('.', at);
    if (dot == std::string::npos || dot == e.size() - 1) return false;
    return true;
}

// ---------------------------------------------------------------------------
// Login rate limiting (file-local): 5 fails / 10 min -> 5 min lock.
// Keyed by clientIp + "|" + username so one attacker IP cannot lock every
// account with that name for other IPs, and vice versa.
// Guarded by sri_attemptsMutex() only (never held while taking the DB lock).
// ---------------------------------------------------------------------------

struct SriLoginAttempt
{
    int fails = 0;
    std::chrono::steady_clock::time_point windowStart = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point lockUntil{};
    bool locked = false;
};

static std::map<std::string, SriLoginAttempt> &sri_loginAttempts()
{
    static std::map<std::string, SriLoginAttempt> attempts;
    return attempts;
}

static std::mutex &sri_attemptsMutex()
{
    static std::mutex m;
    return m;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

// Registers a new user with username, email, and password.
// Returns the created user (without password) or an error.
Json::Value AuthService::registerUser(const Json::Value &userData)
{
    // Validate required fields
    if (!userData.isMember("username") || !userData.isMember("email") || !userData.isMember("password"))
    {
        Json::Value error;
        error["error"] = "Username, email, and password are required";
        return error;
    }

    std::string username = userData["username"].asString();
    std::string email = userData["email"].asString();
    std::string password = userData["password"].asString();

    if (!sri_validUsername(username))
    {
        Json::Value error;
        error["error"] = "Username must be 3-32 chars (letters, digits, _ , -)";
        return error;
    }
    if (!sri_validEmail(email))
    {
        Json::Value error;
        error["error"] = "Invalid email address";
        return error;
    }
    if (password.size() < 4 || password.size() > 128)
    {
        Json::Value error;
        error["error"] = "Password must be 4-128 characters";
        return error;
    }

    // Only customer/seller allowed via self-register; admin accounts must be
    // seeded, never self-granted (privilege-escalation fix).
    std::string requestedRole = userData.get("role", "customer").asString();
    std::string role = (requestedRole == "seller") ? "seller" : "customer";

    // Hash outside the DB lock (PBKDF2 100k is CPU-bound).
    std::string hash = hashPassword(password);

    std::lock_guard<std::recursive_mutex> lock(Database::mutex());

    // Uniqueness check. Safe check-then-act: every writer shares this mutex.
    {
        std::string err;
        PGresult *res = Database::exec("SELECT id FROM users WHERE username = $1", {username}, err);
        if (res == nullptr)
        {
            Json::Value error;
            error["error"] = "Database unavailable";
            return error;
        }
        bool taken = PQntuples(res) > 0;
        PQclear(res);
        if (taken)
        {
            Json::Value error;
            error["error"] = "Username already exists";
            return error;
        }
    }
    {
        std::string err;
        PGresult *res = Database::exec("SELECT id FROM users WHERE email = $1", {email}, err);
        if (res == nullptr)
        {
            Json::Value error;
            error["error"] = "Database unavailable";
            return error;
        }
        bool taken = PQntuples(res) > 0;
        PQclear(res);
        if (taken)
        {
            Json::Value error;
            error["error"] = "Email already registered";
            return error;
        }
    }

    std::string id = generateUUID();
    std::string err;
    PGresult *res = Database::exec(
        "INSERT INTO users (id, username, email, password_hash, role) "
        "VALUES ($1, $2, $3, $4, $5) RETURNING created_at",
        {id, username, email, hash, role}, err);
    if (res == nullptr)
    {
        Json::Value error;
        error["error"] = "Database unavailable";
        return error;
    }
    std::string createdAt = PQntuples(res) > 0 ? PQgetvalue(res, 0, 0) : "";
    PQclear(res);

    return sri_userJson(id, username, email, role, createdAt);
}

// Logs in a user with username and password.
// Returns a 24h token on success, or an error.
// Rate limit: 5 failures per 10-min window locks ip|name for 5 min (429).
Json::Value AuthService::loginUser(const std::string &username, const std::string &password,
                                   const std::string &clientIp)
{
    using clock = std::chrono::steady_clock;
    const std::string attemptKey = clientIp + "|" + username;

    // --- rate-limit pre-check ---
    {
        std::lock_guard<std::mutex> lk(sri_attemptsMutex());
        auto now = clock::now();
        auto &att = sri_loginAttempts()[attemptKey];
        if (att.locked)
        {
            if (now < att.lockUntil)
            {
                auto waitSecs = std::chrono::duration_cast<std::chrono::seconds>(
                                    att.lockUntil - now)
                                    .count();
                Json::Value error;
                error["error"] = "Too many login attempts, try again later";
                error["locked"] = true;
                error["retry_after_seconds"] = static_cast<int>(waitSecs);
                return error;
            }
            att = SriLoginAttempt(); // lock expired, reset
        }
        else if (now - att.windowStart > std::chrono::minutes(10))
        {
            att.fails = 0;
            att.windowStart = now;
        }

        // Opportunistic prune so the attempts map cannot grow without bound.
        if (sri_loginAttempts().size() > 512)
        {
            for (auto it = sri_loginAttempts().begin(); it != sri_loginAttempts().end();)
            {
                bool stale = !it->second.locked &&
                             (now - it->second.windowStart > std::chrono::minutes(30));
                if (stale)
                    it = sri_loginAttempts().erase(it);
                else
                    ++it;
            }
        }
    }

    // --- fetch the account ---
    std::string userId, storedHash, uName, uEmail, uRole, uCreated;
    bool dbFailed = false;
    {
        std::lock_guard<std::recursive_mutex> lock(Database::mutex());
        purgeExpiredSessions();
        std::string err;
        PGresult *res = Database::exec(
            "SELECT id, username, email, password_hash, role, created_at "
            "FROM users WHERE username = $1",
            {username}, err);
        if (res == nullptr)
        {
            dbFailed = true;
        }
        else
        {
            if (PQntuples(res) > 0)
            {
                userId = PQgetvalue(res, 0, 0);
                uName = PQgetvalue(res, 0, 1);
                uEmail = PQgetvalue(res, 0, 2);
                storedHash = PQgetvalue(res, 0, 3);
                uRole = PQgetvalue(res, 0, 4);
                uCreated = PQgetvalue(res, 0, 5);
            }
            PQclear(res);
        }
    }
    if (dbFailed)
    {
        Json::Value error;
        error["error"] = "Database unavailable";
        return error;
    }

    // --- verify (no locks held: PBKDF2 is CPU-bound) ---
    bool ok = !storedHash.empty() && verifyPassword(storedHash, password);

    if (ok)
    {
        // Success: clear attempts for this key, issue a session row.
        {
            std::lock_guard<std::mutex> lk(sri_attemptsMutex());
            sri_loginAttempts().erase(attemptKey);
        }
        std::lock_guard<std::recursive_mutex> lock(Database::mutex());
        std::string token = generateToken();
        std::string err;
        PGresult *res = Database::exec(
            "INSERT INTO sessions (token, user_id, expires_at) "
            "VALUES ($1, $2, NOW() + INTERVAL '" +
            std::to_string(kTokenTtlHours) + " hours')",
            {token, userId}, err);
        if (res == nullptr)
        {
            Json::Value error;
            error["error"] = "Database unavailable";
            return error;
        }
        PQclear(res);

        Json::Value result;
        result["token"] = token;
        result["user"] = sri_userJson(userId, uName, uEmail, uRole, uCreated);
        result["expires_in_hours"] = kTokenTtlHours;
        return result;
    }

    // --- failure: record the attempt ---
    {
        std::lock_guard<std::mutex> lk(sri_attemptsMutex());
        auto now = clock::now();
        auto &att = sri_loginAttempts()[attemptKey];
        if (now - att.windowStart > std::chrono::minutes(10))
        {
            att.fails = 0;
            att.windowStart = now;
        }
        att.fails++;
        if (att.fails >= 5)
        {
            att.locked = true;
            att.lockUntil = now + std::chrono::minutes(5);
            Json::Value error;
            error["error"] = "Too many login attempts, try again in 5 minutes";
            error["locked"] = true;
            error["retry_after_seconds"] = 300;
            return error;
        }
    }
    Json::Value error;
    error["error"] = "Invalid username or password";
    return error;
}

// Validates an authentication token (session row + live TTL).
Json::Value AuthService::validateToken(const std::string &token)
{
    std::lock_guard<std::recursive_mutex> lock(Database::mutex());
    purgeExpiredSessions();

    if (!token.empty())
    {
        std::string err;
        PGresult *res = Database::exec(
            "SELECT u.id, u.username, u.email, u.role, u.created_at "
            "FROM sessions s JOIN users u ON u.id = s.user_id "
            "WHERE s.token = $1 AND s.expires_at > NOW()",
            {token}, err);
        if (res != nullptr && PQntuples(res) > 0)
        {
            Json::Value user = sri_userJson(PQgetvalue(res, 0, 0), PQgetvalue(res, 0, 1),
                                            PQgetvalue(res, 0, 2), PQgetvalue(res, 0, 3),
                                            PQgetvalue(res, 0, 4));
            PQclear(res);
            return user;
        }
        if (res != nullptr) PQclear(res);
    }

    Json::Value error;
    error["error"] = "Invalid or expired token";
    return error;
}

// Returns all users (without password hashes).
Json::Value AuthService::getAllUsers()
{
    std::lock_guard<std::recursive_mutex> lock(Database::mutex());
    Json::Value result(Json::arrayValue);
    std::string err;
    PGresult *res = Database::exec(
        "SELECT id, username, email, role, created_at FROM users "
        "ORDER BY created_at, username",
        {}, err);
    if (res == nullptr) return result; // DB unavailable -> empty list
    for (int i = 0; i < PQntuples(res); i++)
    {
        result.append(sri_userJson(PQgetvalue(res, i, 0), PQgetvalue(res, i, 1),
                                   PQgetvalue(res, i, 2), PQgetvalue(res, i, 3),
                                   PQgetvalue(res, i, 4)));
    }
    PQclear(res);
    return result;
}

// Finds a user by ID.
Json::Value AuthService::getUserById(const std::string &id)
{
    std::lock_guard<std::recursive_mutex> lock(Database::mutex());
    std::string err;
    PGresult *res = Database::exec(
        "SELECT id, username, email, role, created_at FROM users WHERE id = $1",
        {id}, err);
    if (res != nullptr && PQntuples(res) > 0)
    {
        Json::Value user = sri_userJson(PQgetvalue(res, 0, 0), PQgetvalue(res, 0, 1),
                                        PQgetvalue(res, 0, 2), PQgetvalue(res, 0, 3),
                                        PQgetvalue(res, 0, 4));
        PQclear(res);
        return user;
    }
    if (res != nullptr) PQclear(res);
    Json::Value error;
    error["error"] = "User not found";
    return error;
}

// Admin: edits any subset of role/username/email/password for one user.
// Validation mirrors registerUser; the controller has already proven the
// actor is an admin (actorId is kept for the audit trail / future use).
Json::Value AuthService::updateUser(const std::string &actorId,
                                    const std::string &targetId,
                                    const Json::Value &data)
{
    (void)actorId; // admin gate lives in AdminController

    if (targetId.empty())
    {
        Json::Value error;
        error["error"] = "User not found";
        return error;
    }

    // --- Validate the supplied fields before touching the database -----
    bool hasRole = data.isMember("role");
    std::string role = data.get("role", "").asString();
    if (hasRole && role != "customer" && role != "seller" && role != "admin")
    {
        Json::Value error;
        error["error"] = "Invalid role (customer, seller, admin)";
        return error;
    }
    bool hasUsername = data.isMember("username");
    std::string username = data.get("username", "").asString();
    if (hasUsername && !sri_validUsername(username))
    {
        Json::Value error;
        error["error"] = "Username must be 3-32 chars (letters, digits, _ , -)";
        return error;
    }
    bool hasEmail = data.isMember("email");
    std::string email = data.get("email", "").asString();
    if (hasEmail && !sri_validEmail(email))
    {
        Json::Value error;
        error["error"] = "Invalid email address";
        return error;
    }
    std::string password = data.get("password", "").asString();
    bool hasPassword = !password.empty(); // empty/absent = leave unchanged
    if (hasPassword && (password.size() < 4 || password.size() > 128))
    {
        Json::Value error;
        error["error"] = "Password must be 4-128 characters";
        return error;
    }
    if (!hasRole && !hasUsername && !hasEmail && !hasPassword)
    {
        Json::Value error;
        error["error"] = "Nothing to update";
        return error;
    }

    // PBKDF2 is CPU-bound: hash outside the DB lock.
    std::string newHash;
    if (hasPassword) newHash = hashPassword(password);

    std::lock_guard<std::recursive_mutex> lock(Database::mutex());
    std::string err;

    // Target must exist.
    {
        PGresult *res = Database::exec("SELECT id FROM users WHERE id = $1", {targetId}, err);
        if (res == nullptr)
        {
            Json::Value error;
            error["error"] = "Database unavailable";
            return error;
        }
        bool found = PQntuples(res) > 0;
        PQclear(res);
        if (!found)
        {
            Json::Value error;
            error["error"] = "User not found";
            return error;
        }
    }

    // Uniqueness against every OTHER user (check-then-act is safe: every
    // writer shares Database::mutex()).
    if (hasUsername)
    {
        PGresult *res = Database::exec(
            "SELECT id FROM users WHERE username = $1 AND id <> $2",
            {username, targetId}, err);
        if (res == nullptr)
        {
            Json::Value error;
            error["error"] = "Database unavailable";
            return error;
        }
        bool taken = PQntuples(res) > 0;
        PQclear(res);
        if (taken)
        {
            Json::Value error;
            error["error"] = "Username already exists";
            return error;
        }
    }
    if (hasEmail)
    {
        PGresult *res = Database::exec(
            "SELECT id FROM users WHERE email = $1 AND id <> $2",
            {email, targetId}, err);
        if (res == nullptr)
        {
            Json::Value error;
            error["error"] = "Database unavailable";
            return error;
        }
        bool taken = PQntuples(res) > 0;
        PQclear(res);
        if (taken)
        {
            Json::Value error;
            error["error"] = "Email already registered";
            return error;
        }
    }

    // Dynamic SET clause: only the provided fields are written.
    std::vector<std::string> params;
    std::string sql = "UPDATE users SET ";
    bool first = true;
    auto addSet = [&](const std::string &col, const std::string &val) {
        if (!first) sql += ", ";
        sql += col + " = $" + std::to_string(params.size() + 1);
        params.push_back(val);
        first = false;
    };
    if (hasRole) addSet("role", role);
    if (hasUsername) addSet("username", username);
    if (hasEmail) addSet("email", email);
    if (hasPassword) addSet("password_hash", newHash);
    sql += " WHERE id = $" + std::to_string(params.size() + 1);
    params.push_back(targetId);

    PGresult *upd = Database::exec(sql, params, err);
    if (upd == nullptr)
    {
        Json::Value error;
        // Surface UNIQUE violations cleanly if the mutex guard ever changes.
        if (err.find("unique") != std::string::npos ||
            err.find("duplicate") != std::string::npos)
            error["error"] = "Username or email already exists";
        else
            error["error"] = "Database unavailable";
        return error;
    }
    PQclear(upd);

    // Password changed -> sign the user out of every session.
    if (hasPassword)
    {
        PGresult *del = Database::exec("DELETE FROM sessions WHERE user_id = $1",
                                       {targetId}, err);
        if (del != nullptr) PQclear(del);
    }

    // Return the fresh row (public shape, never the hash).
    PGresult *res = Database::exec(
        "SELECT id, username, email, role, created_at FROM users WHERE id = $1",
        {targetId}, err);
    if (res != nullptr && PQntuples(res) > 0)
    {
        Json::Value user = sri_userJson(PQgetvalue(res, 0, 0), PQgetvalue(res, 0, 1),
                                        PQgetvalue(res, 0, 2), PQgetvalue(res, 0, 3),
                                        PQgetvalue(res, 0, 4));
        PQclear(res);
        return user;
    }
    if (res != nullptr) PQclear(res);
    Json::Value error;
    error["error"] = "Database unavailable";
    return error;
}

// Admin: deletes a user plus their sessions and cart rows.
// Orders (history) and products (catalog) are intentionally kept.
bool AuthService::deleteUser(const std::string &actorId,
                             const std::string &targetId,
                             std::string &err)
{
    if (targetId.empty())
    {
        err = "User not found";
        return false;
    }
    if (!actorId.empty() && targetId == actorId)
    {
        err = "Cannot delete your own account";
        return false;
    }
    std::lock_guard<std::recursive_mutex> lock(Database::mutex());

    // Target must exist.
    {
        PGresult *res = Database::exec("SELECT id FROM users WHERE id = $1", {targetId}, err);
        if (res == nullptr)
        {
            err = (err == "database connection unavailable")
                      ? std::string("Database unavailable")
                      : std::string("Database error");
            return false;
        }
        bool found = PQntuples(res) > 0;
        PQclear(res);
        if (!found)
        {
            err = "User not found";
            return false;
        }
    }

    PGresult *r1 = Database::exec("DELETE FROM sessions WHERE user_id = $1", {targetId}, err);
    if (r1 == nullptr)
    {
        err = "Database unavailable";
        return false;
    }
    PQclear(r1);
    PGresult *r2 = Database::exec("DELETE FROM cart_items WHERE user_id = $1", {targetId}, err);
    if (r2 == nullptr)
    {
        err = "Database unavailable";
        return false;
    }
    PQclear(r2);
    PGresult *r3 = Database::exec("DELETE FROM users WHERE id = $1", {targetId}, err);
    if (r3 == nullptr)
    {
        err = "Database unavailable";
        return false;
    }
    bool removed = Database::cmdTuplesIs(r3, "1");
    PQclear(r3);
    if (!removed)
    {
        err = "User not found";
        return false;
    }
    err.clear();
    return true;
}

// Logs out a token by deleting its session row.
bool AuthService::logoutToken(const std::string &token)
{
    if (token.empty()) return false;
    std::lock_guard<std::recursive_mutex> lock(Database::mutex());
    std::string err;
    PGresult *res = Database::exec("DELETE FROM sessions WHERE token = $1", {token}, err);
    if (res == nullptr) return false;
    bool removed = Database::cmdTuplesIs(res, "1");
    PQclear(res);
    return removed;
}

// Returns user_id for a token, or empty string if invalid/expired.
std::string AuthService::getUserIdFromToken(const std::string &token)
{
    if (token.empty()) return "";
    std::lock_guard<std::recursive_mutex> lock(Database::mutex());
    purgeExpiredSessions();
    std::string err;
    PGresult *res = Database::exec(
        "SELECT user_id FROM sessions WHERE token = $1 AND expires_at > NOW()",
        {token}, err);
    std::string userId;
    if (res != nullptr && PQntuples(res) > 0)
        userId = PQgetvalue(res, 0, 0);
    if (res != nullptr) PQclear(res);
    return userId;
}

// Returns role for a token, or "" if invalid/expired. Used for seller/admin gates.
std::string AuthService::getRoleFromToken(const std::string &token)
{
    if (token.empty()) return "";
    std::lock_guard<std::recursive_mutex> lock(Database::mutex());
    purgeExpiredSessions();
    std::string err;
    PGresult *res = Database::exec(
        "SELECT u.role FROM sessions s JOIN users u ON u.id = s.user_id "
        "WHERE s.token = $1 AND s.expires_at > NOW()",
        {token}, err);
    std::string role;
    if (res != nullptr && PQntuples(res) > 0)
        role = PQgetvalue(res, 0, 0);
    if (res != nullptr) PQclear(res);
    return role;
}
