#include "app/controllers/UserController.hpp"

#include <cctype>

#include "app/dto/AuthDtos.hpp"
#include "app/errors/AppException.hpp"
#include "app/middlewares/AuthInterceptor.hpp"

namespace app::controllers
{

namespace
{

std::string textOf(const oatpp::String& value)
{
    return value ? std::string(*value) : std::string();
}

}  // namespace

UserController::UserController(const std::shared_ptr<ObjectMapper>& objectMapper,
                               std::shared_ptr<services::UserService> userService,
                               std::shared_ptr<auth::JwtService> jwtService)
    : ApiController(objectMapper), userService_(std::move(userService)), jwtService_(std::move(jwtService))
{
    if (!userService_ || !jwtService_)
    {
        throw errors::internal::OperationFailed("UserController requires UserService and JwtService");
    }
}

void UserController::ensureAuthenticated(const std::shared_ptr<IncomingRequest>& request)
{
    try
    {
        const auto bundled = request->getBundleData<oatpp::Object<dto::AuthClaimsDto>>(middlewares::kClaimsBundleKey);
        if (bundled && bundled->subject)
        {
            return;
        }
    }
    catch (...)
    {
    }

    const auto header = request->getHeader("Authorization");
    const std::string value = header ? header->c_str() : "";

    jwtService_->verify(auth::JwtService::extractBearer(value));
}

#ifdef APP_WITH_SWAGGER

void UserController::Z__ENDPOINT_ADD_INFO_listUsers(
    const std::shared_ptr<oatpp::web::server::api::Endpoint::Info>& info)
{
    info->summary = "Retrieve a list of all registered users";
    info->description =
        R"DOC(Protected endpoint — **requires `Authorization: Bearer <token>` header**. Returns an array of all registered accounts ordered alphabetically by username.

## Frontend workflow

1. Ensure the user is authenticated and obtain the JWT `access_token` from `POST /auth/login`.
2. Send `GET /users` with `Authorization: Bearer <access_token>`.
3. If the token is invalid or expired, the endpoint responds with `401 Unauthorized` — prompt the user to re-login.
4. On `200 OK`, render the returned user list. Note that password hashes are never exposed.

```js
// Browser fetch example
const res = await fetch("http://localhost:8080/users", {
  method: "GET",
  headers: {
    "Authorization": `Bearer ${accessToken}`
  }
});
if (!res.ok) { throw new Error(await res.text()); }
const users = await res.json();
// users -> [{ username: "admin", role: "admin" }, { username: "operator1", role: "operator" }]
```

```bash
# cURL equivalent
curl -s http://localhost:8080/users \
  -H "Authorization: Bearer ${ACCESS_TOKEN}" | jq
```

## Error responses

| HTTP | Domain exception | When it happens | Frontend hint |
|------|------------------|-----------------|---------------|
| 200 | — | Success. Returns JSON array of users. | Render user management table. |
| 401 | `authentication::InvalidToken` | Token missing, invalid signature, or expired. | Redirect to login. |
| 500 | `internal::OperationFailed` | Database or unexpected server error. | Show error notification. |)DOC";

    info->addTag("users");
    info->authorization = "bearer_auth";
    info->addSecurityRequirement("bearer_auth");

    auto listExample = oatpp::Vector<oatpp::Object<dto::UserResponseDto>>::createShared();
    auto adminUser = dto::UserResponseDto::createShared();
    adminUser->username = oatpp::String("admin");
    adminUser->role = oatpp::String("admin");
    listExample->push_back(adminUser);

    auto opUser = dto::UserResponseDto::createShared();
    opUser->username = oatpp::String("operator1");
    opUser->role = oatpp::String("operator");
    listExample->push_back(opUser);

    info->addResponse<oatpp::Vector<oatpp::Object<dto::UserResponseDto>>>(Status::CODE_200, "application/json",
                                                                          "Array of user records.")
        .addExample("Standard user list", listExample);

    info->addResponse<oatpp::String>(Status::CODE_401, "text/plain", "Missing or expired bearer token.");
}

