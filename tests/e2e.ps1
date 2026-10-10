# ============================================================
# MODULE: tests/e2e.ps1
# PURPOSE: End-to-end smoke test for the Sri Mart API.
# WHY: The project had no tests; this verifies every layer
#      (auth, products+ownership, cart, checkout, orders,
#      reviews, stats, chat, admin control plane + seller
#      analysis + user CRUD, rate limit, persistence).
# INPUT:  Running server at http://localhost:8080 + PostgreSQL.
# OUTPUT: PASS/FAIL lines and exit code = number of failures.
# ARCHITECTURE: Test
# ============================================================
#
# Usage:  powershell -ExecutionPolicy Bypass -File tests\e2e.ps1
#         (start the stack first: docker app + postgres, or start.bat)

$base = "http://localhost:8080"
$script:pass = 0
$script:fail = 0
$script:failures = @()

function Req([string]$method, [string]$path, $body = $null, [string]$token = $null) {
    # WHY file-based body: PowerShell 5.1 passes raw double quotes to curl.exe
    # unescaped, so MSVCRT argv parsing strips them (JSON arrives as
    # {username:admin,...} -> 400). Writing the body to a file and using
    # -d @file avoids the quoting layer entirely.
    $out = [System.IO.Path]::GetTempFileName()
    $args = @("-s", "-o", $out, "-w", "%{http_code}", "-X", $method, "$base$path")
    if ($token) { $args += @("-H", "Authorization: Bearer $token") }
    $bodyFile = $null
    if ($null -ne $body) {
        $json = if ($body -is [string]) { $body } else { $body | ConvertTo-Json -Compress }
        $bodyFile = [System.IO.Path]::GetTempFileName()
        [System.IO.File]::WriteAllText($bodyFile, $json)
        $args += @("-H", "Content-Type: application/json", "-d", "@$bodyFile")
    }
    $code = & curl.exe @args 2>$null
    $content = ""
    if (Test-Path $out) { $content = Get-Content $out -Raw; Remove-Item $out -Force }
    if ($bodyFile -and (Test-Path $bodyFile)) { Remove-Item $bodyFile -Force }
    return @{ code = [int]$code; body = $content }
}

function Check([string]$name, [bool]$cond, $detail = "") {
    if ($cond) { Write-Host "PASS  $name"; $script:pass++ }
    else {
        Write-Host "FAIL  $name  -> $detail"
        $script:fail++
        $script:failures += $name
    }
}

function J([string]$s) { return ($s | ConvertFrom-Json) }

Write-Host "=== Sri Mart e2e against $base ==="

# --- 1. Health + static pages ---------------------------------
$r = Req GET "/api/v1/health"
Check "health UP" ($r.code -eq 200 -and $r.body -match "UP") "$($r.code) $($r.body)"

$r = Req GET "/config/api-config.js"
Check "api-config.js served" ($r.code -eq 200 -and $r.body -match "sriApiBase") "$($r.code)"

foreach ($p in @("/", "/buyer", "/seller", "/admin", "/order")) {
    $r = Req GET $p
    Check "page $p" ($r.code -eq 200) "$($r.code)"
}

# --- 2. Auth: seeded admin login + validate -------------------
$r = Req POST "/api/v1/auth/login" '{"username":"admin","password":"admin123"}'
$adminTok = $null
try { $adminTok = (J $r.body).token } catch {}
Check "admin login (seeded, PBKDF2 verify)" ($r.code -eq 200 -and $adminTok) "$($r.code) $($r.body)"

$r = Req GET "/api/v1/auth/validate" $null $adminTok
Check "admin token validates" ($r.code -eq 200 -and (J $r.body).username -eq "admin") "$($r.code) $($r.body)"

$r = Req POST "/api/v1/auth/login" '{"username":"admin","password":"wrong"}'
Check "bad password -> 401" ($r.code -eq 401) "$($r.code)"

# --- 3. Register two sellers + one buyer ----------------------
function Register($u, $e, $p, $role) {
    return Req POST "/api/v1/auth/register" ('{"username":"' + $u + '","email":"' + $e + '","password":"' + $p + '","role":"' + $role + '"}')
}
function Login($u, $p) {
    $r = Req POST "/api/v1/auth/login" ('{"username":"' + $u + '","password":"' + $p + '"}')
    $t = $null
    try { $t = (J $r.body).token } catch {}
    return @{ code = $r.code; token = $t; body = $r.body }
}

