// ============================================================
// MODULE: Review
// PURPOSE: Product review (ratings + comments per product).
// WHY: Ratings 1-5, one review per (product, user), reviewer name shown.
// INPUT: productId, userId, rating 1-5, comment.
// OUTPUT: JSON review.
// ARCHITECTURE: Model
// ============================================================

#pragma once

#include <string>
#include <drogon/drogon.h>

struct Review
{
    std::string id;
    std::string productId;
    std::string userId;
    std::string username;   // Reviewer's display name (users.username)
    int rating; // 1-5
    std::string comment;
    std::string created_at; // Stored timestamp ("YYYY-MM-DD HH:MM:SS")

    Review() : rating(5) {}

    Json::Value toJson() const
    {
        Json::Value j;
        j["id"] = id;
        j["productId"] = productId;
        j["userId"] = userId;
        j["username"] = username;
        j["rating"] = rating;
        j["comment"] = comment;
        j["created_at"] = created_at;
        return j;
    }
};