void UserController::Z__ENDPOINT_ADD_INFO_getUser(const std::shared_ptr<oatpp::web::server::api::Endpoint::Info>& info)
{
    info->summary = "Retrieve a single user by username";
    info->description =
        R"DOC(Protected endpoint — **requires `Authorization: Bearer <token>` header**. Returns the profile and role for the specified username.

## Frontend workflow

1. Send `GET /users/{username}` with the target username in the URL path.
2. If the user does not exist, the API responds with `404 Not Found`.
3. On `200 OK`, display the account details.

```js
// Browser fetch example
const res = await fetch(`http://localhost:8080/users/${encodeURIComponent(username)}`, {
  method: "GET",
  headers: { "Authorization": `Bearer ${accessToken}` }
});
if (res.status === 404) { alert("User not found"); return; }
if (!res.ok) { throw new Error(await res.text()); }
const user = await res.json();
```

```bash
# cURL equivalent
curl -s http://localhost:8080/users/alice \
  -H "Authorization: Bearer ${ACCESS_TOKEN}" | jq
```

## Error responses

| HTTP | Domain exception | When it happens | Frontend hint |
|------|------------------|-----------------|---------------|
| 200 | — | User found. Returns `UserResponseDto`. | Display user profile. |
| 400 | `request::Missing` | Username path parameter was empty. | Verify URL formation. |
| 401 | `authentication::InvalidToken` | Missing, invalid, or expired bearer token. | Redirect to login. |
| 404 | `database::RecordNotFound` | No account exists with this username. | Show "User not found" message. |)DOC";

    info->addTag("users");
    info->authorization = "bearer_auth";
    info->addSecurityRequirement("bearer_auth");

    info->pathParams["username"].description = "Exact username of the account to look up.";
    info->pathParams["username"].required = true;

    auto userExample = dto::UserResponseDto::createShared();
    userExample->username = oatpp::String("alice01");
    userExample->role = oatpp::String("operator");

    info->addResponse<oatpp::Object<dto::UserResponseDto>>(Status::CODE_200, "application/json",
                                                           "User account profile.")
        .addExample("Alice Profile", userExample);

    info->addResponse<oatpp::String>(Status::CODE_400, "text/plain", "Missing username path parameter.");
    info->addResponse<oatpp::String>(Status::CODE_401, "text/plain", "Missing or expired bearer token.");
    info->addResponse<oatpp::String>(Status::CODE_404, "text/plain", "User not found.");
}

void UserController::Z__ENDPOINT_ADD_INFO_createUser(
    const std::shared_ptr<oatpp::web::server::api::Endpoint::Info>& info)
{
    info->summary = "Add a single user or a batch of users";
    info->description =
        R"DOC(Protected endpoint — **requires `Authorization: Bearer <token>` header**. Polymorphic creation endpoint supporting both single-user creation and batch array creation.

## Payload Scenarios

1. **Single User:** Send a single JSON object:
   ```json
   { "username": "alice", "password": "AlicePassword1", "role": "operator" }
   ```
   Responds with `201 Created` returning `UserResponseDto`.

2. **Batch User List:** Send a JSON array of user objects:
   ```json
   [
     { "username": "alice", "password": "AlicePassword1", "role": "operator" },
     { "username": "bob", "password": "BobPassword1", "role": "viewer" }
   ]
   ```
   Responds with `201 Created` returning an array of `UserResponseDto`.

## Field Validation Rules

- `username` (required): **4-18** characters, charset `[a-zA-Z0-9@#$]`.
- `password` (required): **6-48** characters, must contain at least **one letter and one digit**.
- `role` (required): must be one of `admin`, `operator`, or `viewer`.

## Error responses

| HTTP | Domain exception | When it happens | Frontend hint |
|------|------------------|-----------------|---------------|
| 201 | — | Account(s) created successfully. | Notify success, refresh list. |
| 400 | `request::Missing` | Missing body or missing required field (`username`, `password`, `role`). | Highlight missing required inputs. |
| 401 | `authentication::InvalidToken` | Missing, invalid, or expired bearer token. | Redirect to login. |
| 409 | `database::Conflict` | Username is already registered. | Show "Username already taken". |
| 422 | `validation::InvalidValue` | Validation failed (length, charset, or invalid role). | Show field-level validation errors. |)DOC";

    info->addTag("users");
    info->authorization = "bearer_auth";
    info->addSecurityRequirement("bearer_auth");

    info->body.description = "Single user object or JSON array of user objects.";

    auto singleExample = dto::UserUpsertDto::createShared();
    singleExample->username = oatpp::String("alice01");
    singleExample->password = oatpp::String("AlicePassword1");
    singleExample->role = oatpp::String("operator");

    info->addConsumes<oatpp::Object<dto::UserUpsertDto>>("application/json", "Single user creation payload.")
        .addExample("Single User", singleExample);

    auto singleRespExample = dto::UserResponseDto::createShared();
    singleRespExample->username = oatpp::String("alice01");
    singleRespExample->role = oatpp::String("operator");

    info->addResponse<oatpp::Object<dto::UserResponseDto>>(Status::CODE_201, "application/json",
                                                           "Created user profile.")
        .addExample("Created User", singleRespExample);

    info->addResponse<oatpp::String>(Status::CODE_400, "text/plain", "Missing required field or malformed request.");
    info->addResponse<oatpp::String>(Status::CODE_401, "text/plain", "Missing or expired bearer token.");
    info->addResponse<oatpp::String>(Status::CODE_409, "text/plain", "Username already exists.");
    info->addResponse<oatpp::String>(Status::CODE_422, "text/plain", "Input validation failure.");
}