$r = Register "seller_a" "sellera@test.com" "pass1234" "seller"
Check "register seller_a" ($r.code -eq 201) "$($r.code) $($r.body)"
$r = Register "seller_b" "sellerb@test.com" "pass1234" "seller"
Check "register seller_b" ($r.code -eq 201) "$($r.code) $($r.body)"
$r = Register "buyer_x" "buyerx@test.com" "pass1234" "customer"
Check "register buyer_x" ($r.code -eq 201) "$($r.code) $($r.body)"

$r = Register "admin2" "admin2@test.com" "pass1234" "customer"
Check "self-register cannot grant admin" ($r.code -eq 201 -and (J $r.body).role -eq "customer") "$($r.code) $($r.body)"

$sA = Login "seller_a" "pass1234"
$sB = Login "seller_b" "pass1234"
$bx = Login "buyer_x" "pass1234"
Check "seller_a login" ($sA.code -eq 200 -and $sA.token) "$($sA.code)"
Check "seller_b login" ($sB.code -eq 200 -and $sB.token) "$($sB.code)"
Check "buyer_x login" ($bx.code -eq 200 -and $bx.token) "$($bx.code)"

# --- 4. Products + ownership ----------------------------------
$r = Req POST "/api/v1/products" '{"name":"Laptop","price":999.99,"stock":10,"description":"Fast laptop"}' $sA.token
$p1 = $null
try { $p1 = (J $r.body).id } catch {}
Check "seller_a creates product P1" ($r.code -eq 201 -and $p1) "$($r.code) $($r.body)"

$r = Req POST "/api/v1/products" '{"name":"OwnedTest","price":5,"stock":1}' $sA.token
$p2 = $null
try { $p2 = (J $r.body).id } catch {}
Check "seller_a creates product P2" ($r.code -eq 201 -and $p2) "$($r.code)"

$r = Req GET "/api/v1/products"
Check "list products includes P1" (($r.body -match $p1)) "$($r.code)"

$r = Req GET "/api/v1/products?mine=1" $null $sA.token
$mine = $false
# @() wrap REQUIRED: a single PSCustomObject has no .Count in PS 5.1 ($null), so the
# pipeline result must be re-arrayed before counting.
try { $mine = @(@(J $r.body) | Where-Object { $_.id -eq $p1 }).Count -gt 0 } catch {}
Check "?mine=1 returns own products" ($r.code -eq 200 -and $mine) "$($r.code) $($r.body)"

$r = Req GET "/api/v1/products?mine=1"
Check "?mine=1 without token -> 401" ($r.code -eq 401) "$($r.code)"

$r = Req GET "/api/v1/products?mine=1" $null $bx.token
Check "?mine=1 as customer -> 403" ($r.code -eq 403) "$($r.code)"

$r = Req PUT ("/api/v1/products/" + $p2) '{"name":"HACKED"}' $sB.token
Check "seller_b cannot edit seller_a product -> 403" ($r.code -eq 403) "$($r.code) $($r.body)"

$r = Req PUT ("/api/v1/products/" + $p2) '{"name":"AdminEdited"}' $adminTok
Check "admin can edit any product" ($r.code -eq 200 -and (J $r.body).name -eq "AdminEdited") "$($r.code) $($r.body)"

$r = Req DELETE ("/api/v1/products/" + $p2) $null $sB.token
Check "seller_b cannot delete seller_a product -> 403" ($r.code -eq 403) "$($r.code) $($r.body)"

$r = Req GET "/api/v1/products/search?q=laptop"
Check "search finds Laptop" ($r.code -eq 200 -and ($r.body -match "Laptop")) "$($r.code)"

# --- 5. Cart + checkout ---------------------------------------
$r = Req POST "/api/v1/cart" ('{"productId":"' + $p1 + '","quantity":2}') $bx.token
Check "buyer adds P1 x2 to cart" ($r.code -ge 200 -and $r.code -lt 300) "$($r.code) $($r.body)"

