// ============================================================
// MODULE: Product
// PURPOSE: Represents product data in Sri Mart.
// WHY: Defines the structure of product data used by the API.
// INPUT: Product information from API requests.
// OUTPUT: Product object that can be converted to/from JSON.
// ARCHITECTURE: Model
// ============================================================

#pragma once

#include <string>
#include <drogon/drogon.h>

// This struct defines what a product looks like.
// Think of it as a form template: id, name, description, price, stock, timestamps.
struct Product
{
    std::string id;          // Unique identifier (UUID)
    std::string name;        // Product name (e.g., "Wireless Mouse")
    std::string description; // Product details (e.g., "Ergonomic wireless mouse")
    double price;            // Product cost (e.g., 29.99)
    int stock;               // How many are available (e.g., 50)
    std::string created_at;  // When the product was added
    std::string updated_at;  // When the product was last changed

    // Default constructor: creates an empty product with price=0 and stock=0.
    Product() : price(0.0), stock(0) {}

    // Full constructor: creates a product with all fields filled in.
    Product(const std::string &id, const std::string &name,
            const std::string &description, double price,
            int stock, const std::string &created_at,
            const std::string &updated_at)
        : id(id), name(name), description(description),
          price(price), stock(stock),
          created_at(created_at), updated_at(updated_at) {}

    // Converts this product into a JSON object for sending in HTTP responses.
    Json::Value toJson() const
    {
        Json::Value json;
        json["id"] = id;
        json["name"] = name;
        json["description"] = description;
        json["price"] = price;
        json["stock"] = stock;
        json["created_at"] = created_at;
        json["updated_at"] = updated_at;
        return json;
    }

    // Creates a product from a JSON object received in an HTTP request.
    static Product fromJson(const Json::Value &json)
    {
        Product product;
        if (json.isMember("id")) product.id = json["id"].asString();
        if (json.isMember("name")) product.name = json["name"].asString();
        if (json.isMember("description")) product.description = json["description"].asString();
        if (json.isMember("price")) product.price = json["price"].asDouble();
        if (json.isMember("stock")) product.stock = json["stock"].asInt();
        if (json.isMember("created_at")) product.created_at = json["created_at"].asString();
        if (json.isMember("updated_at")) product.updated_at = json["updated_at"].asString();
        return product;
    }
};
