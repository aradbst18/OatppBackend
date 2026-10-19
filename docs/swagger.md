# Swagger / OpenAPI

Optional (`APP_ENABLE_SWAGGER`, `APP_WITH_SWAGGER`). When OFF, no swagger
code is compiled, `oatpp-swagger` is not searched or linked, and `/swagger/*`
+ `/api-docs/*` return 404 (verified).

## Runtime endpoints (when ON)

- `GET /api-docs/oas-3.0.0.json` — generated OpenAPI document.
- `GET /swagger/ui` — Swagger UI (assets from the configured `swagger.resPath`).
- `GET /swagger/{filename}` — UI assets.

## Resource path

`swagger.resPath` (`AppSettings::swaggerResPath`) defaults to the
oatpp-swagger library's own res dir (`OATPP_SWAGGER_RES_PATH`). Point it at
the installed `share/swagger-res` copy (or any custom dir) to relocate.
Resolution order at startup: configured path when it exists,
`OATPP_SWAGGER_RES_PATH` env var when it exists, exe-relative
`../share/swagger-res`, then the configured path as-is.

## How it is wired

`Application::initRouting()` merges endpoints from `authController`, `userController`, and
`echoController` into one `Endpoints` collection and calls
`infrastructure::registerSwaggerWithEndpoints`, which builds `DocumentInfo`
(title `OatppBackend API`, bearer scheme `bearer_auth`), generates the
OpenAPI 3.0 document with `oatpp::swagger::Generator`, and mounts
`oatpp::swagger::Controller` directly (no global oatpp components).

## Documenting an endpoint correctly (header declares, .cpp documents)

Headers declare only; bodies live in the `.cpp` guarded by `APP_WITH_SWAGGER`,
so swagger text is completely absent when the flag is off:

```cpp
// include/app/controllers/UserController.hpp
#if defined(APP_WITH_SWAGGER)
  ENDPOINT_INFO(updateUser);
#endif
ENDPOINT("PUT", "/users/{username}", updateUser, PATH(String, username), BODY_DTO(Object<dto::UpdateUserDto>, dto));
```

```cpp
// src/controllers/UserController.cpp
#if defined(APP_WITH_SWAGGER)
void UserController::Z__ENDPOINT_ADD_INFO_updateUser(
    const std::shared_ptr<oatpp::web::server::api::Endpoint::Info>& info) {
  info->summary = "Replace entire user record (or rename user)";
  info->description =
      "### Purpose\n"
      "Fully replaces the target user's credentials and role...\n"
      "### Workflow Example (JavaScript Fetch)\n"
      "```javascript\n"
      "const res = await fetch('/users/alice', { ... });\n"
      "```";
  info->addTag("users");
  info->authorization = "bearer_auth"; // required WITH the next line
  info->addSecurityRequirement("bearer_auth");
  info->addResponse<Object<dto::UserResponseDto>>(Status::CODE_200, "application/json");
  info->addResponse<String>(Status::CODE_400, "text/plain");
  info->addResponse<String>(Status::CODE_401, "text/plain");
  info->addResponse<String>(Status::CODE_403, "text/plain");
  info->addResponse<String>(Status::CODE_404, "text/plain");
  info->addResponse<String>(Status::CODE_409, "text/plain");
}
#endif // APP_WITH_SWAGGER
```

DTO fields follow the same split (`DTO_FIELD_INFO` in the header,
`Z__PROPERTY_ADD_INFO_*` in `src/dto/AuthDtos.cpp` and `src/dto/UserDtos.cpp`, only compiled when swagger is on):

```cpp
// header
#if defined(APP_WITH_SWAGGER)
  DTO_FIELD_INFO(username);
#endif
DTO_FIELD(String, username);
```

```cpp
// src/dto/UserDtos.cpp
#if defined(APP_WITH_SWAGGER)
void UserUpsertDto::Z__PROPERTY_ADD_INFO_username(
    oatpp::data::mapping::type::BaseObject::Property::Info* info) {
  info->required = true;
  info->pattern = "^[a-zA-Z0-9@#$]{4,18}$";
  info->description = "Account username identifier (4-18 characters)";
  info->example = oatpp::String("alice");
}
#endif
```

## OpenAPI Quality Standards

- **Exhaustive Status Codes:** List all statuses the handler can return: concrete DTO for `200`/`201` JSON, `String`/`text/plain` for error codes (400, 401, 403, 404, 409, 422, 500).
- **Authentication Dual-Config:** Authenticated endpoints must set **both** `authorization = "bearer_auth"` and `addSecurityRequirement("bearer_auth")` — the generator hard-asserts otherwise.
- **Frontend-Ready Examples:** Descriptions must include markdown formatting with parameter breakdowns, cURL commands, and JavaScript `fetch()` examples so frontend developers can integrate immediately without guessing.
- **Rich DTO Annotations:** DTO fields must declare `required`, `pattern`, `description`, and a realistic `example` string to provide complete documentation in Swagger UI.
