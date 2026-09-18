#include "AuthController.h"

#include "ApiResponse.h"
#include "Database.h"
#include "JwtMiddleware.h"
#include "JwtUtils.h"

using namespace drogon;

namespace fleet
{

void AuthController::login(const HttpRequestPtr &req,
                           std::function<void(const HttpResponsePtr &)> &&callback)
{
    auto jsonBody = req->getJsonObject();
    if (!jsonBody || !jsonBody->isMember("username") || !jsonBody->isMember("password"))
    {
        callback(ApiResponse::error(k400BadRequest, "username and password are required"));
        return;
    }

    std::string username = (*jsonBody)["username"].asString();
    std::string password = (*jsonBody)["password"].asString();
    if (username.empty() || password.empty())
    {
        callback(ApiResponse::error(k400BadRequest, "username and password are required"));
        return;
    }

    std::string secret = config_.jwtSecret;

    Database::client()->execSqlAsync(
        "SELECT id::text AS id, (password_hash = crypt($1, password_hash)) AS match "
        "FROM users WHERE username = $2",
        [callback, username, secret](const orm::Result &result) {
            if (result.empty() || !result[0]["match"].as<bool>())
            {
                callback(ApiResponse::error(k401Unauthorized, "Invalid credentials"));
                return;
            }

            std::string userId = result[0]["id"].as<std::string>();
            std::string token = JwtUtils::signJwt(userId, username, secret);
            JwtMiddleware::instance().addSession(token, userId);

            Json::Value user;
            user["id"] = userId;
            user["username"] = username;

            Json::Value resBody;
            resBody["token"] = token;
            resBody["user"] = user;
            callback(ApiResponse::json(resBody));
        },
        [callback](const orm::DrogonDbException &) {
            callback(ApiResponse::error(k500InternalServerError, "Database error"));
        },
        password,
        username);
}

void AuthController::logout(const HttpRequestPtr &req,
                            std::function<void(const HttpResponsePtr &)> &&callback)
{
    HttpResponsePtr authErr;
    std::string userId, username;
    if (!JwtMiddleware::instance().verifyRequest(req, authErr, userId, username))
    {
        callback(authErr);
        return;
    }

    JwtMiddleware::instance().removeSession(JwtMiddleware::extractBearerToken(req));

    Json::Value resBody;
    resBody["message"] = "logged out";
    callback(ApiResponse::json(resBody));
}

void AuthController::me(const HttpRequestPtr &req,
                        std::function<void(const HttpResponsePtr &)> &&callback)
{
    HttpResponsePtr authErr;
    std::string userId, username;
    if (!JwtMiddleware::instance().verifyRequest(req, authErr, userId, username))
    {
        callback(authErr);
        return;
    }

    Json::Value resBody;
    resBody["id"] = userId;
    resBody["username"] = username;
    callback(ApiResponse::json(resBody));
}

}  // namespace fleet