void UserController::Z__ENDPOINT_ADD_INFO_createUsersBatch(
    const std::shared_ptr<oatpp::web::server::api::Endpoint::Info>& info)
{
    info->summary = "Add a batch list of new users";
    info->description =
        R"DOC(Protected endpoint — **requires `Authorization: Bearer <token>` header**. Dedicated endpoint for bulk user provisioning from a JSON array.

## Request Rules

- Request body MUST be a JSON array of `UserUpsertDto` items.
- Every element in the array must supply all mandatory fields (`username`, `password`, `role`).
- If any item fails validation or if any username conflicts with an existing record, the entire batch is rejected.

```bash
# cURL example
curl -s http://localhost:8080/users/batch \
  -H "Authorization: Bearer ${ACCESS_TOKEN}" \
  -H "Content-Type: application/json" \
  -d '[
    {"username":"op1","password":"Password1","role":"operator"},
    {"username":"op2","password":"Password2","role":"viewer"}
  ]' | jq
```

## Error responses

| HTTP | Domain exception | When it happens | Frontend hint |
|------|------------------|-----------------|---------------|
| 201 | — | All accounts created. Returns array of `UserResponseDto`. | Show success summary. |
| 400 | `request::Missing` | Array is empty or an item is missing required fields. | Validate array elements before sending. |
| 401 | `authentication::InvalidToken` | Missing, invalid, or expired bearer token. | Redirect to login. |
| 409 | `database::Conflict` | One or more usernames already exist. | Check list for duplicate names. |
| 422 | `validation::InvalidValue` | One or more fields fail validation constraints. | Fix invalid rows. |)DOC";

    info->addTag("users");
    info->authorization = "bearer_auth";
    info->addSecurityRequirement("bearer_auth");

    info->body.description = "JSON array of new user objects to create.";

    info->addConsumes<oatpp::Vector<oatpp::Object<dto::UserUpsertDto>>>("application/json", "Batch array of users.");
    info->addResponse<oatpp::Vector<oatpp::Object<dto::UserResponseDto>>>(Status::CODE_201, "application/json",
                                                                          "Array of created user profiles.");
    info->addResponse<oatpp::String>(Status::CODE_400, "text/plain", "Missing or empty user batch.");
    info->addResponse<oatpp::String>(Status::CODE_401, "text/plain", "Missing or expired bearer token.");
    info->addResponse<oatpp::String>(Status::CODE_409, "text/plain", "Username conflict in batch.");
    info->addResponse<oatpp::String>(Status::CODE_422, "text/plain", "Field validation failure in batch item.");
}

