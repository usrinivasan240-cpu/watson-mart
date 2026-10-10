// ============================================================
// MODULE: ReviewController
// PURPOSE: HTTP layer for reviews (Week 6).
// WHY: GET list public; POST requires Bearer token.
// ARCHITECTURE: Controller
// ============================================================

#pragma once
#include <drogon/drogon.h>

class ReviewController
{
public:
    static void listReviews(const drogon::HttpRequestPtr &req,
        std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string productId);
    static void addReview(const drogon::HttpRequestPtr &req,
        std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string productId);
    static void initRoutes();
};
