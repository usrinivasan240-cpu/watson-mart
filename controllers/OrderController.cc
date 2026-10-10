// ============================================================
// MODULE: OrderController
// PURPOSE: Implementation of order HTTP handlers.
// WHY: Checkout + list + status update.
// INPUT: Bearer token.
// OUTPUT: JSON.
// ARCHITECTURE: Controller
// ============================================================

#include "OrderController.h"
#include "../services/OrderService.h"
#include "../services/AuthService.h"

using namespace drogon;

static std::string getBearer(const HttpRequestPtr &req)
{
    std::string h = req->getHeader("Authorization");
    if (h.empty() || h.find("Bearer ") != 0) return "";
    return h.substr(7);
}

static std::string authUser(const HttpRequestPtr &req)
{
    std::string t = getBearer(req);
    return t.empty() ? "" : AuthService::getUserIdFromToken(t);
}

void OrderController::initRoutes()
{
    app().registerHandler("/api/v1/orders/checkout", &OrderController::checkout, {Post});
    app().registerHandler("/api/v1/orders", &OrderController::listOrders, {Get});
    app().registerHandler("/api/v1/orders/{id}", &OrderController::getOrder, {Get});
    app().registerHandler("/api/v1/orders/{id}/status", &OrderController::updateStatus, {Put});
}

void OrderController::checkout(const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    std::string userId = authUser(req);
    if (userId.empty())
    {
        Json::Value error; error["error"] = "Authorization header with Bearer token required";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k401Unauthorized); callback(resp); return;
    }
    auto result = OrderService::checkout(userId);
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

void OrderController::listOrders(const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    std::string userId = authUser(req);
    if (userId.empty())
    {
        Json::Value error; error["error"] = "Authorization header with Bearer token required";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k401Unauthorized); callback(resp); return;
    }
    auto resp = HttpResponse::newHttpJsonResponse(OrderService::getOrders(userId));
    callback(resp);
}

void OrderController::getOrder(const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback, std::string orderId)
{
    std::string userId = authUser(req);
    if (userId.empty())
    {
        Json::Value error; error["error"] = "Authorization header with Bearer token required";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k401Unauthorized); callback(resp); return;
    }
    auto order = OrderService::getOrderById(userId, orderId);
    if (order.isNull())
    {
        Json::Value error; error["error"] = "Order not found";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k404NotFound); callback(resp);
    }
    else
    {
        auto resp = HttpResponse::newHttpJsonResponse(order);
        callback(resp);
    }
}

void OrderController::updateStatus(const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback, std::string orderId)
{
    std::string userId = authUser(req);
    if (userId.empty())
    {
        Json::Value error; error["error"] = "Authorization header with Bearer token required";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k401Unauthorized); callback(resp); return;
    }
    // Fix: shipped/delivered transitions are seller/admin actions. Buyers
    // may only attempt the payment flow targets (paid / cancelled); the
    // service enforces valid current->next transitions for buyers, so a
    // buyer still cannot jump straight to shipped or refund a delivered bill.
    std::string role;
    {
        std::string h = req->getHeader("Authorization");
        std::string token = (h.empty() || h.find("Bearer ") != 0) ? "" : h.substr(7);
        role = AuthService::getRoleFromToken(token);
        if (role != "seller" && role != "admin")
        {
            std::string target = (req->getJsonObject() && req->getJsonObject()->isMember("status"))
                                     ? (*req->getJsonObject())["status"].asString()
                                     : "";
            bool buyerAllowed = (target == "paid" || target == "cancelled");
            if (!buyerAllowed)
            {
                Json::Value error; error["error"] = "Seller or admin only";
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(k403Forbidden); callback(resp); return;
            }
        }
    }
    auto json = req->getJsonObject();
    std::string status = (json && json->isMember("status")) ? (*json)["status"].asString() : "";
    auto result = OrderService::updateStatus(userId, role, orderId, status);
    if (result.isNull())
    {
        Json::Value error; error["error"] = "Order not found";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k404NotFound); callback(resp);
    }
    else if (result.isMember("error"))
    {
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->setStatusCode(k400BadRequest); callback(resp);
    }
    else
    {
        auto resp = HttpResponse::newHttpJsonResponse(result);
        callback(resp);
    }
}
