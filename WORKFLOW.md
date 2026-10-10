# Srinivasan Mart — OVERALL WORKFLOW (All Things, One File)

> One document that shows how **everything** connects — frontend ↔ backend ↔ database — for **all 11 weeks + Final Review Sep 21**.
> Format: `file:line` = open that file, go to that line. Every flow shows **exact code** with explanation.
> Stack: C++20 + Drogon + libpq PostgreSQL (`localhost:5433/sri_mart`) + static `login.html`.

---

## 0. OVERALL SYSTEM FLOW — How All Things Connect

```
                                    ┌──────────────┐
                                    │   Browser    │
                                    │ login.html   │  fetch() JSON, localStorage token
                                    └──────┬───────┘
                                           │ HTTP POST/GET/PUT/DELETE + Authorization: Bearer <token>
                                           ▼
┌────────────┐  sri.cpp:25          ┌─────────────────┐  *.cc: initRoutes()   ┌──────────────┐  call   ┌──────────────┐  SQL/RTN  ┌──────────┐
│  Drogon    │◄──listen 0.0.0.0:8080─┤  Controllers    ├──────────────────────►│   Services   ├────────►│   Models     │────────►│PostgreSQL│
│  Server    │                      │ (front desk)    │                      │ (business)   │         │ (structs)    │         │ or RAM   │
└────────────┘                      └─────────────────┘                      └──────────────┘         └──────────────┘         └──────────┘
         ▲                                 │                                      │                         │                     │
         └─────────────────────────────────┴────────── JSON response ──────────────┴──── newHttpJsonResponse ┴──── toJson() ─────┘
                                    Browser JS: response.json() → localStorage/DOM → next page (window.location.href)
```

**Rule every feature follows:** `Route → Controller → Service → Model/DB → JSON → Frontend change`

**App boot `sri.cpp:1-101`**
- `10-18` includes all controllers + `ProductService.h`
- `25` `app().addListener("0.0.0.0",port)` — port from `PORT` env (default 8080)
- `28-51` `GET /health` → `{"status":"UP"}`, `GET /hello` → welcome
- `54-77` `GET /` serves `config/login.html:1-174` else JSON
- `80` `ProductController::initRoutes()`, `83` `AuthController::initRoutes()`, `86-90` `CartController`, `OrderController`, `ReviewController`, `AdminController`, `ChatController`
- `93` `ProductService::initializeDatabase()` → creates `products` table
- `96-98` `app().run()` blocks

**Build `CMakeLists.txt:1-50`**
- `18-32` `add_executable(sri_mart sri.cpp controllers/*.cc services/*.cc)` → `target_link_libraries Drogon::Drogon pq`

**Folder map**
```
sri.cpp, CMakeLists.txt
config/api-config.js (sriApiBase), login.html (174L)
controllers/Auth(41/184L), Product(48/174L), Cart(26/135L), Order(28/127L), Review(19/52L), Admin(23/71L), Chat(19/37L)
services/Auth(62/268L), Product(50/332L), Cart(39/101L), Order(38/122L), Review(28/68L)
models/User(64L), Product(66L), CartItem(33L), Order(38L), Review(35L)
```

---

## 1. DATABASE — Connected for All Things

**Postgres (products) `services/ProductService.cc:15-106`**
```cpp
// 16 global connection
static PGconn *g_conn = nullptr;
// 20-41 getConnection()
PGconn *ProductService::getConnection(){
  if(g_conn==nullptr || PQstatus(g_conn)!=CONNECTION_OK){
    if(g_conn) PQfinish(g_conn);
    g_conn = PQconnectdb("host=localhost port=5433 dbname=sri_mart user=postgres password=postgres"); // 29
    // LOG_INFO Connected to sri_mart else LOG_ERROR
  }
  return g_conn;
}
// 43-50 freeResult() → PQclear
// 76-106 initializeDatabase() called from sri.cpp:93
const char *query = "CREATE TABLE IF NOT EXISTS products ("
  "id VARCHAR(36) PRIMARY KEY," "name VARCHAR(255) NOT NULL,"
  "description TEXT DEFAULT ''," "price DECIMAL(10,2) NOT NULL,"
  "stock INTEGER DEFAULT 0," "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
  "updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP" ")";
PGresult *res = PQexec(conn, query); // 95
```
- SQL ops: List `118:SELECT ... FROM products` → loop `PQgetvalue` `132-143`, Get one `160:SELECT ... WHERE id=$1` via `PQexecParams`, Create `INSERT ... VALUES(...)` including `owner_id`, Update `UPDATE ... WHERE id=$n`, Delete `DELETE WHERE id=$1`, Search `searchProducts()` — SQL `ILIKE` on `name` with optional price bounds (no RAM filtering).
- **Fully persisted in PostgreSQL** — `users`/`sessions` (AuthService: PBKDF2 passwords, 64-hex CSPRNG tokens), `cart_items` (CartService), `orders` + `order_items` (OrderService, transactional checkout), `reviews` (ReviewService, one per user); schema auto-created by `services/Database.cc`.

