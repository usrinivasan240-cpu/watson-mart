# Sri Mart — Simple Architecture

Think of the application as four layers.

```text
Client
  |
  v
Drogon HTTP routes
  |
  v
controllers/
  |
  v
services/
  |
  v
models/
  |
  v
PostgreSQL Database
```

## 1. sri.cpp
The application entry point.

Plain English:
> "Start the Sri Mart server, register routes, and keep the API running."

It should contain application startup and route registration, not large business algorithms.

## 2. controllers/
The API front desk.

A controller:
- receives HTTP requests;
- validates/reads request data;
- calls the appropriate service;
- returns JSON/HTTP responses.

## 3. services/
The business-logic layer.

A service answers:
> "What should Sri Mart actually do with this request?"

Business rules should live here instead of being buried inside route handlers.

## 4. models/
The data representation layer.

A model describes application data such as a product.
Models are plain C++ structs serialized to JSON; the services read/write the
matching PostgreSQL rows with parameterized libpq queries (no ORM).

## 5. config/
Configuration-related project files/settings, including database connection.

## 6. PostgreSQL Database
The persistent data storage layer.

All product, user, session, cart, order, and review data is stored in PostgreSQL
(schema auto-created on startup by `services/Database.cc`).
Services map C++ models to tables with parameterized libpq queries (no ORM).

## Module Boundary Rule
Do not put all code into `sri.cpp`.
Each new feature should normally follow:

```text
route -> controller -> service -> model/data -> database -> response
```

## Comment Rule
Every module must explain its responsibility in a comment before the implementation.
