// ============================================================
// MODULE: AdminController
// PURPOSE: Seller stats + the full admin control plane.
// WHY: One admin gate guards every management endpoint; each
//      handler only aggregates data from the services layer.
// INPUT: Bearer token, JSON bodies, ?status= query, {id} path.
// OUTPUT: JSON stats/lists or 401/403/400/404.
// ARCHITECTURE: Controller
// ============================================================

#include "AdminController.h"
#include "../services/AuthService.h"
#include "../services/ProductService.h"
#include "../services/OrderService.h"

#include <map>
#include <set>
#include <string>

using namespace drogon;

static Json::Value authUserJson(const HttpRequestPtr &req)
{
    std::string h = req->getHeader("Authorization");
    std::string token = (h.empty() || h.find("Bearer ") != 0) ? "" : h.substr(7);
    if (token.empty()) return Json::Value();
    return AuthService::validateToken(token);
}

// Writes a JSON response, optionally with a non-200 status.
static void sri_sendJson(const std::function<void(const HttpResponsePtr &)> &callback,
                         const Json::Value &body, HttpStatusCode code = k200OK)
{
    auto resp = HttpResponse::newHttpJsonResponse(body);
    if (code != k200OK) resp->setStatusCode(code);
    callback(resp);
}

// Admin gate: true when the caller is a logged-in admin; otherwise the
// 401/403 response has already been written and the handler must return.
static bool sri_adminGate(const HttpRequestPtr &req,
                          std::function<void(const HttpResponsePtr &)> &callback,
                          Json::Value &userOut)
{
    userOut = authUserJson(req);
    if (userOut.isNull() || userOut.isMember("error"))
    {
        Json::Value e;
        e["error"] = "Authorization header with Bearer token required";
        sri_sendJson(callback, e, k401Unauthorized);
        return false;
    }
    if (userOut.get("role", "customer").asString() != "admin")
    {
        Json::Value e;
        e["error"] = "Admin only";
        sri_sendJson(callback, e, k403Forbidden);
        return false;
    }
    return true;
}

void AdminController::initRoutes()
{
    app().registerHandler("/api/v1/seller/stats", &AdminController::sellerStats, {Get});
    app().registerHandler("/api/v1/admin/stats", &AdminController::adminStats, {Get});
    app().registerHandler("/api/v1/admin/overview", &AdminController::adminOverview, {Get});
    app().registerHandler("/api/v1/admin/orders", &AdminController::adminOrders, {Get});
    app().registerHandler("/api/v1/admin/customers", &AdminController::adminCustomers, {Get});
    app().registerHandler("/api/v1/admin/sellers", &AdminController::adminSellers, {Get});
    app().registerHandler("/api/v1/admin/analytics", &AdminController::adminAnalytics, {Get});
    app().registerHandler("/api/v1/admin/users/{id}", &AdminController::adminUpdateUser, {Put});
    app().registerHandler("/api/v1/admin/users/{id}", &AdminController::adminDeleteUser, {Delete});
}

// ---------------------------------------------------------------------------
// Existing stats endpoints
// ---------------------------------------------------------------------------

void AdminController::sellerStats(const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    auto user = authUserJson(req);
    if (user.isNull() || user.isMember("error"))
    {
        Json::Value e; e["error"] = "Authorization header with Bearer token required";
        auto resp = HttpResponse::newHttpJsonResponse(e);
        resp->setStatusCode(k401Unauthorized); callback(resp); return;
    }
    // Fix: seller/admin only; counts are scoped to the requesting seller
    // (admins see the catalog total).
    std::string role = user.get("role", "customer").asString();
    if (role != "seller" && role != "admin")
    {
        Json::Value e; e["error"] = "Seller or admin only";
        auto resp = HttpResponse::newHttpJsonResponse(e);
        resp->setStatusCode(k403Forbidden); callback(resp); return;
    }
    Json::Value ok;
    ok["role"] = role;
    if (role == "admin")
        ok["productCount"] = ProductService::countProducts();
    else
        ok["productCount"] = ProductService::countProductsOwnedBy(user.get("id", "").asString());
    ok["note"] = "Counts reflect products owned by this seller";
    auto resp = HttpResponse::newHttpJsonResponse(ok);
    callback(resp);
}