void UserController::Z__ENDPOINT_ADD_INFO_updateUser(
    const std::shared_ptr<oatpp::web::server::api::Endpoint::Info>& info)
{
    info->summary = "Fully update an existing user's data set (PUT)";
    info->description =
        R"DOC(Protected endpoint — **requires `Authorization: Bearer <token>` header**. Performs a **full update** of an existing user account.

## Functional Distinction: PUT vs PATCH

- **`PUT` is for full updates:** All fields (`username`, `password`, `role`) MUST be provided in the request body.
- **Account Renaming:** The URL path parameter `{username}` specifies the user's **current username** in the database. The request body `username` specifies the **new username** (which may be the same or different). If the username is changed, the backend updates the primary key identity.
- **Admin Protection:** The default admin account (`admin`) cannot have its username changed. Attempting to rename `admin` returns `403 Forbidden`.

## Request Rules

- Path parameter `username`: current account name in database.
- Body `username`: new or retained account username (4-18 chars, charset `[a-zA-Z0-9@#$]`).
- Body `password`: new account password (6-48 chars, letter + digit).
- Body `role`: new account role (`admin`, `operator`, `viewer`).

```js
// Browser fetch example (renaming user and updating password)
const res = await fetch("http://localhost:8080/users/old_username", {
  method: "PUT",
  headers: {
    "Authorization": `Bearer ${accessToken}`,
    "Content-Type": "application/json"
  },
  body: JSON.stringify({
    username: "new_username",
    password: "NewPassword123",
    role: "admin"
  })
});
if (!res.ok) { throw new Error(await res.text()); }
const updated = await res.json();
```

## Error responses

| HTTP | Domain exception | When it happens | Frontend hint |
|------|------------------|-----------------|---------------|
| 200 | — | Success. Returns updated `UserResponseDto`. | Update UI state with new username/role. |
| 400 | `request::Missing` | Path or body fields (`username`, `password`, `role`) omitted. | Ensure all 3 fields are supplied. |
| 401 | `authentication::InvalidToken` | Missing, invalid, or expired bearer token. | Redirect to login. |
| 403 | `authorization::ResourceAccessDenied` | Attempted to rename the default admin user. | Admin username is immutable. |
| 404 | `database::RecordNotFound` | Current username does not exist in the database. | Verify target account. |
| 409 | `database::Conflict` | New username is already in use by another account. | Choose a different username. |
| 422 | `validation::InvalidValue` | Invalid format for username, password, or role. | Review validation constraints. |)DOC";

    info->addTag("users");
    info->authorization = "bearer_auth";
    info->addSecurityRequirement("bearer_auth");

    info->pathParams["username"].description = "Current username of the account to update.";
    info->pathParams["username"].required = true;

    auto putExample = dto::UserUpsertDto::createShared();
    putExample->username = oatpp::String("alice_new");
    putExample->password = oatpp::String("NewPassword1");
    putExample->role = oatpp::String("admin");

    info->addConsumes<oatpp::Object<dto::UserUpsertDto>>("application/json", "Full user data set.")
        .addExample("Full Update", putExample);

    auto putRespExample = dto::UserResponseDto::createShared();
    putRespExample->username = oatpp::String("alice_new");
    putRespExample->role = oatpp::String("admin");

    info->addResponse<oatpp::Object<dto::UserResponseDto>>(Status::CODE_200, "application/json",
                                                           "Updated user profile.")
        .addExample("Updated User", putRespExample);

    info->addResponse<oatpp::String>(Status::CODE_400, "text/plain", "Missing path or required body fields.");
    info->addResponse<oatpp::String>(Status::CODE_401, "text/plain", "Missing or expired bearer token.");
    info->addResponse<oatpp::String>(Status::CODE_403, "text/plain", "Default admin username cannot be changed.");
    info->addResponse<oatpp::String>(Status::CODE_404, "text/plain", "Current user not found.");
    info->addResponse<oatpp::String>(Status::CODE_409, "text/plain", "New username already taken.");
    info->addResponse<oatpp::String>(Status::CODE_422, "text/plain", "Field validation failure.");
}

