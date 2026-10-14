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

`Application::initRouting()` merges `authController->getEndpoints()` +
`echoController->getEndpoints()` into one `Endpoints` and calls
`infrastructure::registerSwaggerWithEndpoints`, which builds `DocumentInfo`
(title `OatppBackend API`, bearer scheme `bearer_auth`), generates the
document with `oatpp::swagger::Generator`, and mounts
`oatpp::swagger::Controller` directly (no global oatpp components).

## Documenting an endpoint correctly (header declares, .cpp documents)

Headers declare only; bodies live in the `.cpp` guarded by `APP_WITH_SWAGGER`,
so swagger text is completely absent when the flag is off:

```cpp
// include/app/controllers/AuthController.hpp
#if defined(APP_WITH_SWAGGER)
  ENDPOINT_INFO(whoAmI);
#endif
ENDPOINT("GET", "/auth/who-am-i", whoAmI, REQUEST(...));
```

```cpp
// src/controllers/AuthController.cpp
#if defined(APP_WITH_SWAGGER)
void AuthController::Z__ENDPOINT_ADD_INFO_whoAmI(
    const std::shared_ptr<oatpp::web::server::api::Endpoint::Info>& info) {
  info->summary = "...";
  info->description = "...";           // validation rules + auth scheme
  info->addTag("auth");
  info->authorization = "bearer_auth"; // required WITH the next line
  info->addSecurityRequirement("bearer_auth");
  info->addResponse<Object<dto::WhoAmIResponseDto>>(Status::CODE_200, "application/json");
  info->addResponse<String>(Status::CODE_401, "text/plain");
}
#endif // APP_WITH_SWAGGER
```

DTO fields follow the same split (`DTO_FIELD_INFO` in the header,
`Z__PROPERTY_ADD_INFO_*` in `src/dto/AuthDtos.cpp`, only compiled when swagger is on):

```cpp
// header
#if defined(APP_WITH_SWAGGER)
  DTO_FIELD_INFO(username);
#endif
DTO_FIELD(String, username);
```

```cpp
// src/dto/AuthDtos.cpp
#if defined(APP_WITH_SWAGGER)
void LoginRequestDto::Z__PROPERTY_ADD_INFO_username(
    oatpp::data::mapping::type::BaseObject::Property::Info* info) {
  info->required = true;
  info->pattern = "^[a-zA-Z0-9@#$]{4,18}$";
  info->description = "...";
}
#endif
```

Rules:

- List **only** statuses the handler can actually return: real DTO for `200` JSON, `String`/`text/plain` for errors.
- Authenticated endpoints must set **both** `authorization` and the security
  requirement — the generator hard-asserts otherwise.
- Descriptions must state validation rules and the `Authorization: Bearer`
  scheme so the docs match runtime behavior.