void AdminController::adminStats(const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    auto user = authUserJson(req);
    if (user.isNull() || user.isMember("error"))
    {
        Json::Value e; e["error"] = "Authorization header with Bearer token required";
        auto resp = HttpResponse::newHttpJsonResponse(e);
        resp->setStatusCode(k401Unauthorized); callback(resp); return;
    }
    if (user.get("role", "customer").asString() != "admin")
    {
        Json::Value e; e["error"] = "Admin only";
        auto resp = HttpResponse::newHttpJsonResponse(e);
        resp->setStatusCode(k403Forbidden); callback(resp); return;
    }
    auto users = AuthService::getAllUsers();
    Json::Value ok;
    ok["userCount"] = (int)users.size();
    ok["productCount"] = ProductService::countProducts();
    auto resp = HttpResponse::newHttpJsonResponse(ok);
    callback(resp);
}

// ---------------------------------------------------------------------------
// Dashboard overview
// ---------------------------------------------------------------------------

// GET /api/v1/admin/overview
void AdminController::adminOverview(const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    Json::Value actor;
    if (!sri_adminGate(req, callback, actor)) return;

    // Users by role.
    Json::Value users = AuthService::getAllUsers();
    int customers = 0, sellers = 0, admins = 0;
    for (const auto &u : users)
    {
        std::string r = u.get("role", "customer").asString();
        if (r == "admin") admins++;
        else if (r == "seller") sellers++;
        else customers++;
    }

    // Catalog: counts, stock units, inventory value, stock alerts.
    Json::Value products = ProductService::getProductSales();
    int prodCount = 0, units = 0, lowStock = 0, outOfStock = 0;
    double inventoryValue = 0.0;
    for (const auto &p : products)
    {
        prodCount++;
        int st = p.get("stock", 0).asInt();
        double pr = p.get("price", 0.0).asDouble();
        units += st;
        inventoryValue += pr * st;
        if (st == 0) outOfStock++;
        else if (st <= 5) lowStock++;
    }

    // Orders: totals + revenue from per-status rollup.
    Json::Value byStatus = OrderService::getStatusCounts();
    int orderCount = 0;
    double gross = 0.0, active = 0.0;
    for (const auto &s : byStatus)
    {
        orderCount += s.get("count", 0).asInt();
        double rev = s.get("revenue", 0.0).asDouble();
        gross += rev;
        if (s.get("status", "").asString() != "cancelled") active += rev;
    }

    Json::Value ok;
    ok["users"]["total"] = (int)users.size();
    ok["users"]["customers"] = customers;
    ok["users"]["sellers"] = sellers;
    ok["users"]["admins"] = admins;
    ok["products"]["total"] = prodCount;
    ok["products"]["units"] = units;
    ok["products"]["lowStock"] = lowStock;
    ok["products"]["outOfStock"] = outOfStock;
    ok["products"]["inventoryValue"] = inventoryValue;
    ok["orders"]["total"] = orderCount;
    ok["orders"]["grossRevenue"] = gross;
    ok["orders"]["activeRevenue"] = active;
    ok["orders"]["byStatus"] = byStatus;
    sri_sendJson(callback, ok);
}

// ---------------------------------------------------------------------------
// Logistics: all orders with buyer names
// ---------------------------------------------------------------------------

// GET /api/v1/admin/orders?status=<optional filter>
void AdminController::adminOrders(const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    Json::Value actor;
    if (!sri_adminGate(req, callback, actor)) return;

    std::string status = req->getParameter("status");
    if (!status.empty() && status != "created" && status != "paid" &&
        status != "shipped" && status != "delivered" && status != "cancelled")
    {
        Json::Value e;
        e["error"] = "Invalid status filter (created, paid, shipped, delivered, cancelled)";
        sri_sendJson(callback, e, k400BadRequest);
        return;
    }
    sri_sendJson(callback, OrderService::getAllOrders(status));
}

// ---------------------------------------------------------------------------
// Buyers with order stats
// ---------------------------------------------------------------------------

