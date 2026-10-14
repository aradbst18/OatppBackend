#include "app/controllers/AuthController.hpp"

#include "app/errors/AppException.hpp"
#include "app/middlewares/AuthInterceptor.hpp"
#include "app/validation/Validators.hpp"

namespace app::controllers
{

AuthController::AuthController(const std::shared_ptr<ObjectMapper>& objectMapper, std::shared_ptr<services::AuthService> authService,
                               std::shared_ptr<auth::JwtService> jwtService)
    : ApiController(objectMapper), authService_(std::move(authService)), jwtService_(std::move(jwtService))
{
    if (!authService_ || !jwtService_)
    {
        throw errors::internal::OperationFailed("AuthController requires AuthService and JwtService");
    }
}

#ifdef APP_WITH_SWAGGER

void AuthController::Z__ENDPOINT_ADD_INFO_login(const std::shared_ptr<oatpp::web::server::api::Endpoint::Info>& info)
{
    info->summary = "Log in and obtain a JWT access token";
    info->description =
        R"DOC(Public endpoint — **no `Authorization` header required**. Validates the supplied credentials against the user store and, on success, returns a signed JWT access token plus the caller's identity.

## Frontend workflow (recommended)

1. `POST /auth/login` with JSON `{"username": "...", "password": "..."}` and `Content-Type: application/json`.
2. On `200`, persist `access_token` (memory is safest; `sessionStorage`/`localStorage` only if you accept XSS trade-offs) and its `expires_in` (seconds, currently `3600`).
3. Send it on every protected call as `Authorization: Bearer <access_token>`.
4. When the token expires the API answers `401` — redirect to login and re-authenticate.
5. Use `GET /auth/who-am-i` right after login to verify your wiring.

```js
// Minimal browser example (errors are text/plain, success is JSON)
const res = await fetch("http://localhost:8080/auth/login", {
  method: "POST",
  headers: { "Content-Type": "application/json" },
  body: JSON.stringify({ username: "admin", password: "Admin1234" })
});
if (!res.ok) { throw new Error(await res.text()); }
const { access_token, username, role, expires_in } = await res.json();
// -> use `access_token` as `Authorization: Bearer ${access_token}`
```

```bash
# cURL equivalent (seeded demo account)
curl -s http://localhost:8080/auth/login \
  -H 'Content-Type: application/json' \
  -d '{"username":"admin","password":"Admin1234"}' | jq
```

## Request rules

- `username`: **4-18** chars, charset `[a-zA-Z0-9@#$]`, case-sensitive.
- `password`: **6-48** chars, must contain **at least one letter and one digit**.
- `pinCode` (optional): demo `Int8` field (`-128..127`). Out-of-range values never reach the controller — deserialization fails first with `400`. Omit it unless you are testing integer range validation.
- Seeded demo account: `admin` / `Admin1234` (created on first boot if missing).

## How errors are reported

Success (`200`) is JSON (`LoginResponseDto`). Failures are `HTTP Status + text/plain message`
via oatpp's `createResponse` / `AppErrorHandler` — no numeric envelope. The status line
carries the machine-readable part, the body the human-readable `what()` (safe to show
in the UI, never contains secrets or hashes). Domain exceptions thrown are
`app::errors::{request,authentication,validation,runtime,...}` mapped to status:

| HTTP | Domain exception | When it happens | Frontend hint |
|------|------------------|-----------------|---------------|
| 200 | — | Credentials valid; token issued. | Store token, navigate to app. |
| 400 | `request::Missing`, `serialization::ParsingFailed` | Missing `username`/`password`, malformed JSON, or `pinCode` out of range. | Fix the request shape; highlight the offending field. |
| 401 | `authentication::InvalidCredentials` | Shape was valid but credentials are wrong (unknown user and wrong password look identical on purpose). | Show "Invalid username or password". |
| 422 | `validation::InvalidValue` | `username`/`password` fail the shape rules above (too short/long, illegal chars, password without letter+digit). | Show field-level validation before retrying. |
| 500 | `runtime::ExecutionFailed`, `internal::OperationFailed` | Unexpected server failure (never leaks internals). | Retry later / report. |

Note the deliberate `422` vs `401` split: `422` means "fix the input shape", `401` means "shape was fine, credentials are wrong". The `401` body is constant-shape so callers cannot enumerate users.)DOC";

    info->addTag("auth");
    info->body.description =
        "Login credentials as JSON. `username` and `password` are required; `pinCode` is an "
        "optional Int8 demo field.";

