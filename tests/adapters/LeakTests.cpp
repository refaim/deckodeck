// Unit-level leak gate of the Win32 adapter: opening and closing a FileMapping / FileSource N
// times, on the happy path and on every failure path, returns every handle and every heap block
// (tests/support/LeakCheck.hpp).

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <doctest/doctest.h>

#include "LeakCheck.hpp"
#include "adapters/win/FileMapping.hpp"
#include "adapters/win/FileSource.hpp"
#include "core/Error.hpp"

namespace
{

    namespace fs = std::filesystem;
    using pvdkit::core::ErrorCode;
    using pvdkit::test::measureLeaks;
    using pvdkit::test::requireNoLeak;

    constexpr std::size_t kIterations = 200;

    class TempFile
    {
      public:
        explicit TempFile(const std::size_t size)
            : path_(fs::temp_directory_path() / ("pvdkit-leak-" + std::to_string(GetCurrentProcessId()) + ".bin"))
        {
            std::ofstream stream{path_, std::ios::binary};
            REQUIRE(stream.good());
            const std::vector<char> payload(size, 'x');
            stream.write(payload.data(), static_cast<std::streamsize>(payload.size()));
            REQUIRE(stream.good());
        }

        ~TempFile()
        {
            std::error_code ignored;
            fs::remove(path_, ignored);
        }

        TempFile(const TempFile &) = delete;
        TempFile &operator=(const TempFile &) = delete;

        [[nodiscard]] const fs::path &path() const noexcept
        {
            return path_;
        }

        [[nodiscard]] std::string utf8() const
        {
            const auto text = path_.u8string();
            return {text.begin(), text.end()};
        }

      private:
        fs::path path_;
    };

} // namespace

TEST_CASE("leak: FileMapping opened and closed N times returns every handle and block")
{
    const TempFile file{4096};
    const std::wstring wide = file.path().wstring();
    const auto report = measureLeaks("FileMapping open/close", 1, kIterations, [&](const std::size_t) {
        const auto mapping = pvdkit::win::FileMapping::open(wide);
        REQUIRE(mapping.has_value());
        CHECK((*mapping)->bytes().size() == 4096);
        CHECK((*mapping)->bytes()[0] == std::byte{'x'});
    });
    requireNoLeak(report);
}

TEST_CASE("leak: FileSource opened and closed N times returns every handle and block")
{
    const TempFile file{4096};
    pvdkit::win::FileSource source;
    const auto utf8 = file.utf8();
    const auto report = measureLeaks("FileSource open/close", 1, kIterations, [&](const std::size_t) {
        const auto data = source.open(utf8);
        REQUIRE(data.has_value());
        CHECK((*data)->bytes().size() == 4096);
    });
    requireNoLeak(report);
}

TEST_CASE("leak: every FileSource failure path returns every handle and block")
{
    const TempFile empty{0};
    pvdkit::win::FileSource source;
    const auto emptyPath = empty.utf8();
    const auto missingPath = (empty.path().parent_path() / "pvdkit-leak-does-not-exist.bin").string();
    const std::string invalidUtf8{"\xC3\x28"};
    const auto report = measureLeaks("FileSource failures", 1, kIterations, [&](const std::size_t) {
        const auto emptyFile = source.open(emptyPath);
        REQUIRE_FALSE(emptyFile.has_value());
        CHECK(emptyFile.error().code == ErrorCode::FileOpenFailed);
        const auto missing = source.open(missingPath);
        REQUIRE_FALSE(missing.has_value());
        CHECK(missing.error().code == ErrorCode::FileOpenFailed);
        const auto invalid = source.open(invalidUtf8);
        REQUIRE_FALSE(invalid.has_value());
        CHECK(invalid.error().code == ErrorCode::FileOpenFailed);
    });
    requireNoLeak(report);
}
