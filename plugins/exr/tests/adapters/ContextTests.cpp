// The OpenEXRCore boundary on its own: the result mapping, the error handler the Core calls
// (with a null context for allocation failures and bad arguments, per context.c), and the part
// reader on an index the Core refuses.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>

#include "adapters/exr/Context.hpp"
#include "core/Error.hpp"
#include "core/IDecoder.hpp"

namespace
{

    using pvdkit::core::DecoderOptions;
    using pvdkit::core::ErrorCode;
    using pvdkit::exr::errorCodeForResult;
    using pvdkit::exr::openContext;
    using pvdkit::exr::readParts;
    using pvdkit::exr::Stream;

    constexpr DecoderOptions kOptions{4, false, std::uint64_t{16'384} * 16'384, 32'768, true};

    std::vector<std::byte> readFixture(const std::string_view name)
    {
        const auto path = std::filesystem::path{PVDKIT_FIXTURE_DIR} / name;
        std::vector<std::byte> bytes(static_cast<std::size_t>(std::filesystem::file_size(path)));
        std::ifstream stream{path, std::ios::binary};
        REQUIRE(stream.good());
        stream.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        REQUIRE(stream.good());
        return bytes;
    }

} // namespace

TEST_CASE("Core results map to the plugin's error categories")
{
    CHECK(errorCodeForResult(EXR_ERR_FEATURE_NOT_IMPLEMENTED, "", ErrorCode::ParseFailed) ==
          ErrorCode::UnsupportedFeature);
    // The Core's own wording for the per-context limits (validation.c).
    CHECK(errorCodeForResult(EXR_ERR_INVALID_ATTR, "Invalid width (40000) too large (max 32768)",
                             ErrorCode::ParseFailed) == ErrorCode::TooLarge);
    CHECK(errorCodeForResult(EXR_ERR_INVALID_ATTR, "Width of tile exceeds max size (4097 vs max 4096)",
                             ErrorCode::ParseFailed) == ErrorCode::TooLarge);
    CHECK(errorCodeForResult(EXR_ERR_CORRUPT_CHUNK, "Unable to decompress", ErrorCode::DecodeFailed) ==
          ErrorCode::DecodeFailed);
    CHECK(errorCodeForResult(EXR_ERR_OUT_OF_MEMORY, "", ErrorCode::DecodeFailed) == ErrorCode::DecodeFailed);
}

TEST_CASE("the error handler keeps the Core's message next to the bytes and tolerates a null context or message")
{
    const auto bytes = readFixture("rgb_half_zip.exr");
    Stream stream{bytes};
    CHECK(stream.message().empty());
    const auto context = openContext(stream, kOptions);
    REQUIRE(context.has_value());
    pvdkit::exr::detail::recordError(context->get(), EXR_ERR_CORRUPT_CHUNK, "chunk went wrong");
    CHECK(stream.message() == "chunk went wrong");
    pvdkit::exr::detail::recordError(context->get(), EXR_ERR_CORRUPT_CHUNK, nullptr);
    CHECK(stream.message() == "chunk went wrong");
    pvdkit::exr::detail::recordError(nullptr, EXR_ERR_OUT_OF_MEMORY, "no memory for a context");
    CHECK(stream.message() == "chunk went wrong");
    // The handler may not allocate (it is noexcept, a C callback): the message lives in a fixed
    // buffer and a longer one is cut at its capacity.
    const std::string longMessage(2'000, 'x');
    pvdkit::exr::detail::recordError(context->get(), EXR_ERR_CORRUPT_CHUNK, longMessage.c_str());
    CHECK(stream.message().size() == Stream::kMessageCapacity - 1);
    CHECK(stream.message() == std::string_view{longMessage}.substr(0, Stream::kMessageCapacity - 1));
    pvdkit::exr::detail::recordError(context->get(), EXR_ERR_CORRUPT_CHUNK, "");
    CHECK(stream.message().empty());
}

TEST_CASE("a part index beyond the file's count is the Core's out-of-range error")
{
    const auto bytes = readFixture("multipart_views.exr");
    Stream stream{bytes};
    const auto context = openContext(stream, kOptions);
    REQUIRE(context.has_value());
    const auto parts = readParts(*context, stream);
    REQUIRE(parts.has_value());
    CHECK(parts->size() == 2);
    const auto beyond = pvdkit::exr::detail::readParts(*context, 3, stream);
    REQUIRE_FALSE(beyond.has_value());
    CHECK(beyond.error().code == ErrorCode::ParseFailed);
    CHECK_FALSE(beyond.error().detail.empty());
}
