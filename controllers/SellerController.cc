// ============================================================
// MODULE: SellerController
// PURPOSE: Implementation of the seller-scoped endpoints.
// WHY: Every handler is gated to seller/admin and scoped to the
//      caller's own ownerId so sellers never see each other's data.
// ARCHITECTURE: Controller
// ============================================================

#include "SellerController.h"
#include "../services/AuthService.h"
#include "../services/ProductService.h"
#include "../services/OrderService.h"
#include "../services/ReviewService.h"

#include <string>

using namespace drogon;

// Seller gate: true when the caller is a logged-in seller/admin; the
// 401/403 response is already written otherwise. userOut gets the
// caller's public user JSON (id used as ownerId).
static bool sri_sellerGate(const HttpRequestPtr &req,
                           std::function<void(const HttpResponsePtr &)> &callback,
                           Json::Value &userOut)
{
    std::string h = req->getHeader("Authorization");
    std::string token = (h.empty() || h.find("Bearer ") != 0) ? "" : h.substr(7);
    userOut = token.empty() ? Json::Value() : AuthService::validateToken(token);
    if (userOut.isNull() || userOut.isMember("error"))
    {
        Json::Value e;
        e["error"] = "Authorization header with Bearer token required";
        auto resp = HttpResponse::newHttpJsonResponse(e);
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        return false;
    }
    std::string role = userOut.get("role", "customer").asString();
    if (role != "seller" && role != "admin")
    {
        Json::Value e;
        e["error"] = "Seller or admin only";
        auto resp = HttpResponse::newHttpJsonResponse(e);
        resp->setStatusCode(k403Forbidden);
        callback(resp);
        return false;
    }
    return true;
}

void SellerController::initRoutes()
{
    app().registerHandler("/api/v1/seller/analytics", &SellerController::sellerAnalytics, {Get});
    app().registerHandler("/api/v1/seller/orders", &SellerController::sellerOrders, {Get});
    app().registerHandler("/api/v1/seller/reviews", &SellerController::sellerReviews, {Get});
}

// GET /api/v1/seller/analytics
// One payload for the whole Sales tab: catalog + sales + daily chart.
void SellerController::sellerAnalytics(const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    Json::Value user;
    if (!sri_sellerGate(req, callback, user)) return;
    std::string ownerId = user.get("id", "").asString();

    // Catalog with lifetime sales per product (my store only).
    Json::Value products = ProductService::getProductSalesByOwner(ownerId);

    // Overall rollup for this owner from the order_items snapshot.
    double revenue = 0.0;
    int units = 0, orders = 0;
    Json::Value sales = OrderService::getSalesByOwner();
    for (const auto &s : sales)
    {
        if (s.get("ownerId", "").asString() != ownerId) continue;
        units = s.get("units", 0).asInt();
        revenue = s.get("revenue", 0.0).asDouble();
        orders = s.get("orders", 0).asInt();
        break;
    }

    // Catalog stats + top products (top 5 by revenue, then units).
    int prodCount = 0, stockUnits = 0, lowStock = 0, outOfStock = 0, priced = 0;
    double inventoryValue = 0.0, priceSum = 0.0, minP = -1.0, maxP = 0.0;
    Json::Value top(Json::arrayValue);
    for (const auto &p : products)
    {
        prodCount++;
        int st = p.get("stock", 0).asInt();
        double pr = p.get("price", 0.0).asDouble();
        double rev = p.get("revenue", 0.0).asDouble();
        stockUnits += st;
        inventoryValue += pr * st;
        priceSum += pr;
        priced++;
        if (minP < 0 || pr < minP) minP = pr;
        if (pr > maxP) maxP = pr;
        if (st == 0) outOfStock++;
        else if (st <= 5) lowStock++;

        Json::Value tp;
        tp["id"] = p.get("id", "");
        tp["name"] = p.get("name", "");
        tp["price"] = pr;
        tp["stock"] = st;
        tp["units_sold"] = p.get("units_sold", 0);
        tp["revenue"] = rev;
        top.append(tp);
    }
    // Simple selection sort of top[] by revenue desc, units desc (tiny n).
    for (Json::ArrayIndex i = 0; i < top.size(); i++)
    {
        Json::ArrayIndex best = i;
        for (Json::ArrayIndex j = i + 1; j < top.size(); j++)
        {
            double rj = top[j].get("revenue", 0.0).asDouble();
            double rb = top[best].get("revenue", 0.0).asDouble();
            if (rj > rb ||
                (rj == rb && top[j].get("units_sold", 0).asInt() >
                                 top[best].get("units_sold", 0).asInt()))
                best = j;
        }
        if (best != i)
        {
            Json::Value tmp = top[i];
            top[i] = top[best];
            top[best] = tmp;
        }
    }
    if (top.size() > 5) top.resize(5);

    // Last 14 days daily sales for the bar chart.
    Json::Value daily = OrderService::getDailySalesForOwner(ownerId, 14);

    Json::Value ok;
    ok["revenue"] = revenue;
    ok["units"] = units;
    ok["orders"] = orders;
    ok["productCount"] = prodCount;
    ok["stockUnits"] = stockUnits;
    ok["inventoryValue"] = inventoryValue;
    ok["avgPrice"] = priced > 0 ? priceSum / priced : 0.0;
    ok["minPrice"] = minP < 0 ? 0.0 : minP;
    ok["maxPrice"] = maxP;
    ok["lowStock"] = lowStock;
    ok["outOfStock"] = outOfStock;
    ok["topProducts"] = top;
    ok["salesByDay"] = daily;
    ok["products"] = products;
    auto resp = HttpResponse::newHttpJsonResponse(ok);
    callback(resp);
}

// GET /api/v1/seller/orders — flat rows of MY items inside orders.
void SellerController::sellerOrders(const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    Json::Value user;
    if (!sri_sellerGate(req, callback, user)) return;
    auto resp = HttpResponse::newHttpJsonResponse(
        OrderService::getOrdersForOwner(user.get("id", "").asString()));
    callback(resp);
}

// GET /api/v1/seller/reviews — reviews on MY products.
void SellerController::sellerReviews(const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    Json::Value user;
    if (!sri_sellerGate(req, callback, user)) return;
    auto resp = HttpResponse::newHttpJsonResponse(
        ReviewService::getForOwner(user.get("id", "").asString()));
    callback(resp);
}
