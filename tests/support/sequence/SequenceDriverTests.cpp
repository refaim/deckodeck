#include <algorithm>
#include <array>
#include <bit>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "core/IDecoder.hpp"
#include "pvd/PluginConstants.hpp"
#include "pvd/PluginFactory.hpp"
#include "pvd/Shim.hpp"
#include "sequence/SequenceAssertions.hpp"
#include "sequence/SequenceDriver.hpp"
#include "sequence/SequenceExpectations.hpp"

namespace pvdkit::sequence
{
    namespace
    {

        constexpr std::string_view kDeathMessage = "controlled invariant violation";
        constexpr wchar_t kDeathEnvironment[] = L"PVDKIT_SEQUENCE_DEATH_TEST";

        class CaptureFile final
        {
          public:
            explicit CaptureFile(std::filesystem::path path) : path_{std::move(path)}
            {
            }
            ~CaptureFile();

            CaptureFile(const CaptureFile &) = delete;
            CaptureFile &operator=(const CaptureFile &) = delete;
            CaptureFile(CaptureFile &&) = delete;
            CaptureFile &operator=(CaptureFile &&) = delete;

            [[nodiscard]] const std::filesystem::path &path() const noexcept
            {
                return path_;
            }

          private:
            std::filesystem::path path_;
        };

        CaptureFile::~CaptureFile()
        {
            std::error_code error;
            static_cast<void>(std::filesystem::remove(path_, error));
        }

        struct HandleDeleter
        {
            using pointer = HANDLE;

            void operator()(const HANDLE handle) const noexcept
            {
                static_cast<void>(CloseHandle(handle));
            }
        };

        using UniqueHandle = std::unique_ptr<HANDLE, HandleDeleter>;

        class ChildProcess final
        {
          public:
            explicit ChildProcess(const PROCESS_INFORMATION &process) noexcept
                : process_{process.hProcess}, thread_{process.hThread}
            {
            }

            ~ChildProcess()
            {
                stop();
            }

            ChildProcess(const ChildProcess &) = delete;
            ChildProcess &operator=(const ChildProcess &) = delete;
            ChildProcess(ChildProcess &&) = delete;
            ChildProcess &operator=(ChildProcess &&) = delete;

            [[nodiscard]] DWORD wait(const DWORD milliseconds) const noexcept
            {
                return WaitForSingleObject(process_.get(), milliseconds);
            }

            [[nodiscard]] BOOL exitCode(DWORD &code) const noexcept
            {
                return GetExitCodeProcess(process_.get(), &code);
            }

            void stop() const noexcept
            {
                static_cast<void>(TerminateProcess(process_.get(), 3));
                static_cast<void>(WaitForSingleObject(process_.get(), 30'000));
            }

          private:
            UniqueHandle process_;
            UniqueHandle thread_;
        };

#if PVDKIT_COVERAGE
        extern "C" int __llvm_profile_write_file();

        void exitAfterAbortSignal(int) noexcept
        {
            // Coverage runtimes flush at normal exit only; preserve this intentional death's counters.
            static_cast<void>(__llvm_profile_write_file());
            std::_Exit(3);
        }
#endif

        struct ChildResult
        {
            DWORD exitCode;
            std::string standardError;
        };

        std::vector<std::filesystem::path> fixtures()
        {
            std::vector<std::filesystem::path> result;
            for (const auto &entry : std::filesystem::recursive_directory_iterator{PVDKIT_FIXTURE_DIR}) {
                if (entry.is_regular_file() && entry.path().extension() != ".md") {
                    result.push_back(entry.path());
                }
            }
            std::ranges::sort(result);
            return result;
        }

        std::vector<std::byte> readFile(const std::filesystem::path &path)
        {
            const auto byteCount = static_cast<std::size_t>(std::filesystem::file_size(path));
            std::vector<std::byte> bytes(byteCount);
            std::ifstream stream{path, std::ios::binary};
            test::require(static_cast<bool>(stream));
            stream.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            test::require(static_cast<bool>(stream));
            return bytes;
        }

        ChildResult runDeathChild()
        {
            std::wstring executable(MAX_PATH, L'\0');
            const auto executableLength =
                GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
            test::require(executableLength > 0);
            test::require(executableLength < executable.size());
            executable.resize(executableLength);

            const CaptureFile capture{std::filesystem::temp_directory_path() /
                                      ("pvdkit-sequence-death-" + std::to_string(GetCurrentProcessId()) + ".txt")};
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION process{};
            auto command =
                L"\"" + executable + L"\" --test-case=\"sequence invariant violation child\" --no-colors=true";
            test::require(SetEnvironmentVariableW(kDeathEnvironment, capture.path().c_str()) != FALSE);
            const auto created = CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                                                &startup, &process);
            const ChildProcess child{process};
            test::require(SetEnvironmentVariableW(kDeathEnvironment, nullptr) != FALSE);
            test::require(created != FALSE);
            const auto waitResult = child.wait(30'000);
            // This is deliberately unconditional: on timeout it terminates and reaps before REQUIRE reports;
            // after normal termination the repeated request is harmless and keeps the cleanup branch-free.
            child.stop();
            test::require(waitResult == WAIT_OBJECT_0);
            DWORD exitCode = 0;
            test::require(child.exitCode(exitCode) != FALSE);

            std::ifstream captured{capture.path(), std::ios::binary};
            test::require(static_cast<bool>(captured));
            std::string standardError{std::istreambuf_iterator<char>{captured}, std::istreambuf_iterator<char>{}};
            return {exitCode, std::move(standardError)};
        }

    } // namespace

