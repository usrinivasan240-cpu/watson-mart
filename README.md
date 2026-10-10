# Srinivasan Mart — E-Commerce Backend API

A C++20 backend API for an e-commerce store, built with Drogon and PostgreSQL. Handles user authentication and product management with a clean layered architecture.

## Tech Stack

- **Language:** C++20
- **Framework:** Drogon (HTTP server + JSON)
- **Database:** PostgreSQL (via libpq, port 5433)
- **Build:** CMake + vcpkg + MSYS2 UCRT64 / MinGW
- **Frontend:** Static HTML UI (login, buyer, seller, admin, order) served by the C++ server

## Features

- User registration, login with token auth (24h expiry), token validation
- Product CRUD (create, list, get by id, update, delete) persisted in PostgreSQL
- Cart, transactional checkout, order history + status flow
- Reviews and ratings per product; seller/admin dashboards and stats
- **Sri Assistant chatbot** (`POST /api/v1/chat`): greetings, catalog search
  with product cards, prices, stock, deals, order tracking, cart summary,
  hours/delivery/returns, and account help — with quick-reply suggestions.
  Buyer page has a floating chat widget; My Orders panel included.
- Health check and hello test endpoints
- Login/Register web UI with token storage in localStorage

## Project Structure

```text
srinivasan-mart/
├── sri.cpp                  # App entry point, route registration, port 8080
├── CMakeLists.txt           # Build config, target sri_mart
├── controllers/             # HTTP layer: Auth, Product, Cart, Order, Review, Admin, Chat
├── services/                # Business logic: Database, Auth, Product, Cart, Order, Review
├── models/                  # Data structs: User, Product, CartItem, Order, Review
├── config/
│   ├── api-config.js        # API base URL (sriApiBase()) for all pages
│   └── login.html / buyer.html / seller.html / admin.html / order.html
├── start.bat / dev.bat / stop.bat
└── ARCHITECTURE.md
```

Architecture flow: `Client -> Drogon routes -> Controller -> Service -> Model/DB -> JSON response`

## API Endpoints

### Health & UI
| Method | URL | Purpose |
|--------|-----|---------|
| GET | `/` | Login page (HTML) |
| GET | `/api/v1/health` | `{"status":"UP"}` |
| GET | `/api/v1/hello` | `{"message":"Welcome to Sri Mart API"}` |

### Auth
| Method | URL | Purpose |
|--------|-----|---------|
| POST | `/api/v1/auth/register` | `{username,email,password}` -> 201 user |
| POST | `/api/v1/auth/login` | `{username,password}` -> `{token,user}` |
| GET | `/api/v1/auth/validate` | `Authorization: Bearer <token>` -> user |
| GET | `/api/v1/auth/users` | List users (no passwords) |

### Products
| Method | URL | Purpose |
|--------|-----|---------|
| GET | `/api/v1/products` | List all |
| POST | `/api/v1/products` | `{name,price,description?,stock?}` |
| GET | `/api/v1/products/{id}` | Get one |
| PUT | `/api/v1/products/{id}` | Update |
| DELETE | `/api/v1/products/{id}` | Delete |

## Getting Started

### Prerequisites
- MSYS2 UCRT64 (MinGW), CMake, vcpkg with Drogon installed
- PostgreSQL running with database `sri_mart` (default expects `localhost:5433`, user `postgres`)

### Build
```cmd
cmake -B build -S . -G "MinGW Makefiles" -DCMAKE_TOOLCHAIN_FILE="C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic
cmake --build build
```

### Run
```cmd
cd build
sri_mart.exe
```
Open `http://localhost:8080`

You should see:
```
Connected to PostgreSQL: sri_mart
Sri Mart API running on http://0.0.0.0:8080
```

Or use `start.bat` / `dev.bat` for quick start, `stop.bat` to stop PostgreSQL.

### Test with cURL
```cmd
curl http://localhost:8080/api/v1/health
curl -X POST http://localhost:8080/api/v1/auth/register -H "Content-Type: application/json" -d "{\"username\":\"test\",\"email\":\"test@test.com\",\"password\":\"test123\"}"
curl -X POST http://localhost:8080/api/v1/auth/login -H "Content-Type: application/json" -d "{\"username\":\"admin\",\"password\":\"admin123\"}"
curl http://localhost:8080/api/v1/products
curl -X POST http://localhost:8080/api/v1/products -H "Content-Type: application/json" -d "{\"name\":\"Phone\",\"price\":699.99,\"stock\":25}"
```

Default accounts: `admin / admin123 (admin)`, `customer / customer123 (customer)`

## Deploy (Vercel + Render)

The C++ Drogon server **cannot run on Vercel** (no long-running processes),
so the app deploys in two halves:

1. **API on Render** — Dashboard: New -> Blueprint -> select this repo
   (`render.yaml` creates the Docker web service + PostgreSQL and wires
   `PGHOST/PGPORT/PGDATABASE/PGUSER/PGPASSWORD` + `PORT`). Wait until
   `GET https://<your-api>.onrender.com/api/v1/health` returns `{"status":"UP"}`.
2. **UI on Vercel** — import the repo, Framework Preset `Other`, no build
   command needed (`vercel.json` rewrites `/`, `/buyer`, `/seller`, `/admin`
   to the static pages). Then point the UI at the API — either edit the one
   line `window.SRI_API_BASE` in `config/api-config.js`, or in the browser
   console run `localStorage.setItem("sri_api_base","https://<your-api>.onrender.com")`
   and reload. Redeploy after editing `api-config.js`.

If `SRI_API_BASE` is empty the pages use same-origin (local dev, where the
C++ server serves them itself).

## Notes
- Products are persisted in PostgreSQL `products` table (auto-created on startup).
- All data (users, sessions, cart, orders + line items, reviews, products) is persisted in PostgreSQL — schema auto-created on startup. Passwords use PBKDF2-HMAC-SHA256 (100k iterations via OpenSSL when available); tokens are 64-hex CSPRNG values in the `sessions` table (24h TTL); login is rate-limited per IP+username.
- DB connection reads `PGHOST/PGPORT/PGDATABASE/PGUSER/PGPASSWORD` env vars (see `.env.example`); HTTP port reads `PORT` (default 8080). All libpq use is serialized on a mutex; checkout reserves stock in one transaction.
- Tokens expire after 24h. Usernames (`3-32`, alnum/`_`-), emails, and field lengths are validated server-side.
- `start.bat` / `dev.bat` / `stop.bat` are portable (relative paths, `PG_CTL`/`PGDATA_DIR`/`VCPKG_TOOLCHAIN` env overrides) — no hardcoded user folders.
- `POST/PUT/DELETE /api/v1/products` require seller/admin Bearer token; `GET /api/v1/auth/users` is admin-only; self-register can only create `customer`/`seller` (never `admin`).
- `GET /api/v1/products` and `/products/search` support `?limit=&offset=` (search is case-insensitive `ILIKE` in SQL); checkout runs in a Postgres transaction and decrements stock atomically.
- Frontend uses same-origin API (`window.location.origin`) + auth headers on seller writes + HTML escaping.
- See `ARCHITECTURE.md` for layer details. The HTTP port comes from `PORT` (env); logging is Drogon's default (no config file).
