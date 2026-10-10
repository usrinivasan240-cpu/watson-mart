// ============================================================
// MODULE: AuthController
// PURPOSE: Implementation of authentication HTTP request handlers.
// WHY: Defines how authentication API endpoints respond to requests.
// INPUT: HTTP requests with user data.
// OUTPUT: JSON responses with user info or authentication tokens.
// ARCHITECTURE: Controller
// ============================================================

#include "AuthController.h"
#include "../services/AuthService.h"

using namespace drogon;

// Registers all authentication routes with the Drogon server.
void AuthController::initRoutes()
{
    // POST /api/v1/auth/register -> creates a new user account
    app().registerHandler("/api/v1/auth/register",
        &AuthController::registerUser,
        {Post});

    // POST /api/v1/auth/login -> logs in a user and returns a token
    app().registerHandler("/api/v1/auth/login",
        &AuthController::loginUser,
        {Post});

    // GET /api/v1/auth/validate -> validates a token from the header
    app().registerHandler("/api/v1/auth/validate",
        &AuthController::validateToken,
        {Get});

    // GET /api/v1/auth/users -> returns all users
    app().registerHandler("/api/v1/auth/users",
        &AuthController::getUsers,
        {Get});

    // POST /api/v1/auth/logout -> logs out a Bearer token
    app().registerHandler("/api/v1/auth/logout",
        &AuthController::logoutUser,
        {Post});

    // PUT /api/v1/profile -> lets a logged-in user update their own
    // username/email/password (role changes are stripped server-side)
    app().registerHandler("/api/v1/profile",
        &AuthController::updateProfile,
        {Put});
}

// Registers a new user account.
// Expects JSON body with username, email, and password.
void AuthController::registerUser(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    auto json = req->getJsonObject();
    if (!json || !json->isMember("username") || !json->isMember("email") || !json->isMember("password"))
    {
        Json::Value error;
        error["error"] = "Username, email, and password are required";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k400BadRequest);
        callback(resp);
        return;
    }

    auto result = AuthService::registerUser(*json);
    if (result.isMember("error"))
    {
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->setStatusCode(k400BadRequest);
        callback(resp);
    }
    else
    {
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->setStatusCode(k201Created);
        callback(resp);
    }
}

// Logs in a user with username and password.
// Expects JSON body with username and password.
// Returns a token on success.
void AuthController::loginUser(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    auto json = req->getJsonObject();
    if (!json || !json->isMember("username") || !json->isMember("password"))
    {
        Json::Value error;
        error["error"] = "Username and password are required";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k400BadRequest);
        callback(resp);
        return;
    }

    std::string username = (*json)["username"].asString();
    std::string password = (*json)["password"].asString();

    auto result = AuthService::loginUser(username, password, req->peerAddr().toIp());
    if (result.isMember("error"))
    {
        auto resp = HttpResponse::newHttpJsonResponse(result);
        // Brute-force lockout returns 429 so clients can back off.
        resp->setStatusCode(result.isMember("locked") ? k429TooManyRequests : k401Unauthorized);
        callback(resp);
    }
    else
    {
        auto resp = HttpResponse::newHttpJsonResponse(result);
        callback(resp);
    }
}

// Validates an authentication token.
// Expects Authorization header with "Bearer <token>".
void AuthController::validateToken(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    std::string authHeader = req->getHeader("Authorization");
    if (authHeader.empty() || authHeader.find("Bearer ") != 0)
    {
        Json::Value error;
        error["error"] = "Authorization header with Bearer token required";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;
    }

    std::string token = authHeader.substr(7);
    auto result = AuthService::validateToken(token);
    if (result.isMember("error"))
    {
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
    }
    else
    {
        auto resp = HttpResponse::newHttpJsonResponse(result);
        callback(resp);
    }
}

// Returns all registered users (without passwords). Admin-only fix:
// previously public, enabling user enumeration.
void AuthController::getUsers(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    std::string h = req->getHeader("Authorization");
    std::string token = (h.empty() || h.find("Bearer ") != 0) ? "" : h.substr(7);
    if (token.empty() || AuthService::getRoleFromToken(token) != "admin")
    {
        Json::Value error;
        if (token.empty())
        {
            error["error"] = "Authorization header with Bearer token required";
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k401Unauthorized);
            callback(resp);
        }
        else
        {
            error["error"] = "Admin only";
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k403Forbidden);
            callback(resp);
        }
        return;
    }
    auto users = AuthService::getAllUsers();
    auto resp = HttpResponse::newHttpJsonResponse(users);
    callback(resp);
}

// Logs out a Bearer token. Client should also clear localStorage.
void AuthController::logoutUser(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    std::string authHeader = req->getHeader("Authorization");
    if (authHeader.empty() || authHeader.find("Bearer ") != 0)
    {
        Json::Value error;
        error["error"] = "Authorization header with Bearer token required";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;
    }
    std::string token = authHeader.substr(7);
    if (AuthService::logoutToken(token))
    {
        Json::Value ok;
        ok["message"] = "Logged out successfully";
        auto resp = HttpResponse::newHttpJsonResponse(ok);
        callback(resp);
    }
    else
    {
        Json::Value error;
        error["error"] = "Invalid or expired token";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
    }
}

// Self-service profile edit: username / email / password.
// Security fix: the "role" field is stripped before the update so a
// customer can never promote themselves to seller/admin from this route.
void AuthController::updateProfile(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    std::string authHeader = req->getHeader("Authorization");
    std::string token = (authHeader.empty() || authHeader.find("Bearer ") != 0)
                            ? "" : authHeader.substr(7);
    std::string userId = token.empty() ? "" : AuthService::getUserIdFromToken(token);
    if (userId.empty())
    {
        Json::Value error;
        error["error"] = "Authorization header with Bearer token required";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return;
    }

    auto json = req->getJsonObject();
    if (!json || (!json->isMember("username") && !json->isMember("email") &&
                  !json->isMember("password")))
    {
        Json::Value error;
        error["error"] = "Nothing to update (send username, email, or password)";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k400BadRequest);
        callback(resp);
        return;
    }

    Json::Value data = *json;
    data.removeMember("role"); // never allow self role change here
    Json::Value result = AuthService::updateUser(userId, userId, data);
    if (result.isMember("error"))
    {
        Json::Value resp2 = result;
        auto resp = HttpResponse::newHttpJsonResponse(resp2);
        resp->setStatusCode(k400BadRequest);
        callback(resp);
        return;
    }
    auto resp = HttpResponse::newHttpJsonResponse(result);
    callback(resp);
}
