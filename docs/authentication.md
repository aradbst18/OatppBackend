# Authentication

## Overview

- `JwtService` (`app/auth/`) is the only crypto owner: HS256 via jwt-cpp,
  claims `{iss, sub, role, iat, exp}`, 30s clock-skew leeway, issuer enforced.
- `AuthService` checks credentials from `UserRepository` and issues tokens.
  `ensureSeeded()` provisions an administrator using explicit private credentials
  and returns the effective username for account protection in `UserService`.
- `AuthInterceptor` enforces authentication per request.

## Administrator provisioning

Set `APP_SEED_ADMIN_PASSWORD` to a unique private password for the first startup.
`APP_SEED_ADMIN_USERNAME` selects the account name and defaults to `admin` when
unset or empty. C++ callers may pass credentials to `ensureSeeded(username, password)`;
nonempty arguments override the corresponding environment variables.

When the selected account is absent, an unset or empty password aborts startup
before HTTP routing is created, without inserting an account. Usernames must
contain 4-18 characters from `[a-zA-Z0-9@#$]`. Passwords must contain 6-48
characters, including a letter and a digit, with no NUL characters. Use a long,
randomly generated password within those limits. The published `Admin1234`
password is rejected even when explicitly supplied. No demo account is created
automatically in any build.

On subsequent starts, keep the selected username configured. The password is
not required again: bootstrap verifies that the existing account is an
administrator and preserves its stored password, including later password
changes. A colliding non-administrator account causes a configuration error;
bootstrap never promotes it or resets its credentials. The selected account
is protected against deletion and renaming, including bulk deletion.

For an installation created with the former `Admin1234` password, startup now
fails while the selected account still uses it. Changing
`APP_SEED_ADMIN_PASSWORD` does not reset an existing account. Before restarting,
use a trusted offline maintenance process to replace the stored password hash
with one produced by `auth::passwordHash()` for a new private password. Audit
other former bootstrap accounts as well if the configured username has changed.
Revoke affected refresh tokens and rotate `jwt.secret` to invalidate previously
issued access tokens. JWT secret rotation alone does not revoke refresh tokens.
Provisioning neither resets passwords nor invalidates sessions automatically.

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
   `dto::AuthClaimsDto` in the request bundle under
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
