#pragma once

#include <stdexcept>
#include <string>
#include <utility>

namespace app::errors
{

/// Machine-readable error categories for the whole backend.
///
/// Unlike HTTP status codes (transport-level), these name the *domain* failure:
/// request shape, auth/session, resources, validation, filesystem, database,
/// serialization, network, configuration, crypto, websocket, external services,
/// rate limiting, runtime and internal operations. Each maps to exactly one HTTP
/// status via `toHttpStatus()`; the HTTP status goes on the status line and an
/// approved public message goes in a `text/plain` body. Diagnostic `what()` text
/// is never forwarded by AppErrorHandler — no numeric `4000`-style envelope.
enum class ErrorCode
{
    RequestInvalid,
    RequestMissing,
    MethodNotAllowed,
    PayloadTooLarge,
    UnsupportedMediaType,
    InvalidCredentials,
    InvalidToken,
    TokenExpired,
    MissingCredentials,
    InvalidSession,
    ExpiredSession,
    ResourceAccessDenied,
    ResourceNotFound,
    ResourceConflict,
    ResourceGone,
    ResourceUnavailable,
    ValidationInvalidValue,
    FileNotFound,
    FileReadFailed,
    FileWriteFailed,
    FileCorrupted,
    DatabaseQueryFailed,
    DatabaseTransactionFailed,
    DatabaseRecordNotFound,
    DatabaseConflict,
    DatabaseConnectionFailed,
    SerializationParsingFailed,
    SerializationDeserializationFailed,
    SerializationFailed,
    NetworkConnectionFailed,
    NetworkTimeout,
    ConfigurationMissing,
    ConfigurationInvalid,
    CryptoKeyInvalid,
    TokenGenerationFailed,
    WebsocketHandshakeFailed,
    WebsocketSendFailed,
    ExternalServiceInvalidResponse,
    ExternalServiceTimeout,
    RateLimited,
    RuntimeExecutionFailed,
    InternalOperationFailed
};

const char* toString(ErrorCode code) noexcept;

/// HTTP status that corresponds to an ErrorCode.
int toHttpStatus(ErrorCode code) noexcept;

/**
 * Application exception carrying an ErrorCode.
 *
 * Services, repositories and infrastructure throw the fine-grained subclasses
 * below; `AppErrorHandler` catches them (including nested) and converts each
 * to its HTTP Status + `text/plain` message via oatpp's
 * `ResponseFactory::createResponse`. Controllers use oatpp's
 * `createResponse` / `createDtoResponse` helpers directly for expected cases
 * and let domain exceptions bubble to the handler otherwise.
 *
 * `what()` is diagnostic text, separate from the handler's public messages.
 * Never include secrets: other internal exception consumers may log it.
 */
class AppException : public std::runtime_error
{
public:
    AppException(ErrorCode code, const std::string& message);
    ErrorCode code() const noexcept;
    int httpStatus() const noexcept;

private:
    ErrorCode code_;
};

}  // namespace app::errors

#define DEFINE_APP_EXCEPTION(NAME, CODE, DEFAULT_MESSAGE)           \
    class NAME final : public AppException                          \
    {                                                               \
    public:                                                         \
        explicit NAME(const std::string& message = DEFAULT_MESSAGE) \
            : AppException(app::errors::ErrorCode::CODE, message)   \
        {                                                           \
        }                                                           \
    }