---

## 2. WEEK 1 — Authentication + Project Foundation (Jul 6-12)

### 2.1 Register — Frontend → Backend → Store
**Click** `login.html:56-70` `regUsername/regEmail/regPassword` + `69: <button onclick="register()">`
**JS `136-163`**
```js
async function register(){ // 136
  const username=document.getElementById('regUsername').value; // 137
  const email=document.getElementById('regEmail').value;
  const password=document.getElementById('regPassword').value;
  const response = await fetch(API_BASE+'/api/v1/auth/register',{ // 147
    method:'POST', headers:{'Content-Type':'application/json'},
    body: JSON.stringify({username,email,password}) // 150 — JSON passed
  });
  if(response.ok){ showMessage('Registration successful! Please login.','success'); showForm('login'); } // 154-156 frontend switches tab
}
```
**Route `AuthController.cc:19-21`** `POST /api/v1/auth/register` → `registerUser`
**Controller `46-74`** `getJsonObject():50` → require 3 fields `51` else 400 → `AuthService::registerUser(*json):61` → 201 else 400
**Service `AuthService.cc:114-161`** `lock 116` → parse `126` → duplicate check `131-145` loop users → `User{id=generateUUID():17-37, password=hashPassword():41-49 XOR 0x5A, role=customer}` → `push_back:157` → `toJson():160` (excludes password `User.h:31-41`).

### 2.2 Login — Full verify + next page
**Click** `login.html:43-52` `loginUsername/loginPassword` + `52:login()`
**JS `105-134`**
```js
async function login(){ // 105
  const username=document.getElementById('loginUsername').value; // 106
  const password=document.getElementById('loginPassword').value; // 107
  const response = await fetch(API_BASE+'/api/v1/auth/login',{ // 115
    method:'POST', headers:{'Content-Type':'application/json'},
    body: JSON.stringify({username,password}) // 118
  });
  const data = await response.json(); // 120
  if(response.ok){ // 122
    localStorage.setItem('token',data.token); // 126 — save for all later calls
    localStorage.setItem('user',JSON.stringify(data.user)); // 127
    tokenBox.textContent='Token: '+data.token; // 124
    // NEXT PAGE: window.location.href='/products.html'; — authenticated pages read token
  }
}
```
**Route `24-26`** `POST /api/v1/auth/login` → `AuthController.cc:79-109` parse `94-95` → `AuthService::loginUser:97`
**Service** `hashPassword` (PBKDF2-HMAC-SHA256, 100k iterations + per-user salt) → verify via constant-time compare → `generateToken():64 hex CSPRNG` → `sessions` row (24h expiry) → `{token,user.toJson()}` else error. **Verification = PBKDF2 password check** against the PostgreSQL `users` table (seeded admin/admin123, customer/customer123).

### 2.3 Validate / Users
- `GET /api/v1/auth/validate` + `Authorization: Bearer <token>` → `AuthController.cc:28-31,113-141` extracts `substr(7):128` → `AuthService::validateToken:193-213` `tokens.find` → user else error.
- `GET /api/v1/auth/users` → `34-36,144-151` → `getAllUsers():216-225` array.

### 2.4 Logout
**JS** `fetch POST /api/v1/auth/logout` with `Authorization: Bearer '+localStorage.getItem('token')` → `removeItem('token'); removeItem('user'); window.location.href='/'`
**Route `38-41`** → `AuthController.cc:154-184` reads header `158`, `logoutToken:169` → **Service `243-267`** `tokens.erase(it)` → 200. Frontend clears + redirects.

---

## 3. WEEK 2 — Browse + Cart + Checkout/Order (Jul 13-19)

### 3.1 Browse (Products)
**Routes `ProductController.cc:17-44`** `GET /products`, `POST /products`, `GET /products/{id}`, `PUT /products/{id}`, `DELETE /products/{id}`, `GET /products/search`
- **List** `GET /api/v1/products` → `46-53:getProducts()` → `ProductService.cc:109-147:getAllProducts()` `SELECT ... FROM products:118` → `PQgetvalue` loop `132-143` → JSON → frontend `response.json().forEach(p=> html+=...)`.
- **Create** `POST /products` `57-75` require name+price `62` → `ProductService.cc:198-232 INSERT:216` → `getProductById:231`.
- **Get one** `78-97` → `150-195 SELECT WHERE id=$1`.