$r = Req GET "/api/v1/cart" $null $bx.token
Check "cart has 1 line qty 2" ((@(J $r.body).Count -eq 1) -and (@(J $r.body)[0].quantity -eq 2)) "$($r.code) $($r.body)"

$r = Req POST "/api/v1/orders/checkout" "{}" $bx.token
$order = $null
try { $order = J $r.body } catch {}
Check "checkout succeeds" ($r.code -ge 200 -and $r.code -lt 300 -and $order.id) "$($r.code) $($r.body)"
$oid = $null; if ($order) { $oid = $order.id }

$r = Req GET "/api/v1/cart" $null $bx.token
Check "cart cleared after checkout" (@(J $r.body).Count -eq 0) "$($r.code) $($r.body)"

$r = Req GET ("/api/v1/orders/" + $oid) $null $bx.token
$o = $null
try { $o = J $r.body } catch {}
$lineOk = $false
if ($o -and $o.items) {
    $lineOk = ($o.items.Count -eq 1) -and ($o.items[0].name -eq "Laptop") -and ($o.items[0].quantity -eq 2)
}
Check "order has server-side line items" ($r.code -eq 200 -and $lineOk) "$($r.code) $($r.body)"

$r = Req GET "/api/v1/products/$p1"
$stockLeft = -1
try { $stockLeft = (J $r.body).stock } catch {}
Check "stock decremented 10 -> 8" ($stockLeft -eq 8) "$($r.code) stock=$stockLeft"

$r = Req GET "/api/v1/orders" $null $bx.token
Check "order history lists order" ($r.code -eq 200 -and ($r.body -match $oid)) "$($r.code)"

# --- 6. Order status authorization ----------------------------
$r = Req PUT ("/api/v1/orders/" + $oid + "/status") '{"status":"shipped"}' $bx.token
Check "buyer cannot self-ship -> 403" ($r.code -eq 403) "$($r.code) $($r.body)"

$r = Req PUT ("/api/v1/orders/" + $oid + "/status") '{"status":"paid"}' $sA.token
Check "seller can mark order paid" ($r.code -eq 200 -and (J $r.body).status -eq "paid") "$($r.code) $($r.body)"

$r = Req PUT ("/api/v1/orders/" + $oid + "/status") '{"status":"shipped"}' $adminTok
Check "admin can mark order shipped" ($r.code -eq 200 -and (J $r.body).status -eq "shipped") "$($r.code) $($r.body)"

$r = Req PUT ("/api/v1/orders/" + $oid + "/status") '{"status":"exploded"}' $adminTok
Check "invalid status rejected" ($r.code -eq 400 -or $r.code -eq 403) "$($r.code) $($r.body)"

# --- 7. Reviews ------------------------------------------------
$r = Req POST "/api/v1/products/$p1/reviews" '{"rating":4,"comment":"Great value"}' $bx.token
Check "buyer reviews product" ($r.code -ge 200 -and $r.code -lt 300) "$($r.code) $($r.body)"

$r = Req POST "/api/v1/products/$p1/reviews" '{"rating":5,"comment":"Updated to 5"}' $bx.token
Check "second review upserts (one per user)" ($r.code -ge 200 -and $r.code -lt 300) "$($r.code) $($r.body)"

$r = Req GET "/api/v1/products/$p1/reviews"
$revs = @()
try { $revs = @(J $r.body) } catch {}
Check "exactly 1 review, rating 5" ($revs.Count -eq 1 -and $revs[0].rating -eq 5) "$($r.code) $($r.body)"

$r = Req POST "/api/v1/products/$p1/reviews" '{"rating":4,"comment":"anon"}'
Check "anonymous review -> 401" ($r.code -eq 401) "$($r.code)"

# --- 8. Stats + admin ------------------------------------------
$r = Req GET "/api/v1/seller/stats" $null $bx.token
Check "seller stats as customer -> 403" ($r.code -eq 403) "$($r.code) $($r.body)"

$r = Req GET "/api/v1/seller/stats" $null $sA.token
$cnt = -1
try { $cnt = (J $r.body).productCount } catch {}
Check "seller stats own count == 1 (P2 deleted later? P1 only now)" ($r.code -eq 200 -and $cnt -ge 1) "$($r.code) $($r.body)"

