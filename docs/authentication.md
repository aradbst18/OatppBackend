# Authentication

## Overview

- `JwtService` (`app/auth/`) is the only crypto owner: HS256 via jwt-cpp,
  claims `{iss, sub, role, session_version, iat, exp}`, 30s clock-skew leeway,
  issuer enforced. `sub` is a permanent account ID, not a username.
- `AuthService` checks credentials from `UserRepository` and issues tokens.
  `ensureSeeded()` provisions an administrator using explicit private credentials
  and returns the effective username for account protection in `UserService`.
- `AuthInterceptor` enforces authentication per request.

## Account identity and session lifetime

Each account receives a random 128-bit ID at creation. Renaming preserves that
ID; deleting and recreating a username creates a different ID. Refresh tokens
reference `users.id` through a foreign key with cascading deletion. SQLCipher
enables and verifies foreign-key enforcement for every pooled connection.

Every password, role, or username update increments `users.session_version` in
the same SQL statement as the account change. Existing refresh tokens and
access JWTs retain their issuance version and are rejected after the change.
This signs out all sessions, including the caller's own session. Deleting an
account removes its refresh tokens and causes access-token authentication to
fail. Bulk deletion preserves the protected administrator and its sessions.
Version-based invalidation is authoritative; old refresh rows need not have
their `revoked` flag changed to become unusable.

`AuthService::authenticate()` verifies the JWT cryptographically, then checks
that its account still exists and that its session version matches. Middleware
and controller fallback paths both use this method. `JwtService::verify()`
alone validates token structure and cryptography, not current account state.
`whoAmI()` resolves the username from the account record rather than from `sub`.

Login and refresh issue tokens inside a database transaction coordinated with
account writes. PostgreSQL locks the account row; SQLCipher uses
`BEGIN IMMEDIATE`. Refresh rechecks token state after obtaining the account
lock. Consuming the old token and storing its successor commit together, so
concurrent refreshes have one winner and an insertion failure leaves the
original token usable.

### Upgrading existing databases

Back up the database and stop older application instances before upgrading.
At startup, the account schema upgrade preserves usernames, password hashes,
and roles, assigns permanent IDs, and initializes session versions. The
upgrade rebuilds the application's users table transactionally and discards
all legacy username-bound refresh tokens; their historical owners cannot be
reconstructed safely. An upgrade failure rolls back the account migration.
Subsequent startups preserve the new IDs and sessions.

All users must sign in again after upgrading. Legacy access JWTs without a
session version are rejected immediately, even if their signatures and expiry
are valid. Do not run older application versions against the upgraded schema.
Any external consumer that treated JWT `sub` as a username must use the
username returned by login or `/auth/who-am-i` instead.

## Administrator provisioning

Set `APP_SEED_ADMIN_PASSWORD` to a unique private password for the first startup.
`APP_SEED_ADMIN_USERNAME` selects the account name and defaults to `admin` when
unset or empty. C++ callers may pass credentials to `ensureSeeded(username, password)`;
nonempty arguments override the corresponding environment variables.

When the selected account is absent, an unset or empty password aborts startup
before HTTP routing is created, without inserting an account. Usernames must
contain 4-18 characters from `[a-zA-Z0-9@#$]`. The initial password follows
the same policy as every new or changed account password below. No demo
account is created automatically in any build.

On subsequent starts, keep the selected username configured. A password is
not required again: bootstrap verifies that the existing account is an
administrator with a stored hash, and preserves its credentials and sessions.
A colliding non-administrator causes a configuration error; bootstrap never
promotes accounts or resets credentials. The selected administrator is protected
against deletion, renaming and demotion, including bulk operations. Password
changes retaining its `admin` role remain allowed.

## Password policy

Account creation, batch creation, PUT, PATCH and initial administrator provisioning
all use the shared password validator:

- Length: **15-128 Unicode code points**, encoded as valid UTF-8.
- Spaces, Unicode, digits, punctuation and letters are supported; no required
  mix of character types.
- Control characters (U+0000-U+001F and U+007F-U+009F) are rejected, including NUL.
- Entire passwords are compared case-sensitively with a bundled SecLists
  common-password list. Substrings and individual words are not banned.
- HTTP 422 responses explain either the length/encoding rule or that the
  password is too common. Password values are never echoed in errors.