    auto example = dto::LoginRequestDto::createShared();
    example->username = oatpp::String("admin");
    example->password = oatpp::String("Admin1234");
    info->addConsumes<Object<dto::LoginRequestDto>>("application/json", "Login credentials.").addExample("Seeded admin login", example);

    auto ok = dto::LoginResponseDto::createShared();
    ok->accessToken = oatpp::String(
        "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJvYXRwcC1iYWNrZW5kIiwic3ViIjoiYWRtaW4i"
        "LCJyb2xlIjoiYWRtaW4iLCJpYXQiOjE3MDAwMDAwMDAsImV4cCI6MTcwMDAwMzYwMH0."
        "example-signature-truncated");
    ok->tokenType = oatpp::String("Bearer");
    ok->username = oatpp::String("admin");
    ok->role = oatpp::String("admin");
    ok->expiresIn = 3600;

    info->addResponse<Object<dto::LoginResponseDto>>(Status::CODE_200, "application/json",
                                                     "Login succeeded. `access_token` is a signed JWT (HS256) valid for `expires_in` "
                                                     "seconds; send it as `Authorization: Bearer <access_token>`. `token_type` is always "
                                                     "`\"Bearer\"`.")
        .addExample("Successful login", ok);

    info->addResponse<String>(Status::CODE_400, "text/plain",
                              "Bad request — the server could not process the body (`request::Missing` / "
                              "`serialization::ParsingFailed`). Missing `username`/`password`, malformed JSON, "
                              "wrong `Content-Type`, or `pinCode` outside Int8 range. Body names the cause.")
        .addExample("Missing username/password", oatpp::String("username and password are required"))
        .addExample("Malformed JSON body", oatpp::String("Failed to parse request JSON: unexpected end of input"))
        .addExample("pinCode out of Int8 range", oatpp::String("pinCode out of range [-128, 127]"));

    info->addResponse<String>(Status::CODE_401, "text/plain",
                              "Unauthorized — `authentication::InvalidCredentials`: shape was valid but the username "
                              "is unknown or the password does not match the stored password hash. Identical for "
                              "both "
                              "cases so callers cannot enumerate users. Do NOT retry with the same values.")
        .addExample("Wrong password or unknown user", oatpp::String("Invalid credentials"));

    info->addResponse<String>(Status::CODE_422, "text/plain",
                              "Validation failed — `validation::InvalidValue`: `username`/`password` violate the "
                              "shape "
                              "rules (length, charset, letter+digit requirement). Validate in the UI before sending.")
        .addExample("Illegal username shape", oatpp::String("Invalid username: 4-18 chars of [a-zA-Z0-9@#$]"))
        .addExample("Weak password shape", oatpp::String("Invalid password: 6-48 chars with a letter and a digit"));

    info->addResponse<String>(Status::CODE_500, "text/plain",
                              "Internal error — `runtime::ExecutionFailed` / `internal::OperationFailed`: unexpected "
                              "server failure. Never leaks internals; body stays generic.")
        .addExample("Unexpected failure", oatpp::String("an internal server error occurred"));
}