// GET /api/v1/admin/customers
void AdminController::adminCustomers(const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    Json::Value actor;
    if (!sri_adminGate(req, callback, actor)) return;

    Json::Value users = AuthService::getAllUsers();
    Json::Value orders = OrderService::getAllOrders("");

    // Per-user rollup: {orderCount, totalSpent, lastOrder, lastStatus}.
    std::map<std::string, Json::Value> stats;
    for (const auto &o : orders)
    {
        std::string uid = o.get("userId", "").asString();
        Json::Value &s = stats[uid];
        if (!s.isObject())
        {
            s = Json::Value(Json::objectValue);
            s["orderCount"] = 0;
            s["totalSpent"] = 0.0;
            s["lastOrder"] = "";
            s["lastStatus"] = "";
        }
        s["orderCount"] = s["orderCount"].asInt() + 1;
        s["totalSpent"] = s["totalSpent"].asDouble() + o.get("total", 0.0).asDouble();
        // created_at is "YYYY-MM-DD HH24:MI:SS": lexicographic = chronological.
        std::string at = o.get("created_at", "").asString();
        if (at >= s["lastOrder"].asString())
        {
            s["lastOrder"] = at;
            s["lastStatus"] = o.get("status", "").asString();
        }
    }

    Json::Value result(Json::arrayValue);
    for (const auto &u : users)
    {
        if (u.get("role", "").asString() != "customer") continue;
        Json::Value row = u;
        auto it = stats.find(row.get("id", "").asString());
        if (it != stats.end())
        {
            row["orderCount"] = it->second["orderCount"];
            row["totalSpent"] = it->second["totalSpent"];
            row["lastOrder"] = it->second["lastOrder"];
            row["lastStatus"] = it->second["lastStatus"];
        }
        else
        {
            row["orderCount"] = 0;
            row["totalSpent"] = 0.0;
            row["lastOrder"] = "";
            row["lastStatus"] = "";
        }
        result.append(row);
    }
    sri_sendJson(callback, result);
}

// ---------------------------------------------------------------------------
// Sellers with catalog + sales aggregates
// ---------------------------------------------------------------------------

// GET /api/v1/admin/sellers
void AdminController::adminSellers(const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    Json::Value actor;
    if (!sri_adminGate(req, callback, actor)) return;

    Json::Value users = AuthService::getAllUsers();
    Json::Value products = ProductService::getProductSales();
    Json::Value sales = OrderService::getSalesByOwner();

    struct Agg
    {
        int prodCount = 0;
        int stock = 0;
        double inv = 0.0;
        int units = 0;
        double rev = 0.0;
        int orders = 0;
    };
    std::map<std::string, Agg> byOwner;
    // Catalog side: product counts, stock, inventory value.
    for (const auto &p : products)
    {
        Agg &a = byOwner[p.get("owner_id", "").asString()];
        a.prodCount++;
        int st = p.get("stock", 0).asInt();
        a.stock += st;
        a.inv += p.get("price", 0.0).asDouble() * st;
    }
    // Sales side (authoritative rollup from order_items snapshot).
    for (const auto &s : sales)
    {
        Agg &a = byOwner[s.get("ownerId", "").asString()];
        a.units = s.get("units", 0).asInt();
        a.rev = s.get("revenue", 0.0).asDouble();
        a.orders = s.get("orders", 0).asInt();
    }

    std::map<std::string, Json::Value> userById;
    for (const auto &u : users) userById[u.get("id", "").asString()] = u;

    // Owner universe: anyone who owns/owned something + all seller users.
    std::set<std::string> ownerIds;
    for (const auto &kv : byOwner) ownerIds.insert(kv.first);
    for (const auto &u : users)
        if (u.get("role", "").asString() == "seller")
            ownerIds.insert(u.get("id", "").asString());

    Json::Value result(Json::arrayValue);
    for (const std::string &oid : ownerIds)
    {
        Json::Value row;
        row["ownerId"] = oid;
        if (oid.empty())
        {
            row["username"] = "(unassigned)";
            row["role"] = "";
            row["email"] = "";
            row["created_at"] = "";
        }
        else
        {
            auto it = userById.find(oid);
            if (it == userById.end())
            {
                row["username"] = "(deleted user)";
                row["role"] = "";
                row["email"] = "";
                row["created_at"] = "";
            }
            else
            {
                row["username"] = it->second.get("username", "");
                row["role"] = it->second.get("role", "");
                row["email"] = it->second.get("email", "");
                row["created_at"] = it->second.get("created_at", "");
            }
        }
        const Agg &a = byOwner[oid];
        row["productCount"] = a.prodCount;
        row["stockUnits"] = a.stock;
        row["inventoryValue"] = a.inv;
        row["unitsSold"] = a.units;
        row["revenue"] = a.rev;
        row["orders"] = a.orders;
        result.append(row);
    }
    sri_sendJson(callback, result);
}