namespace app::errors
{

namespace request
{

DEFINE_APP_EXCEPTION(Invalid, RequestInvalid, "invalid request");
DEFINE_APP_EXCEPTION(Missing, RequestMissing, "required request data is missing");
DEFINE_APP_EXCEPTION(MethodNotAllowed, MethodNotAllowed, "method not allowed");
DEFINE_APP_EXCEPTION(PayloadTooLarge, PayloadTooLarge, "payload too large");
DEFINE_APP_EXCEPTION(UnsupportedMediaType, UnsupportedMediaType, "unsupported media type");

}  // namespace request

namespace authentication
{

DEFINE_APP_EXCEPTION(InvalidCredentials, InvalidCredentials, "authentication credentials are invalid");
DEFINE_APP_EXCEPTION(InvalidToken, InvalidToken, "authentication token is invalid");
DEFINE_APP_EXCEPTION(TokenExpired, TokenExpired, "authentication token has expired");
DEFINE_APP_EXCEPTION(MissingCredentials, MissingCredentials, "authentication credentials are missing");
DEFINE_APP_EXCEPTION(InvalidSession, InvalidSession, "session is invalid");
DEFINE_APP_EXCEPTION(ExpiredSession, ExpiredSession, "session has expired");

}  // namespace authentication

namespace authorization
{

DEFINE_APP_EXCEPTION(ResourceAccessDenied, ResourceAccessDenied, "resource access denied");

}  // namespace authorization

namespace resource
{

DEFINE_APP_EXCEPTION(NotFound, ResourceNotFound, "resource not found");
DEFINE_APP_EXCEPTION(Conflict, ResourceConflict, "resource conflict");
DEFINE_APP_EXCEPTION(Gone, ResourceGone, "resource is gone");
DEFINE_APP_EXCEPTION(Unavailable, ResourceUnavailable, "resource is unavailable");

}  // namespace resource

namespace validation
{

DEFINE_APP_EXCEPTION(InvalidValue, ValidationInvalidValue, "invalid value");

}  // namespace validation

namespace filesystem
{

DEFINE_APP_EXCEPTION(NotFound, FileNotFound, "file not found");
DEFINE_APP_EXCEPTION(ReadFailed, FileReadFailed, "failed to read file");
DEFINE_APP_EXCEPTION(WriteFailed, FileWriteFailed, "failed to write file");
DEFINE_APP_EXCEPTION(Corrupted, FileCorrupted, "file is corrupted");

}  // namespace filesystem

namespace database
{

DEFINE_APP_EXCEPTION(QueryFailed, DatabaseQueryFailed, "database query failed");
DEFINE_APP_EXCEPTION(TransactionFailed, DatabaseTransactionFailed, "database transaction failed");
DEFINE_APP_EXCEPTION(RecordNotFound, DatabaseRecordNotFound, "database record not found");
DEFINE_APP_EXCEPTION(Conflict, DatabaseConflict, "database conflict");
DEFINE_APP_EXCEPTION(ConnectionFailed, DatabaseConnectionFailed, "database connection failed");

}  // namespace database

namespace serialization
{

DEFINE_APP_EXCEPTION(ParsingFailed, SerializationParsingFailed, "parsing failed");
DEFINE_APP_EXCEPTION(DeserializationFailed, SerializationDeserializationFailed, "deserialization failed");
DEFINE_APP_EXCEPTION(SerializationFailed, SerializationFailed, "serialization failed");

}  // namespace serialization

namespace network
{

DEFINE_APP_EXCEPTION(ConnectionFailed, NetworkConnectionFailed, "network connection failed");
DEFINE_APP_EXCEPTION(Timeout, NetworkTimeout, "network timeout");

}  // namespace network

namespace config
{

DEFINE_APP_EXCEPTION(Missing, ConfigurationMissing, "configuration is missing");
DEFINE_APP_EXCEPTION(Invalid, ConfigurationInvalid, "configuration is invalid");

}  // namespace config

namespace crypto
{

DEFINE_APP_EXCEPTION(KeyInvalid, CryptoKeyInvalid, "crypto key is invalid");
DEFINE_APP_EXCEPTION(TokenGenerationFailed, TokenGenerationFailed, "token generation failed");

}  // namespace crypto

namespace websocket
{

DEFINE_APP_EXCEPTION(HandshakeFailed, WebsocketHandshakeFailed, "websocket handshake failed");
DEFINE_APP_EXCEPTION(SendFailed, WebsocketSendFailed, "websocket send failed");

}  // namespace websocket

namespace external_service
{

DEFINE_APP_EXCEPTION(InvalidResponse, ExternalServiceInvalidResponse, "external service returned an invalid response");
DEFINE_APP_EXCEPTION(Timeout, ExternalServiceTimeout, "external service timeout");

}  // namespace external_service

namespace ratelimit
{

DEFINE_APP_EXCEPTION(Exceeded, RateLimited, "rate limit exceeded");

}  // namespace ratelimit

namespace runtime
{

DEFINE_APP_EXCEPTION(ExecutionFailed, RuntimeExecutionFailed, "execution failed");

}  // namespace runtime

namespace internal
{

DEFINE_APP_EXCEPTION(OperationFailed, InternalOperationFailed, "internal operation failed");

}  // namespace internal

}  // namespace app::errors

#undef DEFINE_APP_EXCEPTION