void AuthController::Z__ENDPOINT_ADD_INFO_whoAmI(const std::shared_ptr<oatpp::web::server::api::Endpoint::Info>& info)
{
    info->summary = "Describe the authenticated caller (who-am-i)";
    info->description =
        R"DOC(Protected endpoint — requires `Authorization: Bearer <jwt>` where `<jwt>` is the `access_token` from `POST /auth/login`. Returns the `username` and `role` embedded in the verified token, plus a greeting. Use it as a login smoke-test and as the canonical "am I still authenticated?" probe.

## How authentication flows

1. `AuthInterceptor` runs before routing: it extracts the bearer token, verifies signature + issuer + expiry (30s clock skew allowed), and stashes `subject|role|issuer` in the request bundle. Failures throw `authentication::*` which `AppErrorHandler` maps to `401 text/plain`.
2. This endpoint reads the bundle first. If the interceptor was bypassed (e.g. direct router use in tests) it **fails closed** by verifying the header itself — an absent or bad token still yields `401`.

## Calling it from Swagger UI

1. Call `POST /auth/login` first and copy `access_token`.
2. Click **Authorize** (top-right), enter `Bearer <access_token>` (or just the token), then **Authorize -> Close**.
3. Execute this endpoint — the lock icon confirms the `bearer_auth` scheme is attached.

## Calling it from the frontend

```js
const res = await fetch("http://localhost:8080/auth/who-am-i", {
  headers: { "Authorization": `Bearer ${accessToken}` }
});
if (res.status === 401) { /* token missing/expired/invalid -> go to login */ }
const { username, role, message } = await res.json();
```

```bash
curl -s http://localhost:8080/auth/who-am-i \
  -H "Authorization: Bearer <access_token>" | jq
```

## Responses

- `200` JSON: token verified; body is `{message, username, role}`. `role` comes from the `role` JWT claim (`admin`/`operator`/…).
- `401 text/plain`: any auth failure — `authentication::MissingCredentials` (absent/malformed header, must be exactly `Bearer <token>`, empty token), `authentication::InvalidToken` (bad signature/issuer/empty subject) or `authentication::TokenExpired` (TTL 3600s, 30s leeway). Treat every `401` here as "re-login required".)DOC";

    info->addTag("auth");
    info->authorization = "bearer_auth";
    info->addSecurityRequirement("bearer_auth");

    auto ok = dto::WhoAmIResponseDto::createShared();
    ok->message = oatpp::String("Hello, authenticated world");
    ok->username = oatpp::String("admin");
    ok->role = oatpp::String("admin");
    info->addResponse<Object<dto::WhoAmIResponseDto>>(Status::CODE_200, "application/json",
                                                      "Authenticated. `username`/`role` are the verified JWT subject and `role` claim; "
                                                      "`message` is a static greeting proving the request passed auth.")
        .addExample("Authenticated caller", ok);

    info->addResponse<String>(Status::CODE_401, "text/plain",
                              "Unauthorized — no usable bearer token. `authentication::MissingCredentials` (header "
                              "absent/malformed, must be `Bearer <token>`, or token empty), "
                              "`authentication::InvalidToken` (signature/issuer/empty-subject failure) or "
                              "`authentication::TokenExpired` (TTL 3600s, 30s leeway). Frontend should discard the "
                              "stored token and redirect to login.")
        .addExample("Missing or malformed Authorization header", oatpp::String("Missing or malformed Authorization header"))
        .addExample("Invalid token", oatpp::String("Invalid token"))
        .addExample("Expired token", oatpp::String("Invalid or expired token"))
        .addExample("Empty bearer token", oatpp::String("Missing token"));
}

#endif  // APP_WITH_SWAGGER

std::shared_ptr<AuthController::OutgoingResponse> AuthController::login(const oatpp::Object<dto::LoginRequestDto>& body)
{
    // project convention: validate shape by throwing domain exceptions and let
    // `AppErrorHandler` map them to Status + text/plain. Success uses oatpp's
    // `createDtoResponse` (JSON) directly — no custom error envelope.
    if (!body || !body->username || !body->password)
    {
        throw errors::request::Missing("username and password are required");
    }

    const services::LoginResult result = authService_->login(body->username->c_str(), body->password->c_str());
    auto out = dto::LoginResponseDto::createShared();
    out->accessToken = oatpp::String(result.accessToken.c_str());
    out->tokenType = oatpp::String("Bearer");
    out->username = oatpp::String(result.username.c_str());
    out->role = oatpp::String(result.role.c_str());
    out->expiresIn = result.expiresInSeconds;
    return createDtoResponse(Status::CODE_200, out);
}

std::shared_ptr<AuthController::OutgoingResponse> AuthController::whoAmI(const std::shared_ptr<IncomingRequest>& request)
{
    try
    {
        // Primary path: claims stashed by AuthInterceptor.
        std::string subject;
        std::string role;
        const oatpp::String bundled = request->getBundleData<oatpp::String>(middlewares::kClaimsBundleKey);

        if (bundled)
        {
            const std::string text = bundled->c_str();
            const auto first = text.find('|');
            const auto second = text.find('|', first + 1);

            if (first != std::string::npos && second != std::string::npos)
            {
                subject = text.substr(0, first);
                role = text.substr(first + 1, second - first - 1);
            }
        }

        // Fail-closed fallback: verify the header directly so the endpoint is
        // safe even if the interceptor was not registered.
        auth::TokenClaims claims;
        if (!subject.empty())
        {
            claims.subject = subject;
            claims.role = role;
            claims.issuer = jwtService_->issuer();
        }
        else
        {
            const auto header = request->getHeader("Authorization");
            const std::string value = header ? header->c_str() : "";
            claims = jwtService_->verify(auth::JwtService::extractBearer(value));
        }

        const services::LoginResult result = authService_->whoAmI(claims);
        auto out = dto::WhoAmIResponseDto::createShared();
        out->message = oatpp::String("Hello, authenticated world");
        out->username = oatpp::String(result.username.c_str());
        out->role = oatpp::String(result.role.c_str());
        return createDtoResponse(Status::CODE_200, out);
    }
    catch (const errors::AppException&)
    {
        throw;
    }
    catch (...)
    {
        throw errors::authentication::InvalidToken("Invalid token");
    }
}

}  // namespace app::controllers