### 3.2 Add to Cart — Frontend → Backend → Verify → Store → Frontend badge
**Click** `<button onclick="addToCart('uuid')">Add to Cart</button>`
```js
async function addToCart(productId){
  const token=localStorage.getItem('token');
  const resp=await fetch('/api/v1/cart',{
    method:'POST', headers:{'Content-Type':'application/json','Authorization':'Bearer '+token},
    body: JSON.stringify({productId, quantity:1}) // passed
  });
  if(resp.ok){ cartCount.textContent = +cartCount.textContent+1; showMessage('Added'); }
}
```
**Route `CartController.cc:27-41`** `POST /api/v1/cart` → `addToCart`
**Controller `76-108`** `getBearer:78` → `getUserIdFromToken:79` else 401 → require productId `92` → `CartService::addItem:99` → 201
**Service `CartService.cc:39-62`** validates `ProductService::getProductById:47` else error, merges qty if exists else `CartItem{userId,productId,qty}` pushed to `map<userId,vector>:5-14`. **DB check** ensures product exists in Postgres before cart.

### 3.3 View Cart / Remove
- `GET /api/v1/cart` → `CartController.cc:48-66` → `CartService.cc:23-37` array → frontend renders.
- `DELETE /api/v1/cart/{productId}` → `110-134` → `CartService.cc:64-82:removeItem` erase → frontend `element.remove()`.

### 3.4 Checkout / Billing
**Click Checkout** `fetch POST /api/v1/orders/checkout` with Bearer → `window.location.href='/orders.html'`
**Route `OrderController.cc:24-31`** `POST /orders/checkout` → `checkout`
**Controller `33-53`** auth `37` → `OrderService::checkout:42`
**Service `OrderService.cc:28-51`** `getCart:30` if 0 error, sum `product["price"]*qty` via `ProductService::getProductById:37`, `Order{id=UUID, userId, total, status=created}` `43-49` → `clearCart:50`. **Model `Order.h:14-27`**. Planned Postgres `BEGIN; INSERT orders; INSERT order_items; UPDATE stock; COMMIT;`.

---

## 4. WEEK 3-4 — Seller Dashboard + Admin (Jul 20-Aug 2)

**Seller** `GET /api/v1/seller/stats` + Bearer → `AdminController.cc:24-46:sellerStats()` → `validateToken:29` → `ProductService::getAllProducts().size():37` → `{role, productCount, note}` — per-seller ownership planned.
**Admin** `GET /api/v1/admin/stats` → `48-71:adminStats()` → role check `60` `!=admin →403` → returns `{userCount: AuthService::getAllUsers().size(), productCount}`. Frontend shows stats cards; non-admin gets forbidden.

---

## 5. WEEK 5 — Search/Filter + Order Status (Aug 3-9)

- **Search** `GET /api/v1/products/search?q=phone&minPrice=100&maxPrice=800` → `ProductController` validates `q`/`minPrice`/`maxPrice` → `ProductService::searchProducts()` — SQL `ILIKE` on `name` with price-range parameters → frontend updates list without reload.
- **Order status** `PUT /api/v1/orders/{id}/status {status:"paid|shipped"}` → `OrderController` (login required, **seller/admin only** → else 403) → `OrderService::updateStatus()` validates `created|paid|shipped`, staff may update any order → 200/400/404 → frontend badge `status.textContent = "Paid"`.

---

## 6. WEEK 6 — Reviews + Ratings + Validation (Aug 10-16)

**Routes `ReviewController.cc:12-16`** `GET/POST /products/{id}/reviews`
- **List** `18-24` → `ReviewService.cc:31-40:getByProduct()` array → stars rendered.
- **Add** `POST` + Bearer → `26-46:addReview()` auth `30`, `rating/comment:38-39` → `ReviewService.cc:50-68:addReview()` validates `1-5:42` → `ProductService::getProductById` exists → `Review{id,productId,userId,rating,comment}` → 201. **Model `Review.h:10-24`**.

---

## 7. WEEK 7 — Security + Testing + Sanitizers (Aug 17-23)

- Passwords hashed `AuthService.cc:41-49` XOR (demo, planned bcrypt), `toJson()` excludes password `User.h:31-41`.
- Tokens are random 32-hex `AuthService.cc:52-64`, Bearer required `AuthController.cc:113,158`, mutex `getMutex():106` protects users/tokens, `ProductService.cc:44-50` `PQclear` prevents leaks.
- Testing checkpoints `WEEKLY_PROJECT_SCHEDULE.md` + cURL `FINAL_REVIEW.md:17-23` + placeholder sanitizer hooks.