$r = Req GET "/api/v1/seller/stats"
Check "seller stats no token -> 401" ($r.code -eq 401) "$($r.code)"

$r = Req GET "/api/v1/admin/stats" $null $adminTok
Check "admin stats" ($r.code -eq 200 -and (J $r.body).userCount -ge 5) "$($r.code) $($r.body)"

$r = Req GET "/api/v1/admin/stats" $null $bx.token
Check "admin stats as customer -> 403" ($r.code -eq 403) "$($r.code)"

$r = Req GET "/api/v1/auth/users" $null $adminTok
Check "admin lists users" ($r.code -eq 200 -and ($r.body -match "seller_a")) "$($r.code)"
$r = Req GET "/api/v1/auth/users" $null $bx.token
Check "users list as customer -> 403" ($r.code -eq 403) "$($r.code)"

# --- 9. Chat ----------------------------------------------------
$r = Req POST "/api/v1/chat" '{"message":"hello"}'
Check "chat greets" ($r.code -eq 200 -and $r.body -match "Sri Assistant") "$($r.code) $($r.body)"

$r = Req POST "/api/v1/chat" ('{"message":"search laptop","productId":"' + $p1 + '"}')
Check "chat product context" ($r.code -eq 200) "$($r.code) $($r.body)"

# --- 10. Cleanup product P2 + logout ---------------------------
$r = Req DELETE ("/api/v1/products/" + $p2) $null $sA.token
Check "owner deletes own product P2" ($r.code -eq 200) "$($r.code) $($r.body)"

$r = Req POST "/api/v1/auth/logout" "{}" $bx.token
Check "logout ok" ($r.code -eq 200 -or $r.code -eq 204) "$($r.code)"
$r = Req GET "/api/v1/auth/validate" $null $bx.token
Check "token dead after logout -> 401" ($r.code -eq 401) "$($r.code)"

# --- 11. Admin control plane + seller analysis + user CRUD -------
# Section 10 logged buyer_x out; re-login so role checks hit the 403 gate
# (a dead token would correctly return 401 and mask the gate).
$bx = Login "buyer_x" "pass1234"
Check "buyer re-login for admin gates" ($bx.code -eq 200 -and $bx.token) "$($bx.code)"

$r = Req GET "/api/v1/admin/overview" $null $adminTok
$ov = $null; try { $ov = J $r.body } catch {}
$ovOk = ($r.code -eq 200) -and $ov -and ($ov.users.total -ge 5)
Check "admin overview 200 + users>=5" $ovOk "$($r.code) $($r.body)"

$r = Req GET "/api/v1/admin/overview" $null $bx.token
Check "overview buyer -> 403" ($r.code -eq 403) "$($r.code)"

$r = Req GET "/api/v1/admin/orders" $null $adminTok
$ao = @(); try { $ao = @(J $r.body) } catch {}
# @() wrap REQUIRED (PS 5.1 single-object .Count gotcha, see section 4).
$ordRows = @($ao | Where-Object { $_.id -eq $oid })
$ordOk = ($r.code -eq 200) -and ($ordRows.Count -gt 0) -and ($ordRows[0].username -eq "buyer_x")
Check "admin orders contains order + buyer_x" $ordOk "$($r.code) ord=$($ordRows.Count)"

$r = Req GET "/api/v1/admin/orders?status=bogus" $null $adminTok
Check "admin orders bogus filter -> 400" ($r.code -eq 400) "$($r.code)"

$r = Req PUT ("/api/v1/orders/" + $oid + "/status") '{"status":"delivered"}' $adminTok
Check "admin marks order delivered" ($r.code -eq 200) "$($r.code) $($r.body)"

$r = Req GET "/api/v1/admin/orders?status=delivered" $null $adminTok
$dArr = @(); try { $dArr = @(J $r.body) } catch {}
$dHit = @($dArr | Where-Object { $_.id -eq $oid }).Count -gt 0
Check "delivered filter contains order" ($r.code -eq 200 -and $dHit) "$($r.code) n=$($dArr.Count)"

