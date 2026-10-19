#pragma once

#include <memory>

#include "oatpp/core/macro/codegen.hpp"
#include "oatpp/core/macro/component.hpp"
#include "oatpp/web/server/api/ApiController.hpp"

#include "app/auth/JwtService.hpp"
#include "app/dto/UserDtos.hpp"
#include "app/services/UserService.hpp"

#include OATPP_CODEGEN_BEGIN(ApiController)

namespace app::controllers
{

/**
 * User management endpoints:
 *
 * - GET    /users              List all users
 * - GET    /users/{username}   Get user by username
 * - POST   /users              Create a single user or list of users
 * - POST   /users/batch        Create multiple users (batch)
 * - PUT    /users/{username}   Full update of user (password and role)
 * - PATCH  /users/{username}   Partial update of user (password, role, or both)
 * - DELETE /users/{username}   Delete a specific user (default admin protected)
 * - DELETE /users              Delete all users (default admin protected)
 */
class UserController : public oatpp::web::server::api::ApiController
{
public:
    UserController(const std::shared_ptr<ObjectMapper>& objectMapper,
                   std::shared_ptr<services::UserService> userService, std::shared_ptr<auth::JwtService> jwtService);

#if defined(APP_WITH_SWAGGER)
    ENDPOINT_INFO(listUsers);
    ENDPOINT_INFO(getUser);
    ENDPOINT_INFO(createUser);
    ENDPOINT_INFO(createUsersBatch);
    ENDPOINT_INFO(updateUser);
    ENDPOINT_INFO(patchUser);
    ENDPOINT_INFO(deleteUser);
    ENDPOINT_INFO(deleteAllUsers);
#endif  // APP_WITH_SWAGGER

    ENDPOINT("GET", "/users", listUsers, REQUEST(std::shared_ptr<IncomingRequest>, request));

    ENDPOINT("GET", "/users/{username}", getUser, PATH(oatpp::String, username),
             REQUEST(std::shared_ptr<IncomingRequest>, request));

    ENDPOINT("POST", "/users", createUser, REQUEST(std::shared_ptr<IncomingRequest>, request));

    ENDPOINT("POST", "/users/batch", createUsersBatch, BODY_DTO(oatpp::Vector<oatpp::Object<dto::UserUpsertDto>>, body),
             REQUEST(std::shared_ptr<IncomingRequest>, request));

    ENDPOINT("PUT", "/users/{username}", updateUser, PATH(oatpp::String, username),
             BODY_DTO(oatpp::Object<dto::UserUpsertDto>, body), REQUEST(std::shared_ptr<IncomingRequest>, request));

    ENDPOINT("PATCH", "/users/{username}", patchUser, PATH(oatpp::String, username),
             BODY_DTO(oatpp::Object<dto::PatchUserDto>, body), REQUEST(std::shared_ptr<IncomingRequest>, request));

    ENDPOINT("DELETE", "/users/{username}", deleteUser, PATH(oatpp::String, username),
             REQUEST(std::shared_ptr<IncomingRequest>, request));

    ENDPOINT("DELETE", "/users", deleteAllUsers, REQUEST(std::shared_ptr<IncomingRequest>, request));

private:
    void ensureAuthenticated(const std::shared_ptr<IncomingRequest>& request);

    std::shared_ptr<services::UserService> userService_;
    std::shared_ptr<auth::JwtService> jwtService_;
};

}  // namespace app::controllers

#include OATPP_CODEGEN_END(ApiController)
