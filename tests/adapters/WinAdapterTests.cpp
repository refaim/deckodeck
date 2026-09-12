#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <winioctl.h>

#include <doctest/doctest.h>

#include "adapters/win/FileMapping.hpp"
#include "adapters/win/FileSource.hpp"
#include "adapters/win/Utf8.hpp"
#include "core/Error.hpp"

namespace
{

    namespace fs = std::filesystem;
    using pvdkit::core::ErrorCode;

    constexpr std::size_t kMaxPath = 260;
    constexpr std::array kPayload{std::byte{0x41}, std::byte{0x56}, std::byte{0x49}, std::byte{0x46}};

    // The tests create and delete trees deeper than MAX_PATH themselves, so their own filesystem
    // calls always use the extended-length form regardless of what the adapter decides.
    fs::path extended(const fs::path &path)
    {
        return fs::path{LR"(\\?\)" + path.wstring()};
    }

    // Two adapter_tests processes (e.g. parallel presets) must never share, and therefore never
    // delete, each other's trees: the process id is part of the leaf directory name.
    class TempTree
    {
      public:
        explicit TempTree(const std::wstring_view leaf)
            : path_(fs::temp_directory_path() /
                    fs::path{std::wstring{leaf} + L"-" + std::to_wstring(GetCurrentProcessId())})
        {
            std::error_code ignored;
            fs::remove_all(extended(path_), ignored);
            fs::create_directories(path_);
        }

        ~TempTree()
        {
            std::error_code ignored;
            fs::remove_all(extended(path_), ignored);
        }

        TempTree(const TempTree &) = delete;
        TempTree &operator=(const TempTree &) = delete;

        [[nodiscard]] const fs::path &path() const
        {
            return path_;
        }

        // Creates `<root>/aaaa.../bbbb.../.../eeee...` (5 x 52 characters) and returns it.
        [[nodiscard]] fs::path createLongDirectory() const
        {
            auto longDirectory = path_;
            for (int index = 0; index < 5; ++index) {
                longDirectory /= std::wstring(52, static_cast<wchar_t>(L'a' + index));
            }
            fs::create_directories(extended(longDirectory));
            return longDirectory;
        }

      private:
        fs::path path_;
    };

    void writeBytes(const fs::path &path, const std::span<const std::byte> bytes)
    {
        std::ofstream stream{extended(path), std::ios::binary};
        REQUIRE(stream.good());
        stream.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        REQUIRE(stream.good());
    }

    std::string utf8Path(const fs::path &path)
    {
        const auto utf8 = path.u8string();
        return {utf8.begin(), utf8.end()};
    }

    std::string utf8Path(const std::wstring &path)
    {
        return utf8Path(fs::path{path});
    }

    void checkOpens(pvdkit::win::FileSource &source, const std::string &utf8)
    {
        auto opened = source.open(utf8);
        const std::string openError = opened ? std::string{} : opened.error().detail;
        CAPTURE(utf8);
        CAPTURE(openError);
        REQUIRE(opened.has_value());
        CHECK(std::ranges::equal((*opened)->bytes(), kPayload));
    }

} // namespace

TEST_CASE("UTF-8 paths convert to UTF-16 and reject malformed input")
{
    const auto empty = pvdkit::win::utf8ToWide("");
    REQUIRE(empty.has_value());
    CHECK(empty->empty());

    const auto converted = pvdkit::win::utf8ToWide("folder/\xD1\x84\xD0\xB0\xD0\xB9\xD0\xBB-\xF0\x9F\x98\x80.avif");
    REQUIRE(converted.has_value());
    CHECK(*converted == L"folder/\u0444\u0430\u0439\u043b-\U0001F600.avif");

    const auto invalid = pvdkit::win::utf8ToWide(std::string_view{"\xC3\x28", 2});
    REQUIRE_FALSE(invalid.has_value());
    CHECK(invalid.error().code == ErrorCode::FileOpenFailed);
    CHECK_FALSE(invalid.error().detail.empty());
}