void UserController::Z__ENDPOINT_ADD_INFO_patchUser(
    const std::shared_ptr<oatpp::web::server::api::Endpoint::Info>& info)
{
    info->summary = "Partially update an existing user's fields (PATCH)";
    info->description =
        R"DOC(Protected endpoint — **requires `Authorization: Bearer <token>` header**. Performs a **partial update** on an existing user account.

## Functional Distinction: PUT vs PATCH

- **`PATCH` is for partial updates:** None of the fields (`username`, `password`, `role`) are mandatory.
- Any field that is omitted or `null` remains **unchanged** in the database.
- Any field that is provided is validated and updated.
- At least one field must be provided in the request body.
- If `username` is provided and differs from `{username}` in the path, the account is renamed (default admin is protected from rename).

## Example Scenarios

1. **Update Password Only:**
   ```json
   { "password": "BrandNewPassword1" }
   ```
2. **Update Role Only:**
   ```json
   { "role": "operator" }
   ```
3. **Rename Account Only:**
   ```json
   { "username": "alice_renamed" }
   ```

```js
// Browser fetch example (updating role only)
const res = await fetch("http://localhost:8080/users/alice", {
  method: "PATCH",
  headers: {
    "Authorization": `Bearer ${accessToken}`,
    "Content-Type": "application/json"
  },
  body: JSON.stringify({ role: "admin" })
});
if (!res.ok) { throw new Error(await res.text()); }
const user = await res.json();
```

## Error responses

| HTTP | Domain exception | When it happens | Frontend hint |
|------|------------------|-----------------|---------------|
| 200 | — | Success. Returns updated `UserResponseDto`. | Update cached user state. |
| 400 | `request::Missing` | Path missing or empty PATCH body (no fields provided). | Provide at least one field to change. |
| 401 | `authentication::InvalidToken` | Missing, invalid, or expired bearer token. | Redirect to login. |
| 403 | `authorization::ResourceAccessDenied` | Attempted to rename the default admin user. | Admin username is immutable. |
| 404 | `database::RecordNotFound` | Current user does not exist. | Account not found. |
| 409 | `database::Conflict` | New username is already taken. | Choose another username. |
| 422 | `validation::InvalidValue` | Any provided field fails validation rules. | Fix field validation. |)DOC";

    info->addTag("users");
    info->authorization = "bearer_auth";
    info->addSecurityRequirement("bearer_auth");

    info->pathParams["username"].description = "Current username of the account to partially update.";
    info->pathParams["username"].required = true;

    auto patchExample = dto::PatchUserDto::createShared();
    patchExample->role = oatpp::String("admin");

    info->addConsumes<oatpp::Object<dto::PatchUserDto>>("application/json", "Partial update payload.")
        .addExample("Role Only Update", patchExample);

    auto patchRespExample = dto::UserResponseDto::createShared();
    patchRespExample->username = oatpp::String("alice");
    patchRespExample->role = oatpp::String("admin");

    info->addResponse<oatpp::Object<dto::UserResponseDto>>(Status::CODE_200, "application/json",
                                                           "Updated user profile.")
        .addExample("Updated User", patchRespExample);

    info->addResponse<oatpp::String>(Status::CODE_400, "text/plain", "Missing path or empty patch body.");
    info->addResponse<oatpp::String>(Status::CODE_401, "text/plain", "Missing or expired bearer token.");
    info->addResponse<oatpp::String>(Status::CODE_403, "text/plain", "Default admin username cannot be changed.");
    info->addResponse<oatpp::String>(Status::CODE_404, "text/plain", "Current user not found.");
    info->addResponse<oatpp::String>(Status::CODE_409, "text/plain", "New username already taken.");
    info->addResponse<oatpp::String>(Status::CODE_422, "text/plain", "Field validation failure.");
}

