#include "app/infrastructure/Gzip.hpp"

#ifndef APP_WITH_ZLIB
#error "Gzip.cpp requires APP_WITH_ZLIB (configure with -DAPP_ENABLE_ZLIB=ON)"
#endif  // !APP_WITH_ZLIB

#include <stdexcept>

#include "oatpp-zlib/Processor.hpp"
#include "oatpp/core/data/stream/BufferStream.hpp"

namespace app::infrastructure
{

std::string gzipCompress(const std::string& plain)
{
    oatpp::String input(plain.c_str(), static_cast<v_buff_size>(plain.size()));
    oatpp::data::stream::BufferInputStream inStream(input);
    oatpp::data::stream::BufferOutputStream outStream;
    oatpp::zlib::DeflateEncoder encoder(1024, true /* gzip */);
    oatpp::data::buffer::IOBuffer buffer;
    oatpp::data::stream::transfer(&inStream, &outStream, 0, buffer.getData(), buffer.getSize(), &encoder);

    const auto out = outStream.toString();
    return std::string(out->c_str(), static_cast<std::size_t>(out->size()));
}

std::string gzipDecompress(const std::string& compressed)
{
    if (compressed.empty())
    {
        throw std::runtime_error("gzip decompress failed: empty input");
    }

    oatpp::String input(compressed.c_str(), static_cast<v_buff_size>(compressed.size()));
    oatpp::data::stream::BufferInputStream inStream(input);
    oatpp::data::stream::BufferOutputStream outStream;
    oatpp::zlib::DeflateDecoder decoder(1024, true /* gzip */);
    oatpp::data::buffer::IOBuffer buffer;
    oatpp::data::stream::transfer(&inStream, &outStream, 0, buffer.getData(), buffer.getSize(), &decoder);

    const auto out = outStream.toString();
    if (!out || out->size() == 0)
    {
        throw std::runtime_error("gzip decompress failed: corrupt input");
    }

    return std::string(out->c_str(), static_cast<std::size_t>(out->size()));
}

}  // namespace app::infrastructure