TEST_CASE("UTF-8 inputs longer than INT_MAX bytes are refused before MultiByteToWideChar")
{
    constexpr auto intMax = static_cast<std::size_t>(std::numeric_limits<int>::max());
    CHECK(pvdkit::win::detail::utf8Length(0) == 0);
    CHECK(pvdkit::win::detail::utf8Length(intMax) == std::numeric_limits<int>::max());

    const auto tooLong = pvdkit::win::detail::utf8Length(intMax + 1);
    REQUIRE_FALSE(tooLong.has_value());
    CHECK(tooLong.error().code == ErrorCode::FileOpenFailed);
    CHECK(tooLong.error().detail == "the UTF-8 path is longer than INT_MAX bytes");
}

TEST_CASE("Win32 paths below MAX_PATH are passed through untouched")
{
    using pvdkit::win::toWin32Path;

    for (const auto path : {L"", L"relative\\file.avif", L"relative/../file.avif", L"C:file.avif", L"C:/file.avif",
                            L"C:\\folder\\..\\file.avif", LR"(\\server\share\file.avif)", LR"(\\?\C:\file.avif)",
                            LR"(\\.\C:\file.avif)", LR"(\\.\PhysicalDrive0)"}) {
        CAPTURE(std::wstring_view{path}.size());
        const auto result = toWin32Path(path);
        REQUIRE(result.has_value());
        CHECK(*result == path);
    }

    // The longest short path: MAX_PATH - 1 characters (room for the terminator) is still untouched.
    const std::wstring longestShort = L"C:/" + std::wstring(kMaxPath - 4, L'a');
    REQUIRE(longestShort.size() == kMaxPath - 1);
    CHECK(toWin32Path(longestShort) == longestShort);
}

TEST_CASE("Win32 paths at or above MAX_PATH are normalised and given the extended prefix")
{
    using pvdkit::win::toWin32Path;
    const std::wstring longName(kMaxPath, L'a');

    // Forward slashes and `..` are resolved by GetFullPathNameW before the prefix is added.
    CHECK(toWin32Path(L"C:/data/" + longName + L"/../" + longName + L"/f.avif") ==
          LR"(\\?\C:\data\)" + longName + LR"(\f.avif)");
    CHECK(toWin32Path(LR"(C:\)" + longName) == LR"(\\?\C:\)" + longName);

    // Exactly MAX_PATH characters is already too long for the short form.
    const std::wstring exactlyMaxPath = LR"(C:\)" + std::wstring(kMaxPath - 3, L'b');
    REQUIRE(exactlyMaxPath.size() == kMaxPath);
    CHECK(toWin32Path(exactlyMaxPath) == LR"(\\?\)" + exactlyMaxPath);

    // UNC paths, in either separator style, become \\?\UNC\server\share\... .
    CHECK(toWin32Path(LR"(\\server\share\)" + longName + LR"(\f.avif)") ==
          LR"(\\?\UNC\server\share\)" + longName + LR"(\f.avif)");
    CHECK(toWin32Path(L"//server/share/" + longName + L"/f.avif") ==
          LR"(\\?\UNC\server\share\)" + longName + LR"(\f.avif)");

    // Forward-slash spellings of the NT and device namespaces are not recognised as prefixes, so
    // they go through GetFullPathNameW, which canonicalises them into `\\?\` / `\\.\`; that result
    // must then be used as is rather than wrapped into `\\?\UNC\?\...`.
    CHECK(toWin32Path(L"//?/C:/" + longName + L"/f.avif") == LR"(\\?\C:\)" + longName + LR"(\f.avif)");
    CHECK(toWin32Path(L"//./C:/" + longName + L"/f.avif") == LR"(\\.\C:\)" + longName + LR"(\f.avif)");

    // Paths that already name the NT or device namespace are never rewritten, however long.
    const std::wstring longExtended = LR"(\\?\C:\)" + longName;
    CHECK(toWin32Path(longExtended) == longExtended);
    const std::wstring longUncExtended = LR"(\\?\UNC\server\share\)" + longName;
    CHECK(toWin32Path(longUncExtended) == longUncExtended);
    const std::wstring longDevice = LR"(\\.\C:\)" + longName;
    CHECK(toWin32Path(longDevice) == longDevice);

    // GetFullPathNameW refuses inputs beyond the NT path limit (ERROR_FILENAME_EXCED_RANGE).
    const auto tooLong = toWin32Path(LR"(C:\)" + std::wstring(40000, L'c'));
    REQUIRE_FALSE(tooLong.has_value());
    CHECK(tooLong.error().code == ErrorCode::FileOpenFailed);
    CHECK(tooLong.error().detail == "GetFullPathNameW rejected the path (Win32 error 206)");
}

