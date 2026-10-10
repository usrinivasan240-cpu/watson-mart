// ============================================================
// MODULE: ChatController
// PURPOSE: AI chatbot stub (Weeks 9-10).
// WHY: Rule-based reply now; LLM integration planned.
// INPUT: {message} JSON.
// OUTPUT: {reply} JSON.
// ARCHITECTURE: Controller
// ============================================================

#pragma once
#include <drogon/drogon.h>

class ChatController
{
public:
    static void chat(const drogon::HttpRequestPtr &req,
        std::function<void(const drogon::HttpResponsePtr &)> &&callback);
    static void initRoutes();
};