## 8. WEEK 8 — Deployment + Cloud Testing (Aug 24-30)

- `CMakeLists.txt:18-32` builds `sri_mart` via `MinGW + vcpkg + Drogon + pq`, `start.bat/dev.bat` run `build/sri_mart.exe` expecting `Connected to PostgreSQL: sri_mart` on `0.0.0.0:8080` (`sri.cpp:96`), `stop.bat` stops PG. Cloud: same binary + `PORT` env + `PQconnectdb` env.

## 9. WEEK 9 — AI Chatbot Integration (Aug 31-Sep 6)

**Route `ChatController.cc:12-31`** `POST /api/v1/chat`
```js
await fetch('/api/v1/chat',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({message:"hour?"})});
```
**Controller `22-37`** expects `{message}` else 400, keyword replies `hour→Open 9am-9pm, return→Returns 7 days, offer→check /products/search` → `{reply, echo}`. Planned LLM swap.

## 10. WEEK 10 — Chatbot Refinement + UI + Docs (Sep 7-13)

- UI is `login.html:1-174` tabs `showForm():79-91`, `showMessage():93-97`, `localStorage` tokenBox `124-127`, Enter-key `166-171`. Refinement = same `ChatController` + richer prompts.
- Docs: `README.md:1-106` API table/health/auth/products, `ARCHITECTURE.md:1-73` layers, `COMPLETE_WORKFLOW.md:1-178` traces, `WEEKLY_PROJECT_SCHEDULE.md` table.

## 11. WEEK 11 — Final Testing + Report + PPT + Rehearsal (Sep 14-21)

**Live demo `FINAL_REVIEW.md:1-35`**:
1. `GET /health` → `{"status":"UP"}` (`sri.cpp:28-33`)
2. Register `POST /auth/register` → 201
3. Login `POST /auth/login` → `{token,user}` → `login.html:126-127` stores
4. Validate `GET /auth/validate` Bearer
5. Products `POST /products` + `GET /products` (Postgres)
6. Cart `POST /cart` + checkout `POST /orders/checkout` → `window.location`
7. Show `ARCHITECTURE.md` flow
**cURL rehearsal `FINAL_REVIEW.md:17-23`** + checklist `31-35` (PG `5433`, `cmake --build build`, health/login/products). **PPT** `FINAL_REVIEW.md:25-29` tech stack, API table, DB schema (all tables in Postgres), test results (`tests/e2e.ps1`), next steps.

---

## 12. END-TO-END EXAMPLE — One User Journey Across All Weeks

1. **Register** `login.html:136-163` → `AuthController:46-74` → `AuthService` register → PostgreSQL `users` table.
2. **Login** `105-134` → `79-109` → `165-189` → token → `localStorage:126` → `window.location.href='/products.html'`.
3. **Browse** `ProductController:46-53` → `ProductService:109-147` Postgres → render.
4. **Search** `ProductController:156-174` → `ProductService:309-331` → filtered list.
5. **Add to cart** `CartController:76-108` → `CartService:39-62` verifies product in DB → cart map.
6. **Edit product** (seller) `PUT /products/{id}` `101-130` → `ProductService:235-279 UPDATE`.
7. **Checkout** `OrderController:33-53` → `OrderService:28-51` sums DB prices → order → `clearCart`.
8. **Review** `ReviewController:26-46` → `ReviewService:50-68` → stars.
9. **Admin stats** `AdminController:48-71` Bearer admin → counts.
10. **Chat** `ChatController:22-37` → reply.
11. **Logout** `AuthController:154-184` → erase token → `localStorage.clear()` → `location.href='/'`.

## 13. Folder/File Code Comments (Reference)

All files start `// MODULE: ... PURPOSE: ... WHY: ... INPUT: ... OUTPUT: ... ARCHITECTURE: ...`:
`sri.cpp:1-8`, `AuthController.h:1-8`, `ProductController.h:1-8`, `CartController.h`, `OrderController`, `ReviewController`, `AdminController`, `ChatController`, `AuthService.h:1-8`, `ProductService.h:1-8` (`getConnection:20-41`, `initializeDatabase:76-106`), `CartService`, `OrderService`, `ReviewService`, `User.h:1-8`, `Product.h:1-8`, `CartItem.h`, `Order.h`, `Review.h`, `CMakeLists.txt:1-8`, `config/api-config.js`, `login.html:76-77` API_BASE.

This is the **overall workflow for all things** — one click on any frontend button travels `fetch JSON → Drogon route → Controller (file:line) → Service (file:line) → Model/DB (file:line) → JSON → frontend localStorage/DOM/window.location`.