TEST_CASE("file mapping exposes exact file bytes and reports invalid file kinds")
{
    TempTree tree{L"pvdkit-adapter-file-mapping"};
    constexpr std::array bytes{std::byte{0x00}, std::byte{0x7f}, std::byte{0x80}, std::byte{0xff}};
    const auto normal = tree.path() / L"normal.bin";
    writeBytes(normal, bytes);

    auto mapped = pvdkit::win::FileMapping::open(normal.wstring());
    REQUIRE(mapped.has_value());
    CHECK(std::ranges::equal((*mapped)->bytes(), bytes));

    const std::wstring pathWithTrailingData = normal.wstring() + L".not-part-of-view";
    const auto pathView = std::wstring_view{pathWithTrailingData}.substr(0, normal.wstring().size());
    const auto mappedView = pvdkit::win::FileMapping::open(pathView);
    REQUIRE(mappedView.has_value());
    CHECK(std::ranges::equal((*mappedView)->bytes(), bytes));

    const auto emptyPath = tree.path() / L"empty.bin";
    writeBytes(emptyPath, {});
    const auto empty = pvdkit::win::FileMapping::open(emptyPath.wstring());
    REQUIRE_FALSE(empty.has_value());
    CHECK(empty.error().code == ErrorCode::FileOpenFailed);
    // Nothing in Win32 failed for an empty file, so the detail must not invent a Win32 error code.
    CHECK(empty.error().detail == "file is empty");

    const auto missing = pvdkit::win::FileMapping::open((tree.path() / L"missing.bin").wstring());
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code == ErrorCode::FileOpenFailed);
    CHECK_FALSE(missing.error().detail.empty());

    const auto directory = pvdkit::win::FileMapping::open(tree.path().wstring());
    REQUIRE_FALSE(directory.has_value());
    CHECK(directory.error().code == ErrorCode::FileOpenFailed);
    CHECK_FALSE(directory.error().detail.empty());
}

TEST_CASE("file source supports Unicode and extended-length paths")
{
    TempTree tree{L"pvdkit-adapter-file-source"};
    pvdkit::win::FileSource source;

    const auto unicode = tree.path() / L"\u0444\u0430\u0439\u043b-\U0001F600.bin";
    writeBytes(unicode, kPayload);
    checkOpens(source, utf8Path(unicode));

    const auto longFile = tree.createLongDirectory() / L"payload.bin";
    writeBytes(longFile, kPayload);
    REQUIRE(longFile.wstring().size() > kMaxPath);
    checkOpens(source, utf8Path(longFile));

    const auto invalid = source.open(std::string_view{"\xF0\x28\x8C\x28", 4});
    REQUIRE_FALSE(invalid.has_value());
    CHECK(invalid.error().code == ErrorCode::FileOpenFailed);
    CHECK_FALSE(invalid.error().detail.empty());

    const auto missing = source.open(utf8Path(tree.path() / L"missing.bin"));
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code == ErrorCode::FileOpenFailed);

    const auto tooLong = source.open(utf8Path(LR"(C:\)" + std::wstring(40000, L'c')));
    REQUIRE_FALSE(tooLong.has_value());
    CHECK(tooLong.error().code == ErrorCode::FileOpenFailed);
    CHECK(tooLong.error().detail.starts_with("GetFullPathNameW"));
}