// ---------------------------------------------------------------------------
// Seller analysis (seller x product x price x sales)
// ---------------------------------------------------------------------------

// GET /api/v1/admin/analytics
void AdminController::adminAnalytics(const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    Json::Value actor;
    if (!sri_adminGate(req, callback, actor)) return;

    Json::Value users = AuthService::getAllUsers();
    Json::Value products = ProductService::getProductSales();
    Json::Value sales = OrderService::getSalesByOwner();
    Json::Value byStatus = OrderService::getStatusCounts();

    std::map<std::string, Json::Value> userById;
    for (const auto &u : users) userById[u.get("id", "").asString()] = u;

    // Per-owner catalog aggregation with price stats + top product.
    struct OwnerAgg
    {
        int prodCount = 0;
        int stock = 0;
        double inv = 0.0;
        double priceSum = 0.0;
        int priced = 0;
        double minP = -1.0;
        double maxP = -1.0;
        std::string topId, topName;
        double topRev = -1.0;
        int topUnits = 0;
        Json::Value productList = Json::Value(Json::arrayValue);
    };
    std::map<std::string, OwnerAgg> byOwner;
    for (const auto &p : products)
    {
        std::string oid = p.get("owner_id", "").asString();
        OwnerAgg &a = byOwner[oid];
        a.prodCount++;
        int st = p.get("stock", 0).asInt();
        double pr = p.get("price", 0.0).asDouble();
        double rev = p.get("revenue", 0.0).asDouble();
        a.stock += st;
        a.inv += pr * st;
        a.priceSum += pr;
        a.priced++;
        if (a.minP < 0 || pr < a.minP) a.minP = pr;
        if (pr > a.maxP) a.maxP = pr;
        if (rev > a.topRev)
        {
            a.topRev = rev;
            a.topId = p.get("id", "").asString();
            a.topName = p.get("name", "").asString();
            a.topUnits = p.get("units_sold", 0).asInt();
        }
        Json::Value pl;
        pl["id"] = p.get("id", "");
        pl["name"] = p.get("name", "");
        pl["price"] = pr;
        pl["stock"] = st;
        pl["units_sold"] = p.get("units_sold", 0);
        pl["revenue"] = rev;
        a.productList.append(pl);
    }

    std::map<std::string, Json::Value> salesByOwner;
    for (const auto &s : sales) salesByOwner[s.get("ownerId", "").asString()] = s;

    // Owner universe: catalog owners + anyone with sales + all sellers.
    std::set<std::string> ownerIds;
    for (const auto &kv : byOwner) ownerIds.insert(kv.first);
    for (const auto &kv : salesByOwner) ownerIds.insert(kv.first);
    for (const auto &u : users)
        if (u.get("role", "").asString() == "seller")
            ownerIds.insert(u.get("id", "").asString());

    Json::Value sellerRows(Json::arrayValue);
    for (const std::string &oid : ownerIds)
    {
        Json::Value row;
        row["ownerId"] = oid;
        if (oid.empty())
        {
            row["username"] = "(unassigned)";
            row["role"] = "";
            row["email"] = "";
        }
        else
        {
            auto it = userById.find(oid);
            if (it == userById.end())
            {
                row["username"] = "(deleted user)";
                row["role"] = "";
                row["email"] = "";
            }
            else
            {
                row["username"] = it->second.get("username", "");
                row["role"] = it->second.get("role", "");
                row["email"] = it->second.get("email", "");
            }
        }
        OwnerAgg &a = byOwner[oid];
        row["productCount"] = a.prodCount;
        row["stockUnits"] = a.stock;
        row["inventoryValue"] = a.inv;
        row["avgPrice"] = a.priced > 0 ? a.priceSum / a.priced : 0.0;
        row["minPrice"] = a.minP < 0 ? 0.0 : a.minP;
        row["maxPrice"] = a.maxP < 0 ? 0.0 : a.maxP;
        auto sit = salesByOwner.find(oid);
        row["unitsSold"] = sit == salesByOwner.end() ? 0 : sit->second.get("units", 0);
        row["revenue"] = sit == salesByOwner.end() ? 0.0 : sit->second.get("revenue", 0.0);
        row["orders"] = sit == salesByOwner.end() ? 0 : sit->second.get("orders", 0);
        row["topProductName"] = a.topName;
        row["topProductUnits"] = a.topUnits;
        row["topProductRevenue"] = a.topRev < 0 ? 0.0 : a.topRev;
        row["products"] = a.productList;
        sellerRows.append(row);
    }

    // Product performance list (with human owner names).
    Json::Value productRows(Json::arrayValue);
    double invValue = 0.0, priceSum = 0.0;
    int priced = 0, lowStock = 0, outOfStock = 0;
    double minP = -1.0, maxP = 0.0;
    for (const auto &p : products)
    {
        Json::Value row = p;
        std::string oid = p.get("owner_id", "").asString();
        if (oid.empty())
            row["ownerName"] = "(unassigned)";
        else
        {
            auto it = userById.find(oid);
            row["ownerName"] = it == userById.end()
                                   ? Json::Value("(deleted user)")
                                   : it->second.get("username", "");
        }
        productRows.append(row);

        double pr = p.get("price", 0.0).asDouble();
        int st = p.get("stock", 0).asInt();
        invValue += pr * st;
        priceSum += pr;
        priced++;
        if (minP < 0 || pr < minP) minP = pr;
        if (pr > maxP) maxP = pr;
        if (st == 0) outOfStock++;
        else if (st <= 5) lowStock++;
    }

    // Order rollup.
    int orderCount = 0;
    double gross = 0.0, active = 0.0;
    for (const auto &s : byStatus)
    {
        orderCount += s.get("count", 0).asInt();
        double rev = s.get("revenue", 0.0).asDouble();
        gross += rev;
        if (s.get("status", "").asString() != "cancelled") active += rev;
    }

    Json::Value ok;
    ok["sellers"] = sellerRows;
    ok["products"] = productRows;
    ok["prices"]["inventoryValue"] = invValue;
    ok["prices"]["avgPrice"] = priced > 0 ? priceSum / priced : 0.0;
    ok["prices"]["minPrice"] = minP < 0 ? 0.0 : minP;
    ok["prices"]["maxPrice"] = maxP;
    ok["prices"]["lowStock"] = lowStock;
    ok["prices"]["outOfStock"] = outOfStock;
    ok["orders"]["total"] = orderCount;
    ok["orders"]["grossRevenue"] = gross;
    ok["orders"]["activeRevenue"] = active;
    ok["orders"]["byStatus"] = byStatus;
    sri_sendJson(callback, ok);
}