    TEST_CASE("capture file ownership removes the file at scope exit")
    {
        const auto path = std::filesystem::temp_directory_path() /
                          ("pvdkit-sequence-cleanup-" + std::to_string(GetCurrentProcessId()) + ".txt");
        {
            const CaptureFile capture{path};
            std::ofstream stream{capture.path(), std::ios::binary};
            test::require(static_cast<bool>(stream));
            stream << "temporary";
        }
        test::check(!std::filesystem::exists(path));
    }

    TEST_CASE("row spans address top-down and bottom-up image buffers without escaping their bounds")
    {
        std::array<BYTE, 24> pixels{};
        pvdInfoDecode decoded{pixels.data(), nullptr, 0, 24, 0, 8};

        const auto topFirst = rowSpan(decoded, 3, 0);
        const auto topLast = rowSpan(decoded, 3, 2);
        test::require(topFirst.has_value());
        test::require(topLast.has_value());
        test::check(topFirst->data() == reinterpret_cast<const std::byte *>(pixels.data()));
        test::check(topLast->data() == reinterpret_cast<const std::byte *>(pixels.data() + 16));
        test::check(topFirst->size() == 8);

        decoded.lImagePitch = -8;
        const auto bottomFirst = rowSpan(decoded, 3, 0);
        const auto bottomLast = rowSpan(decoded, 3, 2);
        test::require(bottomFirst.has_value());
        test::require(bottomLast.has_value());
        test::check(bottomFirst->data() == reinterpret_cast<const std::byte *>(pixels.data() + 16));
        test::check(bottomLast->data() == reinterpret_cast<const std::byte *>(pixels.data()));
        test::check(bottomLast->size() == 8);
    }

    TEST_CASE("row spans reject invalid rows and overflowing buffer bounds")
    {
        std::array<BYTE, 8> pixels{};
        pvdInfoDecode decoded{pixels.data(), nullptr, 0, 24, 0, 8};
        test::check(!rowSpan(decoded, 0, 0).has_value());
        test::check(!rowSpan(decoded, 1, 1).has_value());
        test::check(!rowSpan(decoded, 1, 0, 7).has_value());
        decoded.pImage = nullptr;
        test::check(!rowSpan(decoded, 1, 0).has_value());

        decoded.pImage = std::bit_cast<BYTE *>(std::numeric_limits<std::uintptr_t>::max() - 3U);
        test::check(!rowSpan(decoded, 1, 0).has_value());

        decoded.pImage = pixels.data();
        decoded.lImagePitch = std::numeric_limits<INT32>::min();
        // Exercise the size_t-overflow rejection on x64 against a simulated 32-bit size bound;
        // the three-argument overload supplies the architecture's real size_t maximum.
        test::check(!rowSpan(decoded, 3, 0, std::numeric_limits<std::uint32_t>::max()).has_value());
    }

