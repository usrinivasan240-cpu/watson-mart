// ============================================================
// MODULE: ProductController
// PURPOSE: Implementation of product HTTP request handlers.
// WHY: Defines how product API endpoints respond to requests.
// INPUT: HTTP requests with product data.
// OUTPUT: JSON responses with product information.
// ARCHITECTURE: Controller
// ============================================================

#include "ProductController.h"
#include "../services/ProductService.h"
#include "../services/AuthService.h"

using namespace drogon;

// Auth helpers (fix: product writes were public; now seller/admin only).
static std::string sri_bearer(const HttpRequestPtr &req)
{
    std::string h = req->getHeader("Authorization");
    if (h.empty() || h.find("Bearer ") != 0) return "";
    return h.substr(7);
}

// Returns "" if authorized, else error message. Sets HTTP code via out param.
// Optionally returns the caller's user id and role for ownership checks.
static std::string sri_requireSeller(const HttpRequestPtr &req, int &code,
                                     std::string *userIdOut = nullptr,
                                     std::string *roleOut = nullptr)
{
    std::string token = sri_bearer(req);
    if (token.empty())
    {
        code = 401;
        return "Authorization header with Bearer token required";
    }
    std::string role = AuthService::getRoleFromToken(token);
    if (role != "seller" && role != "admin")
    {
        code = 403;
        return "Seller or admin only";
    }
    if (roleOut) *roleOut = role;
    if (userIdOut) *userIdOut = AuthService::getUserIdFromToken(token);
    code = 200;
    return "";
}

static bool sri_parseDouble(const std::string &s, double &out)
{
    if (s.empty()) return false;
    try
    {
        size_t pos = 0;
        out = std::stod(s, &pos);
        return pos == s.size();
    }
    catch (...)
    {
        return false;
    }
}

static bool sri_parseInt(const std::string &s, int &out)
{
    if (s.empty()) return false;
    try
    {
        size_t pos = 0;
        out = std::stoi(s, &pos);
        return pos == s.size();
    }
    catch (...)
    {
        return false;
    }
}

// Registers all product routes with the Drogon server.
// Each route maps a URL + HTTP method to a function.
void ProductController::initRoutes()
{
    // GET /api/v1/products -> returns all products
    app().registerHandler("/api/v1/products",
        &ProductController::getProducts,
        {Get});

    // POST /api/v1/products -> creates a new product
    app().registerHandler("/api/v1/products",
        &ProductController::createProduct,
        {Post});

    // GET /api/v1/products/search -> search/filter.
    // Registered BEFORE /products/{id} so "search" is never captured as an id.
    app().registerHandler("/api/v1/products/search",
        &ProductController::searchProducts,
        {Get});

    // GET /api/v1/products/{id} -> returns one product
    app().registerHandler("/api/v1/products/{id}",
        &ProductController::getProductById,
        {Get});

    // PUT /api/v1/products/{id} -> updates a product
    app().registerHandler("/api/v1/products/{id}",
        &ProductController::updateProduct,
        {Put});

    // DELETE /api/v1/products/{id} -> deletes a product
    app().registerHandler("/api/v1/products/{id}",
        &ProductController::deleteProduct,
        {Delete});
}

// Fetches products with pagination: ?limit=&offset= (defaults 100/0, max 500).
void ProductController::getProducts(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    int limit = 100, offset = 0;
    std::string limS = req->getParameter("limit");
    std::string offS = req->getParameter("offset");
    if (!limS.empty() && (!sri_parseInt(limS, limit) || limit <= 0 || limit > 500))
    {
        Json::Value error;
        error["error"] = "limit must be an integer 1-500";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k400BadRequest);
        callback(resp);
        return;
    }
    if (!offS.empty() && (!sri_parseInt(offS, offset) || offset < 0))
    {
        Json::Value error;
        error["error"] = "offset must be an integer >= 0";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k400BadRequest);
        callback(resp);
        return;
    }
    // ?mine=1 -> only this seller's own products (401/403 otherwise).
    std::string mine = req->getParameter("mine");
    Json::Value products;
    if (mine == "1" || mine == "true")
    {
        std::string token = sri_bearer(req);
        std::string role = token.empty() ? "" : AuthService::getRoleFromToken(token);
        if (token.empty())
        {
            Json::Value error;
            error["error"] = "Authorization header with Bearer token required";
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k401Unauthorized);
            callback(resp);
            return;
        }
        if (role != "seller" && role != "admin")
        {
            Json::Value error;
            error["error"] = "Seller or admin only";
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k403Forbidden);
            callback(resp);
            return;
        }
        std::string userId = AuthService::getUserIdFromToken(token);
        products = ProductService::getProductsByOwner(userId, limit, offset);
    }
    else
    {
        products = ProductService::getAllProducts(limit, offset);
    }
    auto resp = HttpResponse::newHttpJsonResponse(products);
    callback(resp);
}

