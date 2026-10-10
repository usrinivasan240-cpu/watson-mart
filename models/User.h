// ============================================================
// MODULE: User
// PURPOSE: Represents user/account data in Sri Mart.
// WHY: The project needs user authentication so only logged-in users can manage products.
// INPUT: User information from registration or login requests.
// OUTPUT: User object that can be converted to/from JSON.
// ARCHITECTURE: Model
// ============================================================

#pragma once

#include <string>
#include <drogon/drogon.h>

// This struct defines what a user looks like.
// Think of it as a registration form: id, username, email, password, role, timestamps.
struct User
{
    std::string id;          // Unique identifier (UUID)
    std::string username;    // Login name (e.g., "admin")
    std::string email;       // Email address (e.g., "admin@srimart.com")
    std::string password;    // Hashed password (never store plain text)
    std::string role;        // User role: "admin" or "customer"
    std::string created_at;  // When the account was created
    std::string updated_at;  // When the account was last changed

    // Default constructor: creates an empty user.
    User() : role("customer") {}

    // Converts this user into a JSON object (excludes password for security).
    Json::Value toJson() const
    {
        Json::Value json;
        json["id"] = id;
        json["username"] = username;
        json["email"] = email;
        json["role"] = role;
        json["created_at"] = created_at;
        json["updated_at"] = updated_at;
        return json;
    }

    // Converts this user into a JSON object including password (for internal use only).
    Json::Value toFullJson() const
    {
        Json::Value json = toJson();
        json["password"] = password;
        return json;
    }

    // Creates a user from a JSON object received in a registration request.
    static User fromJson(const Json::Value &json)
    {
        User user;
        if (json.isMember("id")) user.id = json["id"].asString();
        if (json.isMember("username")) user.username = json["username"].asString();
        if (json.isMember("email")) user.email = json["email"].asString();
        if (json.isMember("password")) user.password = json["password"].asString();
        if (json.isMember("role")) user.role = json["role"].asString();
        if (json.isMember("created_at")) user.created_at = json["created_at"].asString();
        if (json.isMember("updated_at")) user.updated_at = json["updated_at"].asString();
        return user;
    }
};
