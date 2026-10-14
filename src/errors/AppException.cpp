#include "app/errors/AppException.hpp"

namespace app::errors
{

const char* toString(ErrorCode code) noexcept
{
    switch (code)
    {
        case ErrorCode::RequestInvalid:
            return "RequestInvalid";
        case ErrorCode::RequestMissing:
            return "RequestMissing";
        case ErrorCode::MethodNotAllowed:
            return "MethodNotAllowed";
        case ErrorCode::PayloadTooLarge:
            return "PayloadTooLarge";
        case ErrorCode::UnsupportedMediaType:
            return "UnsupportedMediaType";
        case ErrorCode::InvalidCredentials:
            return "InvalidCredentials";
        case ErrorCode::InvalidToken:
            return "InvalidToken";
        case ErrorCode::TokenExpired:
            return "TokenExpired";
        case ErrorCode::MissingCredentials:
            return "MissingCredentials";
        case ErrorCode::InvalidSession:
            return "InvalidSession";
        case ErrorCode::ExpiredSession:
            return "ExpiredSession";
        case ErrorCode::ResourceAccessDenied:
            return "ResourceAccessDenied";
        case ErrorCode::ResourceNotFound:
            return "ResourceNotFound";
        case ErrorCode::ResourceConflict:
            return "ResourceConflict";
        case ErrorCode::ResourceGone:
            return "ResourceGone";
        case ErrorCode::ResourceUnavailable:
            return "ResourceUnavailable";
        case ErrorCode::ValidationInvalidValue:
            return "ValidationInvalidValue";
        case ErrorCode::FileNotFound:
            return "FileNotFound";
        case ErrorCode::FileReadFailed:
            return "FileReadFailed";
        case ErrorCode::FileWriteFailed:
            return "FileWriteFailed";
        case ErrorCode::FileCorrupted:
            return "FileCorrupted";
        case ErrorCode::DatabaseQueryFailed:
            return "DatabaseQueryFailed";
        case ErrorCode::DatabaseTransactionFailed:
            return "DatabaseTransactionFailed";
        case ErrorCode::DatabaseRecordNotFound:
            return "DatabaseRecordNotFound";
        case ErrorCode::DatabaseConflict:
            return "DatabaseConflict";
        case ErrorCode::DatabaseConnectionFailed:
            return "DatabaseConnectionFailed";
        case ErrorCode::SerializationParsingFailed:
            return "SerializationParsingFailed";
        case ErrorCode::SerializationDeserializationFailed:
            return "SerializationDeserializationFailed";
        case ErrorCode::SerializationFailed:
            return "SerializationFailed";
        case ErrorCode::NetworkConnectionFailed:
            return "NetworkConnectionFailed";
        case ErrorCode::NetworkTimeout:
            return "NetworkTimeout";
        case ErrorCode::ConfigurationMissing:
            return "ConfigurationMissing";
        case ErrorCode::ConfigurationInvalid:
            return "ConfigurationInvalid";
        case ErrorCode::CryptoKeyInvalid:
            return "CryptoKeyInvalid";
        case ErrorCode::TokenGenerationFailed:
            return "TokenGenerationFailed";
        case ErrorCode::WebsocketHandshakeFailed:
            return "WebsocketHandshakeFailed";
        case ErrorCode::WebsocketSendFailed:
            return "WebsocketSendFailed";
        case ErrorCode::ExternalServiceInvalidResponse:
            return "ExternalServiceInvalidResponse";
        case ErrorCode::ExternalServiceTimeout:
            return "ExternalServiceTimeout";
        case ErrorCode::RateLimited:
            return "RateLimited";
        case ErrorCode::RuntimeExecutionFailed:
            return "RuntimeExecutionFailed";
        case ErrorCode::InternalOperationFailed:
            return "InternalOperationFailed";
    }

    return "Unknown";
}

int toHttpStatus(ErrorCode code) noexcept
{
    switch (code)
    {
        case ErrorCode::RequestInvalid:
        case ErrorCode::RequestMissing:
        case ErrorCode::SerializationParsingFailed:
        case ErrorCode::SerializationDeserializationFailed:
        case ErrorCode::WebsocketHandshakeFailed:
            return 400;
        case ErrorCode::InvalidCredentials:
        case ErrorCode::InvalidToken:
        case ErrorCode::TokenExpired:
        case ErrorCode::MissingCredentials:
        case ErrorCode::InvalidSession:
        case ErrorCode::ExpiredSession:
            return 401;
        case ErrorCode::ResourceAccessDenied:
            return 403;
        case ErrorCode::ResourceNotFound:
        case ErrorCode::FileNotFound:
        case ErrorCode::DatabaseRecordNotFound:
            return 404;
        case ErrorCode::MethodNotAllowed:
            return 405;
        case ErrorCode::ResourceConflict:
        case ErrorCode::DatabaseConflict:
            return 409;
        case ErrorCode::ResourceGone:
            return 410;
        case ErrorCode::PayloadTooLarge:
            return 413;
        case ErrorCode::UnsupportedMediaType:
            return 415;
        case ErrorCode::ValidationInvalidValue:
            return 422;
        case ErrorCode::RateLimited:
            return 429;
        case ErrorCode::FileReadFailed:
        case ErrorCode::FileWriteFailed:
        case ErrorCode::FileCorrupted:
        case ErrorCode::DatabaseQueryFailed:
        case ErrorCode::DatabaseTransactionFailed:
        case ErrorCode::DatabaseConnectionFailed:
        case ErrorCode::SerializationFailed:
        case ErrorCode::ConfigurationMissing:
        case ErrorCode::ConfigurationInvalid:
        case ErrorCode::CryptoKeyInvalid:
        case ErrorCode::TokenGenerationFailed:
        case ErrorCode::WebsocketSendFailed:
        case ErrorCode::RuntimeExecutionFailed:
        case ErrorCode::InternalOperationFailed:
            return 500;
        case ErrorCode::ExternalServiceInvalidResponse:
            return 502;
        case ErrorCode::ResourceUnavailable:
        case ErrorCode::NetworkConnectionFailed:
            return 503;
        case ErrorCode::NetworkTimeout:
        case ErrorCode::ExternalServiceTimeout:
            return 504;
    }

    return 500;
}

AppException::AppException(ErrorCode code, const std::string& message) : std::runtime_error(message), code_(code)
{
}

ErrorCode AppException::code() const noexcept
{
    return code_;
}

int AppException::httpStatus() const noexcept
{
    return toHttpStatus(code_);
}

}  // namespace app::errors