void UserController::Z__ENDPOINT_ADD_INFO_deleteUser(
    const std::shared_ptr<oatpp::web::server::api::Endpoint::Info>& info)
{
    info->summary = "Delete a specific user account";
    info->description =
        R"DOC(Protected endpoint — **requires `Authorization: Bearer <token>` header**. Permanently deletes an existing user account by username.

## Admin Protection Guardrail

- **The default admin user (`admin`) CANNOT be deleted.**
- If a client attempts to delete the default admin account, the request is blocked and responds with **`403 Forbidden`** (`ResourceAccessDenied`).

```js
// Browser fetch example
const res = await fetch(`http://localhost:8080/users/${encodeURIComponent(username)}`, {
  method: "DELETE",
  headers: { "Authorization": `Bearer ${accessToken}` }
});
if (res.status === 403) { alert("Default admin user cannot be deleted!"); return; }
if (res.status === 404) { alert("User does not exist"); return; }
if (!res.ok) { throw new Error(await res.text()); }
```

```bash
# cURL example
curl -s -X DELETE http://localhost:8080/users/bob \
  -H "Authorization: Bearer ${ACCESS_TOKEN}"
```

## Error responses

| HTTP | Domain exception | When it happens | Frontend hint |
|------|------------------|-----------------|---------------|
| 200 | — | Success. User permanently removed. | Remove user row from UI. |
| 400 | `request::Missing` | Username path parameter was omitted. | Provide valid username. |
| 401 | `authentication::InvalidToken` | Missing, invalid, or expired bearer token. | Redirect to login. |
| 403 | `authorization::ResourceAccessDenied` | Attempted to delete the default admin account. | Default admin is protected. |
| 404 | `database::RecordNotFound` | Target username does not exist. | User does not exist. |)DOC";

    info->addTag("users");
    info->authorization = "bearer_auth";
    info->addSecurityRequirement("bearer_auth");

    info->pathParams["username"].description = "Username of the account to permanently delete.";
    info->pathParams["username"].required = true;

    info->addResponse<oatpp::String>(Status::CODE_200, "text/plain", "User deleted successfully confirmation.");
    info->addResponse<oatpp::String>(Status::CODE_400, "text/plain", "Missing username path parameter.");
    info->addResponse<oatpp::String>(Status::CODE_401, "text/plain", "Missing or expired bearer token.");
    info->addResponse<oatpp::String>(Status::CODE_403, "text/plain", "Default admin account cannot be deleted.");
    info->addResponse<oatpp::String>(Status::CODE_404, "text/plain", "User not found.");
}

void UserController::Z__ENDPOINT_ADD_INFO_deleteAllUsers(
    const std::shared_ptr<oatpp::web::server::api::Endpoint::Info>& info)
{
    info->summary = "Delete all users while preserving the default admin";
    info->description =
        R"DOC(Protected endpoint — **requires `Authorization: Bearer <token>` header**. Deletes all registered user accounts while strictly **preserving the default admin user account**.

## Admin Protection Guardrail

- All standard user accounts are deleted in bulk.
- The default admin user (`admin`) is exempted and remains intact.
- The response body indicates the number of deleted records.

```js
// Browser fetch example
const res = await fetch("http://localhost:8080/users", {
  method: "DELETE",
  headers: { "Authorization": `Bearer ${accessToken}` }
});
if (!res.ok) { throw new Error(await res.text()); }
const result = await res.json();
// result -> { deleted_count: 5, message: "All users deleted except default admin user ('admin')" }
```

```bash
# cURL example
curl -s -X DELETE http://localhost:8080/users \
  -H "Authorization: Bearer ${ACCESS_TOKEN}" | jq
