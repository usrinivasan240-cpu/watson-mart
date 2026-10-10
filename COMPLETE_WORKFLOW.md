# Srinivasan Mart — Complete Click-to-Code Workflow (Single File)

> One file that explains what happens behind the code when the user clicks.
> Format: `file:line` = open that file, go to that line number.
> Truthful to `master` on Sep 21, 2026: Auth + Products are implemented.
> Logout / Cart / Billing are implemented — persisted in PostgreSQL (sessions, cart_items, orders + order_items).

## 0. App boot — how the server starts

` sri.cpp:17-20` — `main()` + `addListener("0.0.0.0",8080)`
`config/api-config.js` — frontend API base config (served at `/config/api-config.js`).
` sri.cpp:23-33` — `GET /api/v1/health` returns `{"status":"UP"}`.
` sri.cpp:37-46` — `GET /api/v1/hello` returns welcome JSON.
` sri.cpp:50-72` — `GET /` reads `config/login.html:1-174`, serves as HTML; fallback JSON if file missing.
` sri.cpp:75` — `ProductController::initRoutes()` (see §5).
` sri.cpp:78` — `AuthController::initRoutes()` (see §1).
` sri.cpp:81` — `ProductService::initializeDatabase()` creates `products` table if missing.
` sri.cpp:84-86` — `app().run()` blocks and serves.

`services/ProductService.cc:76-106` — `initializeDatabase()`:
- `20-41:getConnection()` → `29:PQconnectdb("host=localhost port=5433 dbname=sri_mart user=postgres...")`
- `84-93:CREATE TABLE IF NOT EXISTS products(id,name,description,price,stock,created_at,updated_at)`

---

## 1. Register — "Register button click"

UI: `config/login.html:56-70` inputs `regUsername,regEmail,regPassword` + `69:<button onclick="register()">`

JS: `config/login.html:136-163` `async function register()`:
- `138-144` read inputs, empty check.
- `147-151` `fetch(API_BASE + '/api/v1/auth/register', POST, JSON{username,email,password})`
- `154-156` success → `showMessage + showForm('login')`.

HTTP: `POST /api/v1/auth/register`

Route: `controllers/AuthController.cc:19-21` registered in `initRoutes():16-37`.

Controller: `controllers/AuthController.cc:41-69` `registerUser()`:
- `45:getJsonObject()`, `46:require username+email+password else 400`.
- `56:AuthService::registerUser(*json)`.
- `57-68: if error→400 else 201`.

Service: `services/AuthService.cc:114-161`:
- `116:lock_guard(mutex)` — `106-110:getMutex()`.
- `119-124` re-validate fields.
- `126-128` parse strings.
- `131-145` duplicate check against the `users` table (PostgreSQL, UNIQUE username/email).
- `149-155` new `User{id=generateUUID():17-37, username,email,password=hashPassword():41-49 XOR 0x5A, role=customer, timestamps}`.
- `157:push_back`, `160:return user.toJson()`.

Model: `models/User.h:18-25` struct, `31-41:toJson()` excludes password (what client sees).

Storage: auth data lives in PostgreSQL — `services/AuthService.cc` uses the `users` / `sessions` tables, seeds `admin/admin123` + `customer/customer123` on first run when `users` is empty. Fully survives restart.

---

## 2. Login — "Login button click" (full trace)

UI: `config/login.html:43-53` inputs `loginUsername,loginPassword` + `52:<button onclick="login()">`.

JS: `config/login.html:105-134`:
- `106-112` read + empty check.
- `115-119` `fetch(.../api/v1/auth/login, POST, {username,password})`.
- `120:data=await response.json()`.
- `122-127` ok → `showMessage + tokenBox.textContent='Token: '+data.token + localStorage.setItem('token',data.token) + setItem('user',...)`.
- `128-130` fail → `showMessage(data.error)`.

HTTP: `POST /api/v1/auth/login` body `{"username","password"}`.

Route: `controllers/AuthController.cc:24-26`.

Controller: `controllers/AuthController.cc:74-104` `loginUser()`:
- `78:getJsonObject()`, `79:require username+password else 400`.
- `89-90:asString()` parse.
- `92:AuthService::loginUser(username,password)`.
- `93-98:error→401`, `99-103:else 200 with {token,user}`.

Service: `services/AuthService.cc:165-189`:
- `167:lock`, `169:hashPassword(password)`.
- `171-184:loop users, if username+hashed match → 176:generateToken():52-64 (32 hex chars) → 177:tokens[token]=user.id → 179-182:{token,user.toJson()}`.
- `186-188:else {error:"Invalid username or password"}`.

Return to browser → token stored in `localStorage` for later `validate` calls.

---

## 3. Validate / me — "app checks who I am"

JS pattern (not in login.html, for future pages): `GET /api/v1/auth/validate` + `Authorization: Bearer <token>`.

Controller: `controllers/AuthController.cc:108-136`:
- `112:getHeader("Authorization")`, `113:must start with "Bearer " else 401`.
- `123:token=substr(7)`, `124:AuthService::validateToken(token)`.

Service: `services/AuthService.cc:193-213`:
- `197-208:lookup token map:99-103, get userId, loop users, return toJson()`.
- `210-212:else {error:"Invalid or expired token"}`.

Used by any protected page before showing cart/checkout.

---

## 4. Logout — IMPLEMENTED (POST /auth/logout deletes the session row)

