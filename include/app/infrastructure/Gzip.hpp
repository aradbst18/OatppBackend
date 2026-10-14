#pragma once

#if !defined(APP_WITH_ZLIB)
#error "Gzip.hpp requires APP_WITH_ZLIB (configure with -DAPP_ENABLE_ZLIB=ON)"
#endif  // !APP_WITH_ZLIB

#include <memory>
#include <string>

namespace app::infrastructure
{

/// Compress a payload with gzip (oatpp-zlib). Used by the gzip demo path and
/// covered by tests. Returns the compressed bytes.
std::string gzipCompress(const std::string& plain);

/// Decompress gzip bytes produced by gzipCompress. Throws on corrupt input.
std::string gzipDecompress(const std::string& compressed);

}  // namespace app::infrastructure