This policy follows the length and blocklist approach in
[NIST SP 800-63B-4, section 3.1.1](https://pages.nist.gov/800-63-4/sp800-63b.html#passwordver).
There is no mandatory MFA in this application, so the minimum is 15 characters.
The 128-character maximum is an application limit, above NIST's recommended
support for at least 64 characters. Unicode is compared as submitted, without
normalization, trimming or truncation; visually identical Unicode sequences can
be different passwords. This is not a claim of complete NIST compliance.

**Existing credentials remain valid.** The new policy is checked only when
setting a password, never at login or when starting with an existing administrator.
Login accepts non-empty passwords up to 512 bytes (128 four-byte code points),
then verifies the complete supplied value against the stored hash. Replacing
an existing password invalidates its sessions.

### Updating the offline blocklist

The bundled dataset comes from the maintained
[SecLists repository](https://github.com/danielmiessler/SecLists/blob/master/Passwords/Common-Credentials/xato-net-10-million-passwords-1000000.txt).
The source is the one-million-entry sample of the Xato password corpus, ranked by
frequency. The bundled snapshot contains **10,908 distinct entries** that meet our
15-128 Unicode character rule and contain no control characters. Shorter, longer,
and control-containing passwords already fail validation and are excluded from
the bundle. This covers known weak long passwords, not every compromised password
or every predictable variation. Validation makes no network requests. Refresh the
snapshot when reviewing password policy or relevant threat information:

```bash
curl --fail --location \
  https://raw.githubusercontent.com/danielmiessler/SecLists/master/Passwords/Common-Credentials/xato-net-10-million-passwords-1000000.txt \
  -o /tmp/common-passwords.txt
python3 scripts/update_password_blocklist.py /tmp/common-passwords.txt
```

The generator filters by the password policy, sorts and deduplicates entries,
records the source SHA-256 and retained count, and runs small self-checks. Length
is counted in Unicode code points, not UTF-8 bytes; spaces are preserved. A source
with no eligible passwords is rejected before writing. Review the generated diff,
run the validation tests and rebuild before shipping. Keep the adjacent SecLists MIT
license with the generated file. No password-specific exceptions are added in code.

## Endpoints

### POST /auth/login (public)

Request: `{"username","password"}` (+ optional `pinCode` Int8 demo).
Validation failures -> 422; bad credentials -> 401 (unknown user and wrong
password are indistinguishable); success -> 200:

```json
{"access_token":"<jwt>","refresh_token":"<opaque-token>","token_type":"Bearer","username":"admin","role":"admin","expires_in":3600}
```

### POST /auth/refresh (public)

Request: `{"refresh_token":"<opaque-token>"}`. Success returns the login
response shape with a new access token and a replacement refresh token.
The previous refresh token cannot be reused. Expired, revoked, deleted-account,
or outdated-session tokens return 401; an empty token returns 400.

The application removes expired refresh-token rows in a background worker: one
pass after initialization, then one pass per minute, with at most 1,000 of the
oldest expired rows deleted per pass. An expiry index is added to new and existing
databases at startup. Both active and revoked rows remain until their original
expiry; no additional audit retention is applied. A deleted token still returns
401, reported as invalid rather than expired.

Cleanup runs separately from login and rotation transactions. Failed passes are
logged and retried on the next interval. PostgreSQL cleanup statements time out
after five seconds; SQLCipher uses its existing five-second busy timeout. Shutdown
wakes and joins the worker before closing the database. If expired rows arrive
faster than 1,000 per minute per instance, cleanup capacity must be increased.
Deleting rows permits database space reuse; it does not guarantee that the database
file immediately shrinks.

### GET /auth/who-am-i (authenticated)

Requires `Authorization: Bearer <jwt>`. Missing/malformed/expired/invalid ->
401 text/plain. Success -> 200:

```json
{"message":"Hello, authenticated world","username":"admin","role":"admin"}
```

## Middleware

`AuthInterceptor::intercept`:

1. Public path (`POST /auth/login`, `POST /auth/refresh`, `GET /ws/echo`, `/swagger/*`,
   `/api-docs/*`) -> return `nullptr` (continue).
2. Else extract `Bearer` token -> `AuthService::authenticate` -> stash
   `dto::AuthClaimsDto` in the request bundle under
   `middlewares::kClaimsBundleKey` -> `nullptr`.
3. Any failure throws `authentication::*` (`MissingCredentials` / `InvalidToken` /
   `TokenExpired`); `AppErrorHandler` maps it to `401 text/plain` (never throws past oatpp).

Controllers authenticate the header directly when the bundle is absent, so the
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

The handler selects fixed public messages rather than forwarding exception or
parser diagnostics. All 5xx messages are generic. `X-Error-Id` identifies the
corresponding internal log, which retains status and category with free-form
diagnostics redacted in full.

## Security notes

- Secrets (JWT secret, DB key, passwords, raw tokens) are never logged.
- Hash comparison failure shape is constant (no user enumeration).
- Token TTL is configurable (`jwtTtlSeconds`); expiry is enforced on every
  verification. Changing the JWT secret invalidates access JWTs; refresh
  tokens must also be invalidated through account/session state.
