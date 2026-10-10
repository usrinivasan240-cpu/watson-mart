// ============================================================
// MODULE: ReviewController
// PURPOSE: Implementation of review handlers.
// ARCHITECTURE: Controller
// ============================================================

#include "ReviewController.h"
#include "../services/ReviewService.h"
#include "../services/AuthService.h"

using namespace drogon;

void ReviewController::initRoutes()
{
    app().registerHandler("/api/v1/products/{id}/reviews", &ReviewController::listReviews, {Get});
    app().registerHandler("/api/v1/products/{id}/reviews", &ReviewController::addReview, {Post});
}

void ReviewController::listReviews(const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback, std::string productId)
{
    auto resp = HttpResponse::newHttpJsonResponse(ReviewService::getByProduct(productId));
    callback(resp);
}

void ReviewController::addReview(const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback, std::string productId)
{
    std::string h = req->getHeader("Authorization");
    std::string token = (h.empty() || h.find("Bearer ") != 0) ? "" : h.substr(7);
    std::string userId = token.empty() ? "" : AuthService::getUserIdFromToken(token);
    if (userId.empty())
    {
        Json::Value e; e["error"] = "Authorization header with Bearer token required";
        auto resp = HttpResponse::newHttpJsonResponse(e);
        resp->setStatusCode(k401Unauthorized); callback(resp); return;
    }
    auto json = req->getJsonObject();
    int rating = (json && json->isMember("rating")) ? (*json)["rating"].asInt() : 5;
    std::string comment = (json && json->isMember("comment")) ? (*json)["comment"].asString() : "";
    auto result = ReviewService::addReview(productId, userId, rating, comment);
    if (result.isMember("error"))
    {
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->setStatusCode(k400BadRequest); callback(resp);
    }
    else
    {
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->setStatusCode(k201Created); callback(resp);
    }
}