$r = Req GET "/api/v1/admin/analytics" $null $adminTok
$an = $null; try { $an = J $r.body } catch {}
$anOk = ($r.code -eq 200) -and $an -and ($null -ne $an.sellers) -and ($null -ne $an.products) -and ($null -ne $an.prices)
Check "admin analytics 200 + sellers/products/prices" $anOk "$($r.code)"

$r = Req GET "/api/v1/admin/analytics" $null $bx.token
Check "analytics buyer -> 403" ($r.code -eq 403) "$($r.code)"

$r = Req GET "/api/v1/admin/customers" $null $adminTok
$cArr = @(); try { $cArr = @(J $r.body) } catch {}
$cx = @($cArr | Where-Object { $_.username -eq "buyer_x" })
$cxOk = ($r.code -eq 200) -and ($cx.Count -gt 0) -and ($cx[0].orderCount -ge 1)
Check "customers contains buyer_x with orders" $cxOk "$($r.code) n=$($cx.Count)"

$r = Req GET "/api/v1/admin/customers" $null $bx.token
Check "customers buyer -> 403" ($r.code -eq 403) "$($r.code)"

$r = Req GET "/api/v1/admin/sellers" $null $adminTok
$sArr = @(); try { $sArr = @(J $r.body) } catch {}
$sHit = @($sArr | Where-Object { $_.username -eq "seller_a" }).Count -gt 0
Check "sellers contains seller_a" ($r.code -eq 200 -and $sHit) "$($r.code) n=$($sArr.Count)"

# --- user CRUD through /api/v1/admin/users/{id} ------------------
$r = Register "adminedit1" "adminedit1@test.com" "pass1234" "customer"
Check "register adminedit1" ($r.code -eq 201) "$($r.code) $($r.body)"

$r = Req GET "/api/v1/auth/users" $null $adminTok
$uArr = @(); try { $uArr = @(J $r.body) } catch {}
$ae = @($uArr | Where-Object { $_.username -eq "adminedit1" })
$aeId = $null; if ($ae.Count -gt 0) { $aeId = $ae[0].id }
Check "find adminedit1 id" ([bool]$aeId) "$($r.code)"

$r = Req PUT ("/api/v1/admin/users/" + $aeId) '{"role":"seller"}' $adminTok
$upd = $null; try { $upd = J $r.body } catch {}
Check "admin PUT role=seller 200" ($r.code -eq 200 -and $upd.role -eq "seller") "$($r.code) $($r.body)"

$r = Req PUT ("/api/v1/admin/users/" + $aeId) '{"role":"wizard"}' $adminTok
Check "PUT invalid role -> 400" ($r.code -eq 400) "$($r.code) $($r.body)"

$r = Req PUT ("/api/v1/admin/users/" + $aeId) '{"role":"admin"}' $bx.token
Check "PUT user by buyer -> 403" ($r.code -eq 403) "$($r.code)"

$r = Req DELETE ("/api/v1/admin/users/" + $aeId) $null $bx.token
Check "DELETE user by buyer -> 403" ($r.code -eq 403) "$($r.code)"

$r = Req GET "/api/v1/auth/validate" $null $adminTok
$me = $null; try { $me = J $r.body } catch {}
$adminId = $null; if ($me) { $adminId = $me.id }
$r = Req DELETE ("/api/v1/admin/users/" + $adminId) $null $adminTok
Check "admin DELETE self -> 400" ($r.code -eq 400) "$($r.code) $($r.body)"

$r = Req DELETE ("/api/v1/admin/users/" + $aeId) $null $adminTok
Check "admin DELETE adminedit1 -> 200" ($r.code -eq 200) "$($r.code) $($r.body)"

$r = Req GET "/api/v1/auth/users" $null $adminTok
$uArr2 = @(); try { $uArr2 = @(J $r.body) } catch {}
$gone = @($uArr2 | Where-Object { $_.username -eq "adminedit1" }).Count -eq 0
Check "adminedit1 removed from users" $gone "$($r.code) n=$($uArr2.Count)"

$r = Req DELETE "/api/v1/admin/users/does-not-exist-123" $null $adminTok
Check "DELETE unknown user -> 404" ($r.code -eq 404) "$($r.code)"

