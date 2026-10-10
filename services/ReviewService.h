// ============================================================
// MODULE: ReviewService
// PURPOSE: PostgreSQL-backed product reviews (reviews table).
// WHY: Ratings validated 1-5; one review per (product, user) upserted so
//      repeat submissions update instead of duplicating.
// INPUT: productId, userId, rating, comment.
// OUTPUT: JSON.
// ARCHITECTURE: Service
// ============================================================

#pragma once

#include <json/json.h>
#include <string>
#include <vector>
#include "../models/Review.h"

class ReviewService
{
public:
    static Json::Value getByProduct(const std::string &productId);
    static Json::Value addReview(const std::string &productId, const std::string &userId, int rating, const std::string &comment);

    // Seller view: reviews on products owned by ownerId
    // -> [{id, productId, productName, username, rating, comment, created_at}].
    static Json::Value getForOwner(const std::string &ownerId);

private:
    static std::string generateUUID();
};
