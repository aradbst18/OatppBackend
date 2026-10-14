# Authentication

## Overview

- `JwtService` (`app/auth/`) is the only crypto owner: HS256 via jwt-cpp,
  claims `{iss, sub, role, iat, exp}`, 30s clock-skew leeway, issuer enforced.
- `AuthService` checks demo credentials from `UserRepository` and issues
  tokens. Seed account: `admin / Admin1234` (role `admin`), created once via
  `ensureSeeded()`.
- `AuthInterceptor` enforces authentication per request.

## Endpoints

### POST /auth/login (public)

Request: `{"username","password"}` (+ optional `pinCode` Int8 demo).
Validation failures -> 422; bad credentials -> 401 (unknown user and wrong
password are indistinguishable); success -> 200:

```json
{"access_token":"<jwt>","token_type":"Bearer","username":"admin","role":"admin","expires_in":3600}
```

### GET /auth/who-am-i (authenticated)

Requires `Authorization: Bearer <jwt>`. Missing/malformed/expired/invalid ->
401 text/plain. Success -> 200:

```json
{"message":"Hello, authenticated world","username":"admin","role":"admin"}
```

## Middleware

`AuthInterceptor::intercept`:

1. Public path (`POST /auth/login`, `GET /ws/echo`, `/swagger/*`,
   `/api-docs/*`) -> return `nullptr` (continue).
2. Else extract `Bearer` token -> `JwtService::verify` -> stash
   `subject|role|issuer` in the request bundle under
   `middlewares::kClaimsBundleKey` -> `nullptr`.
3. Any failure throws `authentication::*` (`MissingCredentials` / `InvalidToken` /
   `TokenExpired`); `AppErrorHandler` maps it to `401 text/plain` (never throws past oatpp).

Controllers re-verify the header directly when the bundle is absent, so the
endpoint fails closed even if the interceptor is skipped.

## Error handling

Success is JSON via `createDtoResponse`. Failures are `Status + text/plain`
via `createResponse` / `AppErrorHandler` — no numeric envelope. Domain exceptions
(`request::Missing`, `authentication::InvalidCredentials`, `validation::InvalidValue`,
`filesystem::*`, `database::*`, `serialization::*`, `config::*`, `crypto::*`, ...) map
to status; e.g. `401` body is just:

```text
Invalid credentials
```

## Security notes

- Secrets (JWT secret, DB key, passwords, raw tokens) are never logged.
- Hash comparison failure shape is constant (no user enumeration).
- Token TTL is configurable (`jwtTtlSeconds`); expiry is enforced on every
  verification. Changing the secret invalidates all tokens.