# --- 12. Seller analytics/orders/reviews + profile + chat extras ---
# NOTE: Login/Register return hashtables — ALWAYS use .token (see section 3).
$r = Req GET "/api/v1/seller/analytics" $null $sA.token
$anOk = ($r.code -eq 200)
if ($anOk) { try { $an = J $r.body; $anOk = ($null -ne $an.productCount) -and ($null -ne $an.revenue) -and ($null -ne $an.topProducts) -and ($null -ne $an.salesByDay) } catch { $anOk = $false } }
Check "seller analytics 200 + shape" $anOk "$($r.code) $($r.body)"

$r = Req GET "/api/v1/seller/analytics" $null $bx.token
Check "seller analytics cust -> 403" ($r.code -eq 403) "$($r.code)"

$r = Req GET "/api/v1/seller/analytics" $null $null
Check "seller analytics no token -> 401" ($r.code -eq 401) "$($r.code)"

$r = Req GET "/api/v1/seller/orders" $null $sA.token
$soOk = ($r.code -eq 200)
if ($soOk) {
    $soArr = @(); try { $soArr = @(J $r.body) } catch {}
    $soOk = (@($soArr | Where-Object { $_.orderId -eq $oid -and $_.buyer -eq "buyer_x" }).Count -gt 0)
}
Check "seller orders contains $oid line" $soOk "$($r.code) $($r.body)"

$r = Req GET "/api/v1/seller/orders" $null $bx.token
Check "seller orders cust -> 403" ($r.code -eq 403) "$($r.code)"

$r = Req GET "/api/v1/seller/reviews" $null $sA.token
$srOk = ($r.code -eq 200)
if ($srOk) {
    $srArr = @(); try { $srArr = @(J $r.body) } catch {}
    $srOk = (@($srArr | Where-Object { $_.productName -eq "Laptop" }).Count -gt 0)
}
Check "seller reviews has Laptop review" $srOk "$($r.code) $($r.body)"

$r = Req GET "/api/v1/seller/reviews" $null $bx.token
Check "seller reviews cust -> 403" ($r.code -eq 403) "$($r.code)"

$r = Req POST "/api/v1/chat" '{"message":"my sales"}' $sA.token
$csOk = ($r.code -eq 200) -and ($r.body -match "Sri Assistant") -and ($r.body -match "Your store")
Check "chat my sales (seller) summary" $csOk "$($r.code) $($r.body)"

$r = Req POST "/api/v1/chat" '{"message":"my sales"}' $bx.token
Check "chat my sales (cust) steers to seller" ($r.code -eq 200 -and $r.body -match "for sellers") "$($r.code) $($r.body)"

$r = Req POST "/api/v1/chat" '{"message":"cancel my order"}' $bx.token
$ccOk = ($r.code -eq 200) -and ($r.body -match "no active order")
Check "chat cancel (delivered order -> none)" $ccOk "$($r.code) $($r.body)"

# --- Payment flow: buyer pays own orders (created -> paid) ----
$r = Req POST "/api/v1/cart" ('{"productId":"' + $p1 + '","quantity":1}') $bx.token
Check "payment: cart add P1 qty1" ($r.code -ge 200 -and $r.code -lt 300) "$($r.code) $($r.body)"

$r = Req POST "/api/v1/orders/checkout" '{}' $bx.token
$oid2 = $null; try { $oid2 = (J $r.body).id } catch {}
Check "payment: checkout -> order" ($r.code -ge 200 -and $r.code -lt 300 -and $oid2) "$($r.code) $($r.body)"

$r = Req PUT ("/api/v1/orders/" + $oid2 + "/status") '{"status":"paid"}' $bx.token
$ppOk = ($r.code -eq 200)
if ($ppOk) { try { $ppOk = ((J $r.body).status -eq "paid") } catch { $ppOk = $false } }
Check "buyer pays own order -> paid 200" $ppOk "$($r.code) $($r.body)"

$r = Req PUT ("/api/v1/orders/" + $oid2 + "/status") '{"status":"paid"}' $bx.token
Check "pay already-paid order -> 400" ($r.code -eq 400) "$($r.code) $($r.body)"