// ---------------------------------------------------------------------------
// User management (edit / delete)
// ---------------------------------------------------------------------------

// PUT /api/v1/admin/users/{id}  body: {role?/username?/email?/password?}
void AdminController::adminUpdateUser(const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback, std::string targetId)
{
    Json::Value actor;
    if (!sri_adminGate(req, callback, actor)) return;

    auto json = req->getJsonObject();
    if (!json)
    {
        Json::Value e;
        e["error"] = "Request body must be a JSON object";
        sri_sendJson(callback, e, k400BadRequest);
        return;
    }
    Json::Value result =
        AuthService::updateUser(actor.get("id", "").asString(), targetId, *json);
    if (result.isMember("error"))
    {
        std::string msg = result["error"].asString();
        HttpStatusCode code = (msg == "User not found") ? k404NotFound : k400BadRequest;
        sri_sendJson(callback, result, code);
        return;
    }
    sri_sendJson(callback, result);
}

// DELETE /api/v1/admin/users/{id}
void AdminController::adminDeleteUser(const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback, std::string targetId)
{
    Json::Value actor;
    if (!sri_adminGate(req, callback, actor)) return;

    std::string err;
    if (AuthService::deleteUser(actor.get("id", "").asString(), targetId, err))
    {
        Json::Value ok;
        ok["message"] = "User deleted";
        ok["id"] = targetId;
        sri_sendJson(callback, ok);
        return;
    }
    Json::Value e;
    e["error"] = err;
    HttpStatusCode code = (err == "User not found") ? k404NotFound : k400BadRequest;
    sri_sendJson(callback, e, code);
}
