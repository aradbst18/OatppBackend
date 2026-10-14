# Adding a New Service

Location: `include/app/services/`, `src/services/`.

## Responsibilities

- Exactly one method per use-case with plain C++ signatures.
- Owns orchestration: validation -> repository calls -> token/derived data.
- Throws `AppException` subtypes only (`ValidationError` for bad input,
  `authentication::*`/`authorization::*` for auth, `database::*` relayed, ...).

## Dependency rules

- May depend on: repositories, `JwtService`, `domain` types, `validation`
  helpers, `logging`.
- Must NOT depend on: oatpp HTTP types, DTOs, controllers, `IDatabase`
  directly (go through a repository), config file formats.
- Constructor-inject all collaborators as `shared_ptr`; throw `InternalError`
  when a required collaborator is null.

## Example

`AuthService::login(username, password)`:

1. `isValidUsername/isValidPassword` -> `ValidationError` (422).
2. `users_->findByUsername` + hash compare -> `Unauthorized` (401), with
   identical failure shape for unknown user vs wrong password.
3. `jwt_->create` -> `LoginResult{accessToken, username, role, expiresIn}`.

Seed data (if any) goes in an explicit `ensureSeeded()` called from
`Application::initServices()`, never lazily inside request handling.
