# Adding a New Controller

Location: `include/app/controllers/<Name>Controller.hpp`,
`src/controllers/<Name>Controller.cpp`.

## Rules

1. Inherit `oatpp::web::server::api::ApiController` between
   `OATPP_CODEGEN_BEGIN/END(ApiController)`.
2. Constructor-inject everything (`objectMapper` + services). No
   `OATPP_COMPONENT`, no service instantiation inside the controller.
3. Declare endpoints with `ENDPOINT(...)` (no body in the header); define
   `std::shared_ptr<...::OutgoingResponse> Class::method(...)` in the `.cpp`.
4. `ENDPOINT_INFO` is a guarded declaration in the header
   (`ENDPOINT_INFO(name);` inside `#if defined(APP_WITH_SWAGGER)`); the
   `Z__ENDPOINT_ADD_INFO_name` body lives in the `.cpp` under the same guard
   with summary, description, consumes, all producible responses with real
   DTO types, and a tag.
5. Authenticated endpoints set **both**:
   `info->authorization = "bearer_auth";` and
   `info->addSecurityRequirement("bearer_auth");`
6. Handler skeleton (success via `createDtoResponse`, failures via
   domain `throw` mapped by `AppErrorHandler` to `Status + text/plain`):

```cpp
std::shared_ptr<ThingController::OutgoingResponse> ThingController::get(
    const oatpp::Object<dto::ThingRequest>& body) {
  if (!body || !body->name) {
    throw errors::request::Missing("name is required");
  }
  const auto result = service_->doThing(body->name->c_str());  // throws validation::InvalidValue, ...
  auto out = dto::ThingResponse::createShared();
  ...
  return createDtoResponse(Status::CODE_200, out);
  // `AppException` bubbles to AppErrorHandler -> text/plain; unexpected -> 500.
  // For fail-closed direct returns: return createResponse(Status::CODE_400, "reason");
}
```

7. Register in `Application::initRouting()` and append endpoints to `merged`.
8. Add HTTP integration tests (success + every error status).

See `app/controllers/AuthController.hpp` and `src/controllers/AuthController.cpp`.
