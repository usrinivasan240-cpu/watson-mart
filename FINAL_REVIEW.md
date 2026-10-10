# Final Review — September 21, 2026 — Full Sri Mart Demonstration

## 1. Demo baseline
- Repo: `usrinivasan240-cpu/srinivasan-mart`, branch `master`
- Stack: C++20 + Drogon + PostgreSQL (port 5433) + CMake + static login UI
- Entry: `sri.cpp` on `http://0.0.0.0:8080`, login at `http://localhost:8080/`

## 2. Live demo script (5–7 min)
1. `GET /api/v1/health` -> `{"status":"UP"}`
2. Register: `POST /api/v1/auth/register` `{"username","email","password"}` -> 201
3. Login: `POST /api/v1/auth/login` -> `{token,user}` (default `admin / admin123`)
4. Validate: `GET /api/v1/auth/validate` with `Authorization: Bearer <token>`
5. Products: `POST /api/v1/products` `{"name":"Phone","price":699.99,"stock":25}`, then `GET /api/v1/products`, `GET /api/v1/products/{id}`
6. Show login page `/` with token in localStorage
7. Show `ARCHITECTURE.md` flow: Client -> routes -> Controller -> Service -> Model/DB -> JSON

## 3. cURL rehearsal
```cmd
curl http://localhost:8080/api/v1/health
curl -X POST http://localhost:8080/api/v1/auth/register -H "Content-Type: application/json" -d "{\"username\":\"test\",\"email\":\"test@test.com\",\"password\":\"test123\"}"
curl -X POST http://localhost:8080/api/v1/auth/login -H "Content-Type: application/json" -d "{\"username\":\"admin\",\"password\":\"admin123\"}"
curl http://localhost:8080/api/v1/products
```

## 4. Report + PPT outline
- Problem, Tech Stack Lock (C++20/Drogon/Postgres), Architecture diagram
- Week 1 done vs Weeks 2–11 plan (see `WEEKLY_PROJECT_SCHEDULE.md`)
- API table, DB schema (all tables auto-created by `services/Database.cc`), test results (`tests/e2e.ps1`)
- Next: Postgres auth + bcrypt, cart/order, seller/admin, search, reviews, hardening, deploy, chatbot

## 5. Rehearsal checklist
- [ ] PostgreSQL `sri_mart` running on `localhost:5433`
- [ ] `cmake --build build`, `sri_mart.exe` prints `Connected to PostgreSQL` + `running on http://0.0.0.0:8080`
- [ ] Health + login + products cURLs pass
- [ ] Backup PPT + offline README copy
