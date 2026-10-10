// ============================================================
// MODULE: config/api-config.js
// PURPOSE: Single source of truth for the API base URL used by every page.
// WHY: Local dev serves UI+API from the same C++ server (same-origin), but
//      the Vercel (UI) + Render (API) split deploy needs an absolute backend
//      URL. Loading this file keeps that decision out of the 5 HTML pages.
// INPUT:  window.SRI_API_BASE (edit the one line below) and/or
//         localStorage["sri_api_base"] (runtime override, no redeploy).
// OUTPUT: window.sriApiBase() -> "https://host" or "" (same-origin).
// ARCHITECTURE: Frontend config
// ============================================================

// >>> EDIT THIS ONE LINE for split deploys, e.g.:
//     window.SRI_API_BASE = "https://srinivasan-mart.onrender.com";
// Leave "" for local dev (the C++ server serves the UI itself).
window.SRI_API_BASE = "";

// Returns the API base URL:
//   1. localStorage "sri_api_base"  (runtime override from browser console:
//      localStorage.setItem("sri_api_base","https://<api>") then reload)
//   2. window.SRI_API_BASE          (deploy-time value edited above)
//   3. window.location.origin       (same-origin local dev fallback)
// Trailing slashes are stripped so callers can do base + "/api/v1/...".
function sriApiBase() {
    try {
        var local = window.localStorage && window.localStorage.getItem("sri_api_base");
        if (local && local.trim()) return local.trim().replace(/\/+$/, "");
    } catch (e) { /* localStorage may be unavailable (privacy mode) */ }
    if (typeof window.SRI_API_BASE === "string" && window.SRI_API_BASE.trim()) {
        return window.SRI_API_BASE.trim().replace(/\/+$/, "");
    }
    return window.location.origin;
}