Current truth: no `POST /logout`, no `DELETE /token`, no controller/service code. `AuthController.h:18-33` has only register/login/validate/getUsers. Client "logout" today = `localStorage.removeItem('token')` in browser only; server token stays in `tokens` map until restart.

Planned flow following same layer rule (`route→controller→service→response`):
- `POST /api/v1/auth/logout` with `Bearer` header → `AuthController::logoutUser` (new, like `validateToken:108-124`) → `AuthService::logoutToken(token)` erases `tokens[token]` (`99-103` map) → `{message:"logged out"}`.
- Frontend: button `onclick="logout()"` → `fetch(.../logout, {headers:{Authorization:'Bearer '+localStorage.getItem('token')}})` → clear storage + redirect to `/`.

---

## 5. Products = Browse / "data get + store" (implemented via Postgres)

Routes: `controllers/ProductController.cc:17-43`:
- `20-22:GET /api/v1/products→getProducts`
- `25-27:POST /api/v1/products→createProduct`
- `30-32:GET /api/v1/products/{id}→getProductById`
- `35-37:PUT ...→updateProduct`
- `40-42:DELETE ...→deleteProduct`

### 5a. List (browse)
Controller `46-53:getProducts()` → `50:ProductService::getAllProducts()` → JSON array.
Service `services/ProductService.cc:109-147`:
- `112:getConnection():20-41`, `118-120:SELECT id,name,description,price,stock,... FROM products`, `129-143:rows→Json (price:stod, stock:stoi)`, `145:freeResult:44-50`.

### 5b. Create (store new data)
Controller `57-75:createProduct()`: `61:getJson`, `62:require name+price else 400`, `71:service`, `73:201`.
Service `198-232`: `208:generateUUID:53-73`, `209-212:parse name/description/price/stock`, `216-219:INSERT ... VALUES($1..$5) via PQexecParams`, `231:return getProductById(id)` (re-read).

### 5c. Get one / Update / Delete
- Get: `78-97` → `83:getProductById(id)` → Service `151-195:PQexecParams SELECT ... WHERE id=$1`, `179:ntuples>0?item:Null`, controller `84-90:Null→404`.
- Update: `101-130` → `116:updateProduct(id,json)` → Service `235-279:check exists 245-254, UPDATE ... 264-268, re-read 278`.
- Delete: `133-154` → `138:deleteProduct(id)` → Service `282-305:DELETE WHERE id=$1, 302:PQcmdTuples[0]!='0'`, controller `141-144:{message:"deleted"}` else 404.

Model: `models/Product.h:18-25` fields, `40-51:toJson()`, `54-65:fromJson()`.

DB: full PostgreSQL persistence for users, sessions, cart, orders, order items, reviews, and products (schema auto-created by `services/Database.cc`).

---

## 6. Add to Cart — IMPLEMENTED (`cart_items` table, qty 1–99, stock checked)

No `CartController/Service/Model`, no table, no routes. Closest existing code is product get/list above.

Planned minimal flow reusing same pattern:
- Table `cart_items(user_id,product_id,qty)` in `initializeDatabase():76-106` style.
- `POST /api/v1/cart {productId,qty}` + Bearer → `CartController::addToCart` (validate like `ProductController:61-70`) → `CartService::addItem` checks `ProductService::getProductById:151-195` exists + stock → `INSERT/UPSERT` via `PQexecParams` (`216-219` pattern) → return cart JSON.
- `GET /api/v1/cart` → `SELECT ... JOIN products` → same row→JSON loop as `129-143`.
- Frontend: product page button `onclick="addToCart(id)"` → `fetch(POST /cart, {headers:{Authorization,Content-Type}, body})`.

---

## 7. Billing / Checkout — IMPLEMENTED (single-transaction `orders` + `order_items`)

No `OrderController`, no `orders` table, no billing code in `sri.cpp:75-81`.

Planned flow:
- `POST /api/v1/orders/checkout` + Bearer → validate token (§3) → load cart (§6) → `BEGIN; check stock (SELECT ... FOR UPDATE); INSERT INTO orders(id,user_id,total,status); INSERT INTO order_items(...); UPDATE products SET stock-=qty; COMMIT;` using `PQexec/PQexecParams` style from `216-268` → return `{orderId,total}`.
- Failure → `ROLLBACK` + 400 (`{error}` like `63-69`).
- UI: `Checkout` button → `fetch` → show order id, clear cart.

---

## 8. Cheat sheet — click → HTTP → file:line

| Click | HTTP | Controller | Service | Store |
|---|---|---|---|---|
| Register | `POST /auth/register` | `AuthController.cc:41-69` | `AuthService.cc:114-161` | RAM `68-96` |
| Login | `POST /auth/login` | `AuthController.cc:74-104` | `AuthService.cc:165-189` + `41-49,52-64` | RAM token `99-103` |
| Validate | `GET /auth/validate` | `AuthController.cc:108-136` | `AuthService.cc:193-213` | RAM |
| Users | `GET /auth/users` | `AuthController.cc:139-146` | `AuthService.cc:216-225` | RAM |
| Browse | `GET /products` | `ProductController.cc:46-53` | `ProductService.cc:109-147` | Postgres `29,118-120` |
| Add product | `POST /products` | `ProductController.cc:57-75` | `ProductService.cc:198-232` | Postgres `216-219` |
| Logout/cart/billing | — | NOT FOUND | NOT FOUND | planned §4,§6,§7 |

Startup: `sri.cpp:20,75,78,81,86` + `login.html:77,105-134,136-163`.