TEST_CASE("file source opens short absolute paths that rely on Win32 normalisation")
{
    TempTree tree{L"pvdkit-adapter-short-paths"};
    const auto file = tree.path() / L"normal.bin";
    writeBytes(file, kPayload);
    pvdkit::win::FileSource source;

    // `C:\...\Temp\pvdkit-adapter-short-paths/../pvdkit-adapter-short-paths/normal.bin`: both the
    // forward slashes and the `..` segment need Win32's own normalisation, which `\\?\` disables.
    const auto leaf = tree.path().filename().wstring();
    const std::wstring unnormalised = tree.path().wstring() + L"/../" + leaf + L"/" + file.filename().wstring();
    REQUIRE(unnormalised.size() < kMaxPath);
    checkOpens(source, utf8Path(unnormalised));

    // A trailing dot and space are stripped by Win32 normalisation as well.
    checkOpens(source, utf8Path(file) + ". ");

    // A device-namespace path is handed over untouched and still opens the file.
    checkOpens(source, utf8Path(LR"(\\.\)" + file.wstring()));
}

TEST_CASE("file source opens long paths written with forward slashes and dot-dot segments")
{
    TempTree tree{L"pvdkit-adapter-long-slash"};
    const auto longDirectory = tree.createLongDirectory();
    const auto longFile = longDirectory / L"payload.bin";
    writeBytes(longFile, kPayload);

    const std::wstring slashed =
        longDirectory.generic_wstring() + L"/../" + longDirectory.filename().wstring() + L"/payload.bin";
    REQUIRE(slashed.size() >= kMaxPath);
    pvdkit::win::FileSource source;
    checkOpens(source, utf8Path(slashed));

    // An already extended long path is passed through as is.
    checkOpens(source, utf8Path(extended(longFile)));
}

TEST_CASE("a file beyond the 32-bit address space is refused, never truncated")
{
    // 4 GiB + 1 byte: one more than a 32-bit std::size_t can count, so the 64-bit file size must
    // be checked before it is narrowed. The file is sparse (no disk space is consumed and no
    // 4 GiB write happens); the test insists on sparse support rather than falling back.
    constexpr std::uint64_t kSize = (std::uint64_t{1} << 32) + 1;
    const TempTree tree{L"pvdkit-huge"};
    const auto path = tree.path() / L"sparse.bin";
    {
        pvdkit::win::UniqueHandle file{CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
        REQUIRE(file.get() != INVALID_HANDLE_VALUE);
        DWORD ignored = 0;
        REQUIRE(DeviceIoControl(file.get(), FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &ignored, nullptr) != FALSE);
        LARGE_INTEGER end{};
        end.QuadPart = static_cast<LONGLONG>(kSize);
        REQUIRE(SetFilePointerEx(file.get(), end, nullptr, FILE_BEGIN) != FALSE);
        REQUIRE(SetEndOfFile(file.get()) != FALSE);
    }

    pvdkit::win::UniqueHandle file{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                               FILE_ATTRIBUTE_NORMAL, nullptr)};
    REQUIRE(file.get() != INVALID_HANDLE_VALUE);
    const auto size = pvdkit::win::detail::fileSize(file);
    pvdkit::win::FileSource source;
    const auto opened = source.open(utf8Path(path));
    if constexpr (sizeof(std::size_t) == 4) {
        REQUIRE_FALSE(size.has_value());
        CHECK(size.error().code == ErrorCode::TooLarge);
        CHECK(size.error().detail == "file size (4294967297) does not fit in 32 bits");

        REQUIRE_FALSE(opened.has_value());
        CHECK(opened.error().code == ErrorCode::TooLarge);
        CHECK(opened.error().detail == "file size (4294967297) does not fit in 32 bits");
    } else {
        REQUIRE(size.has_value());
        CHECK(*size == kSize);

        REQUIRE(opened.has_value());
        CHECK((*opened)->bytes().size() == kSize);
    }
}

TEST_CASE("file-mapping Win32 operations report failures as values")
{
    const auto size = pvdkit::win::detail::fileSize(pvdkit::win::UniqueHandle{});
    REQUIRE_FALSE(size.has_value());
    CHECK(size.error().code == ErrorCode::FileOpenFailed);

    const auto mapping = pvdkit::win::detail::createReadOnlyMapping(pvdkit::win::UniqueHandle{});
    REQUIRE_FALSE(mapping.has_value());
    CHECK(mapping.error().code == ErrorCode::FileOpenFailed);

    const auto view = pvdkit::win::detail::mapReadOnly(pvdkit::win::UniqueHandle{});
    REQUIRE_FALSE(view.has_value());
    CHECK(view.error().code == ErrorCode::FileOpenFailed);
}
