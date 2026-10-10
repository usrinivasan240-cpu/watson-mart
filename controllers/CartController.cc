// ============================================================
// MODULE: CartController
// PURPOSE: Implementation of cart HTTP handlers.
// WHY: Maps cart URLs to CartService; extracts user from token.
// INPUT: HTTP requests with Bearer token.
// OUTPUT: JSON responses.
// ARCHITECTURE: Controller
// ============================================================

#include "CartController.h"
#include "../services/CartService.h"
#include "../services/AuthService.h"

using namespace drogon;

// Extracts Bearer token, returns "" if missing.
static std::string getBearer(const HttpRequestPtr &req)
{
    std::string h = req->getHeader("Authorization");
    if (h.empty() || h.find("Bearer ") != 0)
    {
        return "";
    }
    return h.substr(7);
}

void CartController::initRoutes()
{
    // GET /api/v1/cart -> list my items
    app().registerHandler("/api/v1/cart",
        &CartController::getCart,
        {Get});
    // POST /api/v1/cart {productId,quantity} -> add
    app().registerHandler("/api/v1/cart",
        &CartController::addToCart,
        {Post});
    // DELETE /api/v1/cart/{productId} -> remove one line
    app().registerHandler("/api/v1/cart/{productId}",
        &CartController::removeFromCart,
        {Delete});
}

void CartController::getCart(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    std::string token = getBearer(req);
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
    auto cart = CartService::getCart(userId);
    auto resp = HttpResponse::newHttpJsonResponse(cart);
    callback(resp);
}

void CartController::addToCart(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    std::string token = getBearer(req);
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
    if (!json || !json->isMember("productId"))
    {
        Json::Value error;
        error["error"] = "productId and quantity are required";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k400BadRequest);
        callback(resp);
        return;
    }
    int qty = json->get("quantity", 1).asInt();
    auto result = CartService::addItem(userId, (*json)["productId"].asString(), qty);
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

void CartController::removeFromCart(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback,
    std::string productId)
{
    std::string token = getBearer(req);
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
    if (CartService::removeItem(userId, productId))
    {
        Json::Value ok;
        ok["message"] = "Removed from cart";
        auto resp = HttpResponse::newHttpJsonResponse(ok);
        callback(resp);
    }
    else
    {
        Json::Value error;
        error["error"] = "Item not found in cart";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k404NotFound);
        callback(resp);
    }
}
