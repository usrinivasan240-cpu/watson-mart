// ============================================================
// MODULE: AuthController
// PURPOSE: Handles HTTP requests related to user authentication.
// WHY: Keeps authentication API request handling separate from business logic.
// INPUT: HTTP requests for login, register, and token validation.
// OUTPUT: JSON responses with user info or authentication tokens.
// ARCHITECTURE: Controller
// ============================================================

#pragma once

#include <drogon/drogon.h>

// This class is the "front desk" for authentication requests.
// It receives login/register requests and passes them to the service layer.
class AuthController
{
public:
    // Registers a new user account.
    static void registerUser(const drogon::HttpRequestPtr &req,
                             std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    // Logs in a user and returns a token.
    static void loginUser(const drogon::HttpRequestPtr &req,
                          std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    // Validates a token and returns the user.
    static void validateToken(const drogon::HttpRequestPtr &req,
                              std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    // Returns all users (admin use).
    static void getUsers(const drogon::HttpRequestPtr &req,
                         std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    // Logs out a token.
    static void logoutUser(const drogon::HttpRequestPtr &req,
                           std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    // Lets a logged-in user edit their own profile (not their role).
    static void updateProfile(const drogon::HttpRequestPtr &req,
                              std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    // Registers all auth routes with the server.
    static void initRoutes();
};
