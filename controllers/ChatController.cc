// ============================================================
// MODULE: ChatController
// PURPOSE: Rule-based shopping assistant for customers.
// WHY: Answers store questions, searches the live catalog, looks up the
//      caller's orders/cart (via Bearer token), and returns quick replies
//      plus structured product/order cards for the chat widget.
// INPUT: JSON {message, productId?, orderId?} + optional Bearer token.
// OUTPUT: JSON {reply, echo, suggestions[], products[], orders[], cart?}.
// ARCHITECTURE: Controller
// ============================================================

#include "ChatController.h"
#include "../services/AuthService.h"
#include "../services/ProductService.h"
#include "../services/OrderService.h"
#include "../services/CartService.h"
#include "../services/ReviewService.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace drogon;

void ChatController::initRoutes()
{
    app().registerHandler("/api/v1/chat", &ChatController::chat, {Post});
}

// ---------- small file-local helpers ----------

static std::string sri_lower(std::string s)
{
    for (auto &c : s) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    return s;
}

static std::string sri_trim(const std::string &s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Word match with boundaries ("hi" must not match "this").
static bool sri_hasWord(const std::string &lower, const std::string &word)
{
    size_t pos = 0;
    while ((pos = lower.find(word, pos)) != std::string::npos)
    {
        bool left = (pos == 0) || !std::isalnum(static_cast<unsigned char>(lower[pos - 1]));
        size_t e = pos + word.size();
        bool right = (e >= lower.size()) || !std::isalnum(static_cast<unsigned char>(lower[e]));
        if (left && right) return true;
        pos = e;
    }
    return false;
}

static bool sri_hasAny(const std::string &lower, std::initializer_list<const char *> keys)
{
    for (auto k : keys)
    {
        if (std::strlen(k) <= 3)
        {
            if (sri_hasWord(lower, k)) return true;
        }
        else if (lower.find(k) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

// Text after the first matching keyword, with filler words stripped.
static std::string sri_after(const std::string &lower, const std::string &orig,
                             std::initializer_list<const char *> keys)
{
    for (auto k : keys)
    {
        size_t pos = lower.find(k);
        if (pos == std::string::npos) continue;
        std::string rest = sri_trim(orig.substr(pos + std::strlen(k)));
        static const char *fillers[] = {"for ", "me ", "a ", "an ", "the ",
                                        "please ", "about ", "on ", "some ",
                                        "is ", "are ", "do ", "does ", "of "};
        bool stripped = true;
        while (stripped && !rest.empty())
        {
            stripped = false;
            std::string rl = sri_lower(rest);
            for (auto f : fillers)
            {
                if (rl.compare(0, std::strlen(f), f) == 0)
                {
                    rest = sri_trim(rest.substr(std::strlen(f)));
                    stripped = true;
                    break;
                }
            }
        }
        // strip trailing question marks
        while (!rest.empty() && (rest.back() == '?' || rest.back() == '.' || rest.back() == '!'))
            rest.pop_back();
        return sri_trim(rest);
    }
    return "";
}

static std::string sri_money(double v)
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << v;
    return oss.str();
}

static Json::Value sri_productCard(const Json::Value &p)
{
    Json::Value c;
    c["id"] = p.get("id", "").asString();
    c["name"] = p.get("name", "").asString();
    c["price"] = p.get("price", 0.0).asDouble();
    c["stock"] = p.get("stock", 0).asInt();
    return c;
}

void ChatController::chat(const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    auto fail = [&](const std::string &msg) {
        Json::Value e;
        e["error"] = msg;
        auto resp = HttpResponse::newHttpJsonResponse(e);
        resp->setStatusCode(k400BadRequest);
        callback(resp);
    };

    auto json = req->getJsonObject();
    std::string msg = (json && json->isMember("message")) ? (*json)["message"].asString() : "";
    msg = sri_trim(msg);
    if (msg.empty() || msg.size() > 500)
    {
        fail("message must be 1-500 characters");
        return;
    }

    // Optional auth: order/cart answers need it, everything else is public.
    std::string h = req->getHeader("Authorization");
    std::string token = (h.empty() || h.find("Bearer ") != 0) ? "" : h.substr(7);
    std::string userId = token.empty() ? "" : AuthService::getUserIdFromToken(token);

    // Optional product context (chat opened from a product card).
    Json::Value ctx;
    bool hasCtx = false;
    if (json && json->isMember("productId"))
    {
        ctx = ProductService::getProductById((*json)["productId"].asString());
        hasCtx = !ctx.isNull();
    }

    std::string lower = sri_lower(msg);
    Json::Value ok;
    ok["echo"] = msg;
    Json::Value sugg(Json::arrayValue);
    auto say = [&](const std::string &reply, std::initializer_list<const char *> chips = {}) {
        ok["reply"] = std::string("Sri Assistant: ") + reply;
        for (auto c : chips) sugg.append(c);
        ok["suggestions"] = sugg;
    };

    // ---- greetings ----
    if (sri_hasAny(lower, {"hello", "hi", "hey", "namaste", "vanakkam",
                           "good morning", "good afternoon", "good evening"}))
    {
        say("Welcome to Sri Mart! I can search products, track your orders, "
            "explain returns, or help with your cart. What do you need?",
            {"Search products", "Track my order", "Store hours", "Returns"});
        auto resp = HttpResponse::newHttpJsonResponse(ok);
        callback(resp);
        return;
    }

    // ---- help ----
    if (sri_hasAny(lower, {"help", "what can you", "options", "menu"}))
    {
        say("Here's what I do: search the catalog (try 'find mouse'), show "
            "prices and stock, track your orders, summarize your cart, and "
            "answer questions on hours, delivery, and returns.",
            {"Search products", "Track my order", "My cart", "Returns"});
        auto resp = HttpResponse::newHttpJsonResponse(ok);
        callback(resp);
        return;
    }

    // ---- hours ----
    if (sri_hasAny(lower, {"hour", "open", "close", "timing", "when are you"}))
    {
        say("We're open 9am-9pm every day, online 24x7. Anything else?",
            {"Search products", "Track my order"});
        auto resp = HttpResponse::newHttpJsonResponse(ok);
        callback(resp);
        return;
    }

    // ---- returns ----
    if (sri_hasAny(lower, {"return", "refund", "exchange", "replace"}))
    {
        say("Easy returns within 7 days with the bill. Refunds go back to "
            "your original payment mode in 2-3 days.",
            {"Track my order", "Search products"});
        auto resp = HttpResponse::newHttpJsonResponse(ok);
        callback(resp);
        return;
    }

    // ---- delivery ----
    if (sri_hasAny(lower, {"deliver", "shipping", "ship ", "shipment",
                           "courier", "how long", "when will"}))
    {
        say("Delivery takes 2-4 days. Shipping is free over Rs.499, otherwise "
            "a flat Rs.49. You'll get order status updates as it ships.",
            {"Track my order", "Search products"});
        auto resp = HttpResponse::newHttpJsonResponse(ok);
        callback(resp);
        return;
    }

    // ---- pay order (before cancel and "my order": payment is an action)
    if (sri_hasAny(lower, {"pay my order", "pay for my order",
                           "pay the order", "pay for order",
                           "make payment", "pay my bill"}))
    {
        if (userId.empty())
        {
            say("Please login first, then ask me to pay for an order.",
                {"Track my order"});
            auto resp = HttpResponse::newHttpJsonResponse(ok);
            callback(resp);
            return;
        }
        Json::Value all = OrderService::getOrders(userId);
        // Latest unpaid order = last one still in "created".
        std::string targetId;
        for (Json::ArrayIndex i = all.size(); i-- > 0;)
        {
            if (all[i].get("status", "").asString() == "created")
            {
                targetId = all[i].get("id", "").asString();
                break;
            }
        }
        if (targetId.empty())
        {
            say("There's no unpaid order to pay right now — your orders are "
                "already paid, shipped, or closed. Place a new order and "
                "I'll help you pay for it.",
                {"Track my order", "Search products"});
            auto resp = HttpResponse::newHttpJsonResponse(ok);
            callback(resp);
            return;
        }
        std::string role = AuthService::getRoleFromToken(token);
        Json::Value upd = OrderService::updateStatus(userId, role, targetId, "paid");
        if (upd.isMember("error"))
        {
            say("I couldn't process the payment: " + upd["error"].asString(),
                {"Track my order"});
        }
        else
        {
            Json::Value arr(Json::arrayValue);
            arr.append(upd);
            ok["orders"] = arr;
            double total = upd.get("total", 0.0).asDouble();
            say("Payment received — order " + targetId.substr(0, 8) +
                " is now paid (" + sri_money(total) + "). Next step: we "
                "ship it.",
                {"Track my order", "Cancel my order"});
        }
        auto resp = HttpResponse::newHttpJsonResponse(ok);
        callback(resp);
        return;
    }

    // ---- cancel order (before "my order": cancel is an action, not a lookup)
    if (sri_hasAny(lower, {"cancel my order", "cancel order",
                           "cancel the order", "cancel purchase"}))
    {
        if (userId.empty())
        {
            say("Please login first, then ask me to cancel an order that "
                "hasn't shipped yet.",
                {"Track my order"});
            auto resp = HttpResponse::newHttpJsonResponse(ok);
            callback(resp);
            return;
        }
        Json::Value all = OrderService::getOrders(userId);
        // Latest cancellable order = last one still created/paid.
        std::string targetId;
        Json::Value target;
        for (Json::ArrayIndex i = all.size(); i-- > 0;)
        {
            std::string st = all[i].get("status", "").asString();
            if (st == "created" || st == "paid")
            {
                targetId = all[i].get("id", "").asString();
                target = all[i];
                break;
            }
        }
        if (targetId.empty())
        {
            say("There's no active order to cancel — only orders that are "
                "still being prepared or paid can be cancelled. Shipped "
                "orders can be returned after delivery.",
                {"Track my order", "Returns"});
            auto resp = HttpResponse::newHttpJsonResponse(ok);
            callback(resp);
            return;
        }
        std::string role = AuthService::getRoleFromToken(token);
        Json::Value upd = OrderService::updateStatus(userId, role, targetId, "cancelled");
        if (upd.isMember("error"))
        {
            say("I couldn't cancel that order: " + upd["error"].asString(),
                {"Track my order"});
        }
        else
        {
            Json::Value arr(Json::arrayValue);
            arr.append(upd);
            ok["orders"] = arr;
            say("Done — order " + targetId.substr(0, 8) + " is cancelled. "
                "Nothing will be shipped.",
                {"Track my order", "Search products"});
        }
        auto resp = HttpResponse::newHttpJsonResponse(ok);
        callback(resp);
        return;
    }

    // ---- seller sales summary (before "deals": "my sales" must not
    //      fall through to the discount intent which matches "sale")
    if (sri_hasAny(lower, {"my sales", "sales report", "how are sales",
                           "how am i doing", "earnings", "revenue"}))
    {
        std::string role = userId.empty() ? "" : AuthService::getRoleFromToken(token);
        if (role != "seller" && role != "admin")
        {
            say("Sales reports are for sellers. Register as a seller, add "
                "products, and ask me again — I'll track your revenue here.",
                {"Search products", "Deals"});
            auto resp = HttpResponse::newHttpJsonResponse(ok);
            callback(resp);
            return;
        }
        Json::Value prods = ProductService::getProductSalesByOwner(userId);
        int prodCount = (int)prods.size();
        int stock = 0, low = 0, bestRevIdx = -1;
        double bestRev = -1.0;
        for (Json::ArrayIndex i = 0; i < prods.size(); i++)
        {
            int st = prods[i].get("stock", 0).asInt();
            stock += st;
            if (st > 0 && st <= 5) low++;
            double rev = prods[i].get("revenue", 0.0).asDouble();
            if (rev > bestRev) { bestRev = rev; bestRevIdx = (int)i; }
        }
        double revenue = 0.0;
        int units = 0, ordersCount = 0;
        Json::Value sales = OrderService::getSalesByOwner();
        for (const auto &s : sales)
        {
            if (s.get("ownerId", "").asString() != userId) continue;
            units = s.get("units", 0).asInt();
            revenue = s.get("revenue", 0.0).asDouble();
            ordersCount = s.get("orders", 0).asInt();
            break;
        }
        std::string summary = "Your store: " + std::to_string(prodCount) +
            " product(s), " + std::to_string(stock) + " unit(s) in stock, Rs." +
            sri_money(revenue) + " revenue from " + std::to_string(units) +
            " unit(s) across " + std::to_string(ordersCount) + " order(s).";
        if (bestRevIdx >= 0)
        {
            summary += " Top seller: " +
                prods[bestRevIdx].get("name", "").asString() + " (Rs." +
                sri_money(bestRev) + ").";
        }
        if (low > 0)
        {
            summary += " " + std::to_string(low) + " product(s) running low.";
        }
        say(summary, {"My orders", "Deals"});
        auto resp = HttpResponse::newHttpJsonResponse(ok);
        callback(resp);
        return;
    }

    // ---- orders ----
    if (sri_hasAny(lower, {"my order", "track", "order status", "package",
                           "parcel", "where is", "my purchase"}))
    {
        if (userId.empty())
        {
            say("Please login first, then ask me to track your order and "
                "I'll pull it up right away.",
                {"Store hours", "Search products"});
            auto resp = HttpResponse::newHttpJsonResponse(ok);
            callback(resp);
            return;
        }
        std::string oid = (json && json->isMember("orderId"))
            ? (*json)["orderId"].asString() : "";
        if (!oid.empty())
        {
            Json::Value o = OrderService::getOrderById(userId, oid);
            if (o.isNull())
            {
                say("I can't find that order under your account. Check the ID "
                    "in My Orders and try again.",
                    {"My orders"});
            }
            else
            {
                Json::Value arr(Json::arrayValue);
                arr.append(o);
                ok["orders"] = arr;
                say("Order " + oid.substr(0, 8) + ": Rs." +
                    sri_money(o.get("total", 0.0).asDouble()) + ", status '" +
                    o.get("status", "created").asString() + "'.",
                    {"My orders", "Returns"});
            }
            auto resp = HttpResponse::newHttpJsonResponse(ok);
            callback(resp);
            return;
        }
        Json::Value all = OrderService::getOrders(userId);
        if (all.size() == 0)
        {
            say("You have no orders yet. Add something to your cart and check "
                "out — I'll track it here afterwards.",
                {"Search products", "Deals"});
            auto resp = HttpResponse::newHttpJsonResponse(ok);
            callback(resp);
            return;
        }
        Json::Value arr(Json::arrayValue);
        std::string summary;
        for (Json::ArrayIndex i = 0; i < all.size() && i < 3; i++)
        {
            arr.append(all[i]);
            if (!summary.empty()) summary += " | ";
            summary += all[i].get("id", "").asString().substr(0, 8) + ": Rs." +
                sri_money(all[i].get("total", 0.0).asDouble()) + " (" +
                all[i].get("status", "created").asString() + ")";
        }
        ok["orders"] = arr;
        say("Your recent orders: " + summary + ".",
            {"Pay my order", "Cancel my order", "Returns", "Search products"});
        auto resp = HttpResponse::newHttpJsonResponse(ok);
        callback(resp);
        return;
    }
    if (sri_hasWord(lower, "orders"))
    {
        if (userId.empty())
        {
            say("Please login first, then I'll show your orders.",
                {"Search products"});
        }
        else
        {
            Json::Value all = OrderService::getOrders(userId);
            ok["orders"] = all;
            say(all.size() == 0
                    ? "You have no orders yet — your history will appear here."
                    : "You have " + std::to_string((int)all.size()) +
                      " order(s). Latest is '" +
                      all[all.size() - 1].get("status", "created").asString() +
                      "'.",
                {"Track my order", "Search products"});
        }
        auto resp = HttpResponse::newHttpJsonResponse(ok);
        callback(resp);
        return;
    }

    // ---- cart ----
    if (sri_hasAny(lower, {"cart", "basket", "bag"}))
    {
        if (userId.empty())
        {
            say("Please login first, then I'll summarize your cart.",
                {"Search products"});
            auto resp = HttpResponse::newHttpJsonResponse(ok);
            callback(resp);
            return;
        }
        Json::Value cart = CartService::getCart(userId);
        if (cart.size() == 0)
        {
            say("Your cart is empty. Search the catalog and add something!",
                {"Search products", "Deals"});
            auto resp = HttpResponse::newHttpJsonResponse(ok);
            callback(resp);
            return;
        }
        double total = 0.0;
        int count = 0;
        for (auto &line : cart)
        {
            Json::Value p = ProductService::getProductById(
                line.get("productId", "").asString());
            double price = p.isNull() ? 0.0 : p.get("price", 0.0).asDouble();
            int qty = line.get("quantity", 1).asInt();
            total += price * qty;
            count += qty;
        }
        ok["cartCount"] = count;
        ok["cartTotal"] = total;
        say("Your cart has " + std::to_string(count) + " item(s), total Rs." +
            sri_money(total) + ". Ready when you are!",
            {"Checkout help", "Search products"});
        auto resp = HttpResponse::newHttpJsonResponse(ok);
        callback(resp);
        return;
    }

    // ---- checkout ----
    if (sri_hasAny(lower, {"checkout", "buy", "pay", "payment", "place order",
                           "confirm order"}))
    {
        say("To order: add items to your cart, then hit Checkout. I'll total "
            "it from live prices, reserve stock, and give you a bill ID you "
            "can track here.",
            {"My cart", "Track my order"});
        auto resp = HttpResponse::newHttpJsonResponse(ok);
        callback(resp);
        return;
    }

    // ---- deals ----
    if (sri_hasAny(lower, {"offer", "deal", "discount", "sale", "cheap",
                           "lowest"}))
    {
        Json::Value all = ProductService::getAllProducts(50, 0);
        std::vector<std::pair<double, Json::Value>> ranked;
        for (auto &p : all)
            ranked.emplace_back(p.get("price", 0.0).asDouble(), p);
        std::sort(ranked.begin(), ranked.end(),
                  [](const auto &a, const auto &b) { return a.first < b.first; });
        Json::Value arr(Json::arrayValue);
        std::string names;
        for (size_t i = 0; i < ranked.size() && i < 3; i++)
        {
            arr.append(sri_productCard(ranked[i].second));
            if (!names.empty()) names += ", ";
            names += ranked[i].second.get("name", "").asString() + " Rs." +
                sri_money(ranked[i].first);
        }
        if (arr.size() == 0)
        {
            say("No products listed right now — check back soon!",
                {"Store hours"});
        }
        else
        {
            ok["products"] = arr;
            say("Cheapest right now: " + names + ". Tap one to add it!",
                {"Search products", "My cart"});
        }
        auto resp = HttpResponse::newHttpJsonResponse(ok);
        callback(resp);
        return;
    }

    // ---- search ----
    if (sri_hasAny(lower, {"search", "find", "look for", "show me",
                           "do you have", "any "}))
    {
        std::string term = sri_after(lower, msg,
            {"search for", "search", "find", "look for", "show me",
             "do you have", "any "});
        if (term.empty())
        {
            say("What should I look for? Try 'find mouse' or 'search keyboard'.",
                {"Deals"});
            auto resp = HttpResponse::newHttpJsonResponse(ok);
            callback(resp);
            return;
        }
        Json::Value hits = ProductService::searchProducts(term, -1, -1, 5, 0);
        if (hits.size() == 0)
        {
            say("Nothing found for '" + term + "'. Try a shorter word, or "
                "browse the deals.",
                {"Deals", "Search products"});
            auto resp = HttpResponse::newHttpJsonResponse(ok);
            callback(resp);
            return;
        }
        Json::Value arr(Json::arrayValue);
        std::string names;
        for (Json::ArrayIndex i = 0; i < hits.size() && i < 3; i++)
        {
            arr.append(sri_productCard(hits[i]));
            if (!names.empty()) names += ", ";
            names += hits[i].get("name", "").asString() + " Rs." +
                sri_money(hits[i].get("price", 0.0).asDouble());
        }
        ok["products"] = arr;
        say("Found " + std::to_string((int)hits.size()) + " match(es): " +
            names + ".",
            {"My cart", "Search products"});
        auto resp = HttpResponse::newHttpJsonResponse(ok);
        callback(resp);
        return;
    }

    // ---- price ----
    if (sri_hasAny(lower, {"price", "cost", "how much", "rate"}))
    {
        std::string term = sri_after(lower, msg, {"price of", "cost of",
            "how much is", "how much", "price", "cost", "rate"});
        if (!term.empty())
        {
            Json::Value hits = ProductService::searchProducts(term, -1, -1, 1, 0);
            if (hits.size() > 0)
            {
                ok["products"] = Json::Value(Json::arrayValue);
                ok["products"].append(sri_productCard(hits[0]));
                say(hits[0].get("name", "").asString() + " costs Rs." +
                    sri_money(hits[0].get("price", 0.0).asDouble()) + ".",
                    {"My cart", "Search products"});
                auto resp = HttpResponse::newHttpJsonResponse(ok);
                callback(resp);
                return;
            }
            say("I can't find '" + term + "'. Try 'deals' to see what's in.",
                {"Deals"});
            auto resp = HttpResponse::newHttpJsonResponse(ok);
            callback(resp);
            return;
        }
        if (hasCtx)
        {
            say(std::string(ctx.get("name", "").asString()) + " costs Rs." +
                sri_money(ctx.get("price", 0.0).asDouble()) + ".",
                {"My cart"});
            auto resp = HttpResponse::newHttpJsonResponse(ok);
            callback(resp);
            return;
        }
        say("Which product's price? Name it, like 'price of mouse'.",
            {"Deals", "Search products"});
        auto resp = HttpResponse::newHttpJsonResponse(ok);
        callback(resp);
        return;
    }

    // ---- stock ----
    if (sri_hasAny(lower, {"stock", "available", "in stock", "left", "quantity"}))
    {
        if (hasCtx)
        {
            int st = ctx.get("stock", 0).asInt();
            say(std::string(ctx.get("name", "").asString()) +
                (st > 0 ? " is in stock (" + std::to_string(st) + " left)."
                        : " is out of stock right now."),
                {"My cart", "Search products"});
            auto resp = HttpResponse::newHttpJsonResponse(ok);
            callback(resp);
            return;
        }
        std::string term = sri_after(lower, msg, {"stock of", "stock", "available"});
        if (!term.empty())
        {
            Json::Value hits = ProductService::searchProducts(term, -1, -1, 1, 0);
            if (hits.size() > 0)
            {
                int st = hits[0].get("stock", 0).asInt();
                say(hits[0].get("name", "").asString() +
                    (st > 0 ? " has " + std::to_string(st) + " in stock."
                            : " is out of stock right now."),
                    {"My cart"});
                auto resp = HttpResponse::newHttpJsonResponse(ok);
                callback(resp);
                return;
            }
        }
        say("Tell me which product, like 'stock of keyboard'.",
            {"Search products"});
        auto resp = HttpResponse::newHttpJsonResponse(ok);
        callback(resp);
        return;
    }

    // ---- reviews ----
    if (sri_hasAny(lower, {"review", "rating", "star", "feedback"}))
    {
        std::string pid = (json && json->isMember("productId"))
            ? (*json)["productId"].asString() : "";
        if (!pid.empty())
        {
            Json::Value revs = ReviewService::getByProduct(pid);
            if (revs.size() == 0)
            {
                say("No reviews yet for this one — yours could be the first!",
                    {"Search products"});
            }
            else
            {
                double sum = 0;
                for (auto &r : revs) sum += r.get("rating", 5).asInt();
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(1)
                    << (sum / (double)revs.size());
                say("Rated " + oss.str() + "/5 from " +
                    std::to_string((int)revs.size()) + " review(s).",
                    {"Search products"});
            }
            auto resp = HttpResponse::newHttpJsonResponse(ok);
            callback(resp);
            return;
        }
        say("You can read and leave star ratings (1-5) on any product card "
            "in the buyer page.",
            {"Search products"});
        auto resp = HttpResponse::newHttpJsonResponse(ok);
        callback(resp);
        return;
    }

    // ---- account help ----
    if (sri_hasAny(lower, {"register", "sign up", "signup", "create account",
                           "join", "new account"}))
    {
        say("Tap Register, pick a username, email, password, and role "
            "(Buyer or Seller). Then login — takes 30 seconds.",
            {"Store hours"});
        auto resp = HttpResponse::newHttpJsonResponse(ok);
        callback(resp);
        return;
    }
    if (sri_hasAny(lower, {"login", "sign in", "signin", "log in"}))
    {
        say("Login with your username and password. I keep you signed in for "
            "24 hours so orders and cart stay yours.",
            {"Track my order"});
        auto resp = HttpResponse::newHttpJsonResponse(ok);
        callback(resp);
        return;
    }
    if (sri_hasAny(lower, {"logout", "log out", "sign out"}))
    {
        say("Hit Logout (top right). It clears your session on this device.",
            {"Store hours"});
        auto resp = HttpResponse::newHttpJsonResponse(ok);
        callback(resp);
        return;
    }
    if (sri_hasAny(lower, {"seller", "sell "}))
    {
        say("To sell: register with the Seller role, open the Seller "
            "dashboard, and add products — buyers see them instantly.",
            {"Search products"});
        auto resp = HttpResponse::newHttpJsonResponse(ok);
        callback(resp);
        return;
    }
    if (sri_hasWord(lower, "admin"))
    {
        say("Admins sign in with the admin account to see user and product "
            "stats on the Admin page.",
            {"Store hours"});
        auto resp = HttpResponse::newHttpJsonResponse(ok);
        callback(resp);
        return;
    }

    // ---- thanks / bye ----
    if (sri_hasAny(lower, {"thank", "thanks", "great", "awesome", "nice"}))
    {
        say("You're welcome! Happy shopping.",
            {"Search products", "Deals"});
        auto resp = HttpResponse::newHttpJsonResponse(ok);
        callback(resp);
        return;
    }
    if (sri_hasAny(lower, {"bye", "see you", "good night"}))
    {
        say("Bye! I'll be right here when you need me.",
            {"Search products"});
        auto resp = HttpResponse::newHttpJsonResponse(ok);
        callback(resp);
        return;
    }

    // ---- fallback ----
    say("I didn't quite get that. I do product search, prices, orders, cart, "
        "hours, delivery, and returns — pick one below.",
        {"Search products", "Track my order", "Store hours", "Help"});
    auto resp = HttpResponse::newHttpJsonResponse(ok);
    callback(resp);
}
