# Weekly Project Schedule — Srinivasan Mart

Planned 11-week build + Final Review on September 21, 2026.

| Week | Dates | Main Work | Status on Sep 21, 2026 |
|------|-------|-----------|------------------------|
| Week 1 | Jul 6 – Jul 12 | Authentication + project/repository foundation | Done — register/login/token, logout, repo layout, Drogon + PostgreSQL base |
| Week 2 | Jul 13 – Jul 19 | Browse + Cart + Checkout/Order | Done — cart + checkout/order persisted in PostgreSQL |
| Week 3 | Jul 20 – Jul 26 | Review fixes + Seller dashboard | Stub done — seller stats endpoint |
| Week 4 | Jul 27 – Aug 2 | Seller dashboard + Admin | Stub done — admin stats, role gate |
| Week 5 | Aug 3 – Aug 9 | Search/filter + Order status | Stub done — product search + order status |
| Week 6 | Aug 10 – Aug 16 | Reviews + ratings + validation | Stub done — reviews 1-5 |
| Week 7 | Aug 17 – Aug 23 | Security + testing + sanitizers | Done — PBKDF2 password hashing, IP+username rate limit, ownership gates, e2e suite (`tests/e2e.ps1`) |
| Week 8 | Aug 24 – Aug 30 | Deployment + cloud testing | Planned |
| Week 9 | Aug 31 – Sep 6 | AI chatbot integration | Stub done — rule-based /chat |
| Week 10 | Sep 7 – Sep 13 | Chatbot refinement + UI + documentation | Planned — current UI is static login page at `/` |
| Week 11 | Sep 14 – Sep 21 | Final testing + report + PPT + rehearsal | Final review baseline — this repo state is the demo |
| Final Review | September 21, 2026 | Full Sri Mart demonstration | See `FINAL_REVIEW.md` |

Implemented in this repo (master):
- `POST /api/v1/auth/register`, `POST /api/v1/auth/login`, `GET /api/v1/auth/validate`, `GET /api/v1/auth/users`
- `GET /api/v1/products`, `POST /api/v1/products`, `GET /api/v1/products/{id}`, `PUT /api/v1/products/{id}`, `DELETE /api/v1/products/{id}`
- `GET /api/v1/health`, `GET /api/v1/hello`, `GET /` login page
- Full PostgreSQL persistence for all entities (see README Notes)

<!-- Week7 checkpoint: security review -->

<!-- Week8 checkpoint: deployment prep -->