```

## Error responses

| HTTP | Domain exception | When it happens | Frontend hint |
|------|------------------|-----------------|---------------|
| 200 | — | Success. Returns `DeleteUsersResponseDto`. | Show count of deleted accounts. |
| 401 | `authentication::InvalidToken` | Missing, invalid, or expired bearer token. | Redirect to login. |
| 500 | `internal::OperationFailed` | Database or unexpected server error. | Show server error alert. |)DOC";

    info->addTag("users");
    info->authorization = "bearer_auth";
    info->addSecurityRequirement("bearer_auth");

    auto delExample = dto::DeleteUsersResponseDto::createShared();
    delExample->deletedCount = 4;
    delExample->message = oatpp::String("All users deleted except default admin user ('admin')");

    info->addResponse<oatpp::Object<dto::DeleteUsersResponseDto>>(Status::CODE_200, "application/json",
                                                                  "Deletion summary.")
        .addExample("Batch deletion outcome", delExample);

    info->addResponse<oatpp::String>(Status::CODE_401, "text/plain", "Missing or expired bearer token.");
}

#endif  // APP_WITH_SWAGGER

std::shared_ptr<UserController::OutgoingResponse> UserController::listUsers(
    const std::shared_ptr<IncomingRequest>& request)
{
    ensureAuthenticated(request);

    const auto users = userService_->listUsers();

    auto outList = oatpp::Vector<oatpp::Object<dto::UserResponseDto>>::createShared();
    for (const auto& user : users)
    {
        auto item = dto::UserResponseDto::createShared();
        item->username = oatpp::String(user.username);
        item->role = oatpp::String(user.role);
        outList->push_back(std::move(item));
    }

    return createDtoResponse(Status::CODE_200, outList);
}

std::shared_ptr<UserController::OutgoingResponse> UserController::getUser(
    const oatpp::String& username, const std::shared_ptr<IncomingRequest>& request)
{
    ensureAuthenticated(request);

    if (!username || username->empty())
    {
        throw errors::request::Missing("username path parameter is required");
    }

    const auto user = userService_->getUser(textOf(username));

    auto out = dto::UserResponseDto::createShared();
    out->username = oatpp::String(user.username);
    out->role = oatpp::String(user.role);

    return createDtoResponse(Status::CODE_200, out);
}

std::shared_ptr<UserController::OutgoingResponse> UserController::createUser(
    const std::shared_ptr<IncomingRequest>& request)
{
    ensureAuthenticated(request);

    const auto bodyStr = request->readBodyToString();
    if (!bodyStr || bodyStr->empty())
    {
        throw errors::request::Missing("Request body is required");
    }

    std::size_t idx = 0;
    while (idx < bodyStr->size() && std::isspace(static_cast<unsigned char>((*bodyStr)[idx])))
    {
        ++idx;
    }

    const auto mapper = getDefaultObjectMapper();

    if (idx < bodyStr->size() && (*bodyStr)[idx] == '[')
    {
        const auto dtoList = mapper->readFromString<oatpp::Vector<oatpp::Object<dto::UserUpsertDto>>>(bodyStr);
        if (!dtoList || dtoList->empty())
        {
            throw errors::request::Missing("User list is required and must not be empty");
        }

        std::vector<services::CreateUserInput> inputs;
        inputs.reserve(dtoList->size());

        for (const auto& item : *dtoList)
        {
            if (!item)
            {
                throw errors::request::Missing("User item in batch list cannot be null");
            }

            item->validate();

            inputs.emplace_back(textOf(item->username), textOf(item->password), textOf(item->role));
        }

        const auto created = userService_->createUsers(inputs);

        auto outList = oatpp::Vector<oatpp::Object<dto::UserResponseDto>>::createShared();
        for (const auto& user : created)
        {
            auto item = dto::UserResponseDto::createShared();
            item->username = oatpp::String(user.username);
            item->role = oatpp::String(user.role);
            outList->push_back(std::move(item));
        }

        return createDtoResponse(Status::CODE_201, outList);
    }

    const auto dto = mapper->readFromString<oatpp::Object<dto::UserUpsertDto>>(bodyStr);
    if (!dto)
    {
        throw errors::request::Missing("User body cannot be null");
    }

    dto->validate();

    const auto created = userService_->createUser(
        services::CreateUserInput(textOf(dto->username), textOf(dto->password), textOf(dto->role)));

    auto out = dto::UserResponseDto::createShared();
    out->username = oatpp::String(created.username);
    out->role = oatpp::String(created.role);

    return createDtoResponse(Status::CODE_201, out);
}

std::shared_ptr<UserController::OutgoingResponse> UserController::createUsersBatch(
    const oatpp::Vector<oatpp::Object<dto::UserUpsertDto>>& body, const std::shared_ptr<IncomingRequest>& request)
{
    ensureAuthenticated(request);

    if (!body || body->empty())
    {
        throw errors::request::Missing("User list is required and must not be empty");
    }

    std::vector<services::CreateUserInput> inputs;
    inputs.reserve(body->size());

    for (const auto& dtoItem : *body)
    {
        if (!dtoItem)
        {
            throw errors::request::Missing("User item in batch list cannot be null");
        }

        dtoItem->validate();

        inputs.emplace_back(textOf(dtoItem->username), textOf(dtoItem->password), textOf(dtoItem->role));
    }

    const auto created = userService_->createUsers(inputs);

    auto outList = oatpp::Vector<oatpp::Object<dto::UserResponseDto>>::createShared();
    for (const auto& user : created)
    {
        auto item = dto::UserResponseDto::createShared();
        item->username = oatpp::String(user.username);
        item->role = oatpp::String(user.role);
        outList->push_back(std::move(item));
    }

    return createDtoResponse(Status::CODE_201, outList);
}

std::shared_ptr<UserController::OutgoingResponse> UserController::updateUser(
    const oatpp::String& username, const oatpp::Object<dto::UserUpsertDto>& body,
    const std::shared_ptr<IncomingRequest>& request)
{
    ensureAuthenticated(request);

    if (!username || username->empty())
    {
        throw errors::request::Missing("username path parameter is required");
    }

    if (!body)
    {
        throw errors::request::Missing("Request body is required");
    }

    body->validate();

    const auto updated = userService_->updateUser(
        textOf(username),
        services::UpdateUserInput(textOf(body->username), textOf(body->password), textOf(body->role)));

    auto out = dto::UserResponseDto::createShared();
    out->username = oatpp::String(updated.username);
    out->role = oatpp::String(updated.role);

    return createDtoResponse(Status::CODE_200, out);
}

std::shared_ptr<UserController::OutgoingResponse> UserController::patchUser(
    const oatpp::String& username, const oatpp::Object<dto::PatchUserDto>& body,
    const std::shared_ptr<IncomingRequest>& request)
{
    ensureAuthenticated(request);

    if (!username || username->empty())
    {
        throw errors::request::Missing("username path parameter is required");
    }

    if (!body)
    {
        throw errors::request::Missing("Request body is required");
    }

    body->validate();

    services::PatchUserInput input;
    if (body->username && !body->username->empty())
    {
        input.username = textOf(body->username);
    }

    if (body->password && !body->password->empty())
    {
        input.password = textOf(body->password);
    }

    if (body->role && !body->role->empty())
    {
        input.role = textOf(body->role);
    }

    const auto updated = userService_->patchUser(textOf(username), input);

    auto out = dto::UserResponseDto::createShared();
    out->username = oatpp::String(updated.username);
    out->role = oatpp::String(updated.role);

    return createDtoResponse(Status::CODE_200, out);
}

std::shared_ptr<UserController::OutgoingResponse> UserController::deleteUser(
    const oatpp::String& username, const std::shared_ptr<IncomingRequest>& request)
{
    ensureAuthenticated(request);

    if (!username || username->empty())
    {
        throw errors::request::Missing("username path parameter is required");
    }

    userService_->deleteUser(textOf(username));

    return createResponse(Status::CODE_200, "User '" + textOf(username) + "' deleted successfully");
}

std::shared_ptr<UserController::OutgoingResponse> UserController::deleteAllUsers(
    const std::shared_ptr<IncomingRequest>& request)
{
    ensureAuthenticated(request);

    const auto result = userService_->deleteAllUsers();

    auto out = dto::DeleteUsersResponseDto::createShared();
    out->deletedCount = result.deletedCount;
    out->message = oatpp::String(result.message);

    return createDtoResponse(Status::CODE_200, out);
}

}  // namespace app::controllers