    TEST_CASE("the pure Shim output checks explain every rejected contract shape")
    {
        std::array<char, kMaxStringBytes> unterminated{};
        unterminated.fill('x');

        test::check(!checkInvariant(true, "unused").has_value());
        test::check(checkInvariant(false, "violation").value() == "violation");
        test::check(!checkProgress(0, 1).has_value());
        test::check(checkProgress(0, 0).has_value());
        test::check(checkProgress(1, 1).has_value());

        pvdInfoPlugin plugin{10, "Plugin", "1.0.0", "comments"};
        test::check(!checkPluginInfo(plugin).has_value());
        plugin.pName = nullptr;
        test::check(checkPluginInfo(plugin).has_value());
        plugin.pName = "Plugin";
        plugin.pVersion = nullptr;
        test::check(checkPluginInfo(plugin).has_value());
        plugin.pVersion = "1.0.0";
        plugin.pComments = nullptr;
        test::check(checkPluginInfo(plugin).has_value());
        plugin.pComments = unterminated.data();
        test::check(checkPluginInfo(plugin).has_value());

        pvdInfoImage image{1, 0, "Format", "Compression", "comments"};
        test::check(!checkImageInfo(image).has_value());
        image.nPages = 0;
        test::check(checkImageInfo(image).has_value());
        image.nPages = 1;
        image.pFormatName = nullptr;
        test::check(checkImageInfo(image).has_value());
        image.pFormatName = "Format";
        image.pCompression = nullptr;
        test::check(checkImageInfo(image).has_value());
        image.pCompression = "Compression";
        image.pComments = nullptr;
        test::check(checkImageInfo(image).has_value());
        image.pComments = unterminated.data();
        test::check(checkImageInfo(image).has_value());

        pvdInfoPage page{2, 3, 24, 0};
        test::check(!checkPageInfo(page).has_value());
        page.lWidth = 0;
        test::check(checkPageInfo(page).has_value());
        page.lWidth = 2;
        page.lHeight = 0;
        test::check(checkPageInfo(page).has_value());

        std::array<BYTE, 48> pixels{};
        page.lHeight = 3;
        pvdInfoDecode decoded{pixels.data(), nullptr, 0, 24, 0, 8};
        test::check(!checkDecoded(page, decoded).has_value());
        decoded.pImage = nullptr;
        test::check(checkDecoded(page, decoded).has_value());
        decoded.pImage = pixels.data();
        decoded.nBPP = 8;
        test::check(checkDecoded(page, decoded).has_value());
        decoded.nBPP = 64;
        decoded.lImagePitch = 16;
        test::check(!checkDecoded(page, decoded).has_value());
        decoded.lImagePitch = 15;
        test::check(checkDecoded(page, decoded).has_value());
        decoded.nBPP = 32;
        decoded.lImagePitch = 7;
        test::check(checkDecoded(page, decoded).has_value());
        decoded.lImagePitch = 8;
        decoded.pImage = std::bit_cast<BYTE *>(std::numeric_limits<std::uintptr_t>::max() - 3U);
        test::check(checkDecoded(page, decoded).has_value());
        decoded.pImage = pixels.data();
        decoded.lImagePitch = -8;
        test::check(!checkDecoded(page, decoded).has_value());
    }

    TEST_CASE("sequence invariant violation child")
    {
        std::array<wchar_t, MAX_PATH> capturePath{};
        const auto pathLength =
            GetEnvironmentVariableW(kDeathEnvironment, capturePath.data(), static_cast<DWORD>(capturePath.size()));
        if (pathLength == 0) {
            return;
        }
        test::require(pathLength < capturePath.size());
        FILE *redirected = nullptr;
        test::require(_wfreopen_s(&redirected, capturePath.data(), L"wb", stderr) == 0);
        test::require(redirected != nullptr);
#if PVDKIT_COVERAGE
        test::require(std::signal(SIGABRT, exitAfterAbortSignal) != SIG_ERR);
#endif
        enforceInvariant(checkInvariant(false, kDeathMessage));
    }

    TEST_CASE("an invariant violation flushes its diagnostic and terminates the subprocess")
    {
        const auto child = runDeathChild();
        test::check(child.exitCode != 0);
        test::check(child.standardError.find("host-sequence invariant failed: controlled invariant violation\n") !=
                    std::string::npos);
    }

    TEST_CASE("the in-process host sequence is deterministic for every plugin fixture")
    {
        constexpr core::DecoderOptions options{1, false, std::uint64_t{4} * 1024U * 1024U, 8192};
        const auto plugin = pvd::makePlugin(options);
        test::require(plugin != nullptr);
        pvd::Shim shim{*plugin, pvd::kPluginIdentity};

        const auto allFixtures = fixtures();
        test::require(!allFixtures.empty());
        std::uint32_t forcedAborts = 0;
        std::size_t acceptedCount = 0;

        for (const auto &path : allFixtures) {
            const auto relative = std::filesystem::relative(path, PVDKIT_FIXTURE_DIR).generic_string();
            CAPTURE(relative);
            const auto expected = fixtureDisposition(relative);
            test::require(expected.has_value());
            const auto bytes = readFile(path);
            const auto first = replayHostSequence(shim, bytes);
            const auto second = replayHostSequence(shim, bytes);
            test::check(first == second);

            if (*expected == FixtureDisposition::Rejected) {
                test::check(first == SequenceReport{});
                continue;
            }

            ++acceptedCount;
            test::check(first.opened);
            test::check(first.pages >= 1);
            const auto replayedPages = std::min(first.pages, std::uint32_t{4});
            test::check(first.decoded + first.aborted + first.failed == replayedPages);
            if (*expected == FixtureDisposition::Accepted) {
                test::check(first.failed == 0);
                const auto forced = replayHostSequence(shim, bytes, AbortPolicy{1, 1});
                test::check(forced.opened);
                test::check(forced.failed == 0);
                test::check(forced.decoded == 0);
                test::check(forced.aborted == std::min(forced.pages, std::uint32_t{4}));
                forcedAborts += forced.aborted;
            } else {
                const auto noAbort = replayHostSequence(shim, bytes, AbortPolicy{0, 0});
                test::check(noAbort.opened);
                test::check(noAbort.aborted == 0);
                test::check(noAbort.failed >= 1);
                test::check(noAbort.decoded + noAbort.failed == std::min(noAbort.pages, std::uint32_t{4}));
            }
        }

        test::check(acceptedCount > 0);
        test::check(forcedAborts > 0);
    }

} // namespace pvdkit::sequence