// Reads the JSON body, requires seller/admin, validates, creates product.
void ProductController::createProduct(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    int code = 200;
    std::string actorId, actorRole;
    std::string authErr = sri_requireSeller(req, code, &actorId, &actorRole);
    if (!authErr.empty())
    {
        Json::Value error;
        error["error"] = authErr;
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(code == 401 ? k401Unauthorized : k403Forbidden);
        callback(resp);
        return;
    }
    auto json = req->getJsonObject();
    if (!json || !json->isMember("name") || !json->isMember("price"))
    {
        Json::Value error;
        error["error"] = "Name and price are required";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k400BadRequest);
        callback(resp);
        return;
    }
    // Admins may assign the new product to a seller via ownerId in the body;
    // sellers always own what they create.
    std::string ownerId = actorId;
    if (actorRole == "admin" && json->isMember("ownerId") &&
        (*json)["ownerId"].isString() && !(*json)["ownerId"].asString().empty())
    {
        ownerId = (*json)["ownerId"].asString();
    }
    auto result = ProductService::createProduct(*json, ownerId);
    if (result.isMember("error"))
    {
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->setStatusCode(k400BadRequest);
        callback(resp);
        return;
    }
    auto resp = HttpResponse::newHttpJsonResponse(result);
    resp->setStatusCode(k201Created);
    callback(resp);
}

// Finds one product by its ID. Returns 404 if not found.
void ProductController::getProductById(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback,
    std::string productId)
{
    auto product = ProductService::getProductById(productId);
    if (product.isNull())
    {
        Json::Value error;
        error["error"] = "Product not found";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k404NotFound);
        callback(resp);
    }
    else
    {
        auto resp = HttpResponse::newHttpJsonResponse(product);
        callback(resp);
    }
}

// Updates an existing product (seller/admin only, merge semantics).
// Returns 404 if the product doesn't exist.
void ProductController::updateProduct(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback,
    std::string productId)
{
    int code = 200;
    std::string actorId, actorRole;
    std::string authErr = sri_requireSeller(req, code, &actorId, &actorRole);
    if (!authErr.empty())
    {
        Json::Value error;
        error["error"] = authErr;
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(code == 401 ? k401Unauthorized : k403Forbidden);
        callback(resp);
        return;
    }
    auto json = req->getJsonObject();
    if (!json)
    {
        Json::Value error;
        error["error"] = "Invalid request body";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k400BadRequest);
        callback(resp);
        return;
    }
    auto result = ProductService::updateProduct(productId, *json, actorId, actorRole);
    if (result.isNull())
    {
        Json::Value error;
        error["error"] = "Product not found";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k404NotFound);
        callback(resp);
    }
    else if (result.isMember("error"))
    {
        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->setStatusCode(result["error"].asString() == "Forbidden"
                                ? k403Forbidden : k400BadRequest);
        callback(resp);
    }
    else
    {
        auto resp = HttpResponse::newHttpJsonResponse(result);
        callback(resp);
    }
}

// Deletes a product by its ID (seller/admin only). Returns 404 if not found.
void ProductController::deleteProduct(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback,
    std::string productId)
{
    int code = 200;
    std::string actorId, actorRole;
    std::string authErr = sri_requireSeller(req, code, &actorId, &actorRole);
    if (!authErr.empty())
    {
        Json::Value error;
        error["error"] = authErr;
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(code == 401 ? k401Unauthorized : k403Forbidden);
        callback(resp);
        return;
    }
    std::string delErr;
    bool deleted = ProductService::deleteProduct(productId, actorId, actorRole, delErr);
    if (deleted)
    {
        Json::Value json;
        json["message"] = "Product deleted successfully";
        auto resp = HttpResponse::newHttpJsonResponse(json);
        callback(resp);
    }
    else if (delErr == "Forbidden")
    {
        Json::Value error;
        error["error"] = "Forbidden";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k403Forbidden);
        callback(resp);
    }
    else if (delErr == "Product not found")
    {
        Json::Value error;
        error["error"] = "Product not found";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k404NotFound);
        callback(resp);
    }
    else
    {
        Json::Value error;
        error["error"] = delErr.empty() ? "Delete failed" : delErr;
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k400BadRequest);
        callback(resp);
    }
}

// Searches products: ?q=&minPrice=&maxPrice=&limit=&offset=.
// Invalid numbers return 400 instead of throwing (previous std::stod crash fix).
void ProductController::searchProducts(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    std::string q = req->getParameter("q");
    std::string minS = req->getParameter("minPrice");
    std::string maxS = req->getParameter("maxPrice");
    std::string limS = req->getParameter("limit");
    std::string offS = req->getParameter("offset");
    double minPrice = -1, maxPrice = -1;
    int limit = 100, offset = 0;
    if (!minS.empty() && !sri_parseDouble(minS, minPrice))
    {
        Json::Value error;
        error["error"] = "minPrice must be a number";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k400BadRequest);
        callback(resp);
        return;
    }
    if (!maxS.empty() && !sri_parseDouble(maxS, maxPrice))
    {
        Json::Value error;
        error["error"] = "maxPrice must be a number";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k400BadRequest);
        callback(resp);
        return;
    }
    if (!limS.empty() && (!sri_parseInt(limS, limit) || limit <= 0 || limit > 500))
    {
        Json::Value error;
        error["error"] = "limit must be an integer 1-500";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k400BadRequest);
        callback(resp);
        return;
    }
    if (!offS.empty() && (!sri_parseInt(offS, offset) || offset < 0))
    {
        Json::Value error;
        error["error"] = "offset must be an integer >= 0";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k400BadRequest);
        callback(resp);
        return;
    }
    auto result = ProductService::searchProducts(q, minPrice, maxPrice, limit, offset);
    auto resp = HttpResponse::newHttpJsonResponse(result);
    callback(resp);
}
