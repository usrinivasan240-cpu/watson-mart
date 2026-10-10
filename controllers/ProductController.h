// ============================================================
// MODULE: ProductController
// PURPOSE: Handles HTTP requests related to products.
// WHY: Keeps product API request handling separate from business logic.
// INPUT: HTTP requests such as GET /api/v1/products.
// OUTPUT: JSON responses containing product information.
// ARCHITECTURE: Controller
// ============================================================

#pragma once

#include <drogon/drogon.h>

// This class is the "front desk" for product requests.
// It receives HTTP requests and passes them to the service layer.
class ProductController
{
public:
    // Returns a list of all products.
    static void getProducts(const drogon::HttpRequestPtr &req,
                            std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    // Creates a new product from the request body.
    static void createProduct(const drogon::HttpRequestPtr &req,
                              std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    // Returns one product by its ID.
    static void getProductById(const drogon::HttpRequestPtr &req,
                               std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                               std::string productId);

    // Updates an existing product by its ID.
    static void updateProduct(const drogon::HttpRequestPtr &req,
                              std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                              std::string productId);

    // Deletes a product by its ID.
    static void deleteProduct(const drogon::HttpRequestPtr &req,
                              std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                              std::string productId);

    // Searches products by ?q=&minPrice=&maxPrice= (Week 5).
    static void searchProducts(const drogon::HttpRequestPtr &req,
                               std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    // Registers all product routes with the server.
    static void initRoutes();
};