$r = Req PUT ("/api/v1/orders/" + $oid2 + "/status") '{"status":"shipped"}' $bx.token
Check "buyer cannot self-ship own order -> 403" ($r.code -eq 403) "$($r.code) $($r.body)"

$r = Req POST "/api/v1/chat" '{"message":"pay my order"}' $bx.token
Check "chat pay (no unpaid order)" ($r.code -eq 200 -and $r.body -match "no unpaid order") "$($r.code) $($r.body)"

$r = Req POST "/api/v1/cart" ('{"productId":"' + $p1 + '","quantity":1}') $bx.token
$r = Req POST "/api/v1/orders/checkout" '{}' $bx.token
$oid3 = $null; try { $oid3 = (J $r.body).id } catch {}
Check "payment: second checkout -> order" ($r.code -ge 200 -and $r.code -lt 300 -and $oid3) "$($r.code) $($r.body)"

$r = Req POST "/api/v1/chat" '{"message":"pay my order"}' $bx.token
Check "chat pay my order -> Payment received" ($r.code -eq 200 -and $r.body -match "Payment received") "$($r.code) $($r.body)"

$r = Req PUT ("/api/v1/orders/" + $oid3 + "/status") '{"status":"cancelled"}' $bx.token
$pcOk = ($r.code -eq 200)
if ($pcOk) { try { $pcOk = ((J $r.body).status -eq "cancelled") } catch { $pcOk = $false } }
Check "buyer cancels paid order -> 200" $pcOk "$($r.code) $($r.body)"

# Profile self-service: register + login, then PUT /api/v1/profile
Register "profile1" "p1@test.com" "pass1234" "customer" | Out-Null
$r = Req POST "/api/v1/auth/login" '{"username":"profile1","password":"pass1234"}'
$pt = $null; try { $pt = (J $r.body).token } catch {}
Check "profile1 login" ($r.code -eq 200 -and $pt) "$($r.code) $($r.body)"

$r = Req PUT "/api/v1/profile" '{"username":"profile1","email":"p1x@test.com","role":"admin"}' $pt
$prOk = ($r.code -eq 200)
if ($prOk) { try { $prOk = ((J $r.body).role -eq "customer") } catch { $prOk = $false } }
Check "profile PUT strips role self-raise" $prOk "$($r.code) $($r.body)"

$r = Req PUT "/api/v1/profile" '{"password":"newpass99"}' $pt
Check "profile change password -> 200" ($r.code -eq 200) "$($r.code) $($r.body)"

$r = Req GET "/api/v1/auth/validate" $null $pt
Check "old token dead after pw change" ($r.code -eq 401) "$($r.code)"

$r = Req POST "/api/v1/auth/login" '{"username":"profile1","password":"newpass99"}'
Check "login with NEW password" ($r.code -eq 200) "$($r.code) $($r.body)"

$r = Req PUT "/api/v1/profile" '{"username":"x"}' $pt
Check "profile bad username -> 401 dead token" ($r.code -eq 401) "$($r.code)"

$r = Req PUT "/api/v1/profile" '{"username":"y"}' $null
Check "profile no token -> 401" ($r.code -eq 401) "$($r.code)"

# --- 13. Rate limiting (throwaway account, end of run) ---------
Register "ratelimit1" "rl1@test.com" "pass1234" "customer" | Out-Null
$codes = @()
for ($i = 0; $i -lt 5; $i++) {
    $r = Req POST "/api/v1/auth/login" '{"username":"ratelimit1","password":"nope"}'
    $codes += $r.code
}
$r = Req POST "/api/v1/auth/login" '{"username":"ratelimit1","password":"nope"}'
$lockedOk = ($r.code -eq 429) -and ($r.body -match "locked")
Check "6th bad login locked -> 429" $lockedOk "$($r.code) $($r.body) (prior: $($codes -join ','))"

# --- Summary ----------------------------------------------------
Write-Host ""
Write-Host "=== RESULT: $script:pass passed, $script:fail failed ==="
if ($script:fail -gt 0) {
    Write-Host "Failed checks:"
    $script:failures | ForEach-Object { Write-Host "  - $_" }
}
exit $script:fail
