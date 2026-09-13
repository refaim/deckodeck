// The host-like leak scenarios, compiled into every plugin's <id>_leak_tests
// (pvdkit_add_plugin_e2e_tests in cmake/pvdkit-plugin.cmake) against that plugin's DLL and fixture
// directory. Every scenario drives the DLL through the shared host driver (tests/e2e/PluginHost)
// exactly as 0PictureView.dll would, and asserts - after a warm-up - a zero delta in live heap
// blocks, heap bytes and handles, and a bounded delta in private bytes (tests/support/LeakCheck.hpp).
// The list of scenarios is docs/tasks/task10-leaks.md, level 1.
//
// Fixtures are discovered, not listed: every regular file in PVDKIT_FIXTURE_DIR that is not *.md
// is offered to the plugin once at start-up (both host modes); what it accepts is "accepted",
// with its page count, and what it refuses is "rejected". A plugin's fixture directory must hold
// at least one of each; animated fixtures (more than one page) are optional and, when present,
// get their page switches.
//
// Cost: every scenario performs about N = 200 host-level operations after its warm-up
// (PVDKIT_LEAK_ITERATIONS overrides N; a soak run is PVDKIT_LEAK_ITERATIONS=5000). Scenarios that
// cycle through the fixtures warm up with one full cycle so every code path ran once before the
// first snapshot; scenarios whose unit is a whole pass over the folder or a batch of N sessions
// run that unit once as warm-up.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <latch>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "HostileCorpus.hpp"
#include "LeakCheck.hpp"
#include "PluginHost.hpp"

namespace pvdkit::leak
{
    namespace
    {

        using e2e::DecodedPage;
        using e2e::FixtureFile;
        using e2e::OpenedImage;
        using e2e::OpenMode;
        using e2e::PluginExports;
        using e2e::PluginLibrary;
        using test::measureLeaks;
        using test::requireNoLeak;

        constexpr std::size_t kDefaultIterations = 200;
        constexpr std::size_t kLibraryCycles = 20;
        constexpr int kThreads = 8;
        constexpr std::size_t kWindow = 3;     // files the browsing host keeps open: current + prefetched neighbours
        constexpr std::size_t kAbortEvery = 5; // browsing: every 5th file's decode is aborted by the callback
        constexpr std::size_t kMutants = 200;
        constexpr std::uint32_t kCorpusSeed = 20260912;

        std::size_t iterations()
        {
            return test::leakIterations(kDefaultIterations);
        }

        struct Fixture
        {
            FixtureFile file;
            std::uint32_t pages = 0;
        };

        struct Corpus
        {
            std::vector<Fixture> accepted; // opened by the plugin in both modes, with the page count
            std::vector<Fixture> rejected; // refused in both modes
            std::size_t animated = 0;      // accepted fixtures with more than one page
        };

        PluginLibrary loadPlugin()
        {
            auto loaded = PluginLibrary::load(e2e::pluginPath());
            const std::string loadError = loaded ? std::string{} : loaded.error();
            CAPTURE(loadError);
            REQUIRE(loaded.has_value());
            return std::move(*loaded);
        }

        Corpus discover()
        {
            std::vector<std::filesystem::path> paths;
            for (const auto &entry : std::filesystem::directory_iterator{std::filesystem::path{PVDKIT_FIXTURE_DIR}}) {
                if (entry.is_regular_file() && entry.path().extension() != ".md") {
                    paths.push_back(entry.path());
                }
            }
            std::ranges::sort(paths);
            REQUIRE_FALSE(paths.empty());

            const auto plugin = e2e::loadInitializedPlugin();
            const auto &exports = plugin.exports();
            Corpus corpus;
            for (const auto &path : paths) {
                const auto utf8Name = path.filename().u8string();
                Fixture fixture{e2e::readFixture(
                    std::string_view{reinterpret_cast<const char *>(utf8Name.data()), utf8Name.size()})};
                CAPTURE(fixture.file.utf8Path);
                const auto disk = e2e::openImage(exports, fixture.file, OpenMode::Disk);
                const auto memory = e2e::openImage(exports, fixture.file, OpenMode::Memory);
                REQUIRE(disk.has_value() == memory.has_value());
                if (disk) {
                    fixture.pages = disk->info.nPages;
                    REQUIRE(fixture.pages >= 1);
                    REQUIRE(memory->info.nPages == fixture.pages);
                    exports.fileClose(disk->context);
                    exports.fileClose(memory->context);
                    corpus.animated += fixture.pages > 1 ? 1 : 0;
                    corpus.accepted.push_back(std::move(fixture));
                } else {
                    corpus.rejected.push_back(std::move(fixture));
                }
            }
            exports.exit();
            REQUIRE_FALSE(corpus.accepted.empty());
            REQUIRE_FALSE(corpus.rejected.empty());
            std::printf("[leak] fixtures: %zu accepted (%zu animated), %zu rejected, N = %zu\n", corpus.accepted.size(),
                        corpus.animated, corpus.rejected.size(), iterations());
            return corpus;
        }

        // Discovered once per process, through the DLL, before any measurement.
        const Corpus &corpus()
        {
            static const Corpus instance = discover();
            return instance;
        }

        const Fixture &cycle(const std::vector<Fixture> &fixtures, const std::size_t index)
        {
            return fixtures[index % fixtures.size()];
        }

        OpenMode alternate(const std::size_t index)
        {
            return index % 2 == 0 ? OpenMode::Disk : OpenMode::Memory;
        }

        OpenedImage open(const PluginExports &exports, const Fixture &fixture, const OpenMode mode)
        {
            auto opened = e2e::openImage(exports, fixture.file, mode);
            REQUIRE(opened.has_value());
            REQUIRE(opened->info.nPages == fixture.pages);
            return *opened;
        }

        DecodedPage decode(const PluginExports &exports, void *context, const std::uint32_t page)
        {
            auto decoded = e2e::decodePage(exports, context, page, nullptr, nullptr);
            REQUIRE(decoded.has_value());
            return *decoded;
        }

        // Reads every byte of every row; a buffer shorter than it claims trips ASan here, and the
        // sum keeps the reads alive.
        std::uint64_t checksum(const DecodedPage &page)
        {
            std::uint64_t sum = 0;
            for (std::uint32_t y = 0; y < page.page.lHeight; ++y) {
                for (const std::byte value : page.row(y)) {
                    sum += std::to_integer<std::uint64_t>(value);
                }
            }
            return sum;
        }

        std::vector<DecodedPage> decodeAll(const PluginExports &exports, const OpenedImage &image)
        {
            std::vector<DecodedPage> pages;
            pages.reserve(image.info.nPages);
            for (std::uint32_t page = 0; page < image.info.nPages; ++page) {
                pages.push_back(decode(exports, image.context, page));
                static_cast<void>(checksum(pages.back()));
            }
            return pages;
        }

        void freeAll(const PluginExports &exports, const OpenedImage &image, std::vector<DecodedPage> &pages)
        {
            for (auto &page : pages) {
                exports.pageFree(image.context, &page.decode);
            }
            pages.clear();
        }

        enum class Free : std::uint8_t
        {
            BeforeClose,
            Never // the host may close a file with its pages still out
        };

        // One host round trip: open, page info + decode of every page, (free,) close.
        void roundTrip(const PluginExports &exports, const Fixture &fixture, const OpenMode mode, const Free free)
        {
            const auto image = open(exports, fixture, mode);
            auto pages = decodeAll(exports, image);
            if (free == Free::BeforeClose) {
                freeAll(exports, image, pages);
            }
            exports.fileClose(image.context);
        }

        // The callback that aborts when the plugin reports step `*context`.
        BOOL __stdcall abortAtStep(void *context, const UINT32 step, const UINT32)
        {
            return step == *static_cast<const std::uint32_t *>(context) ? FALSE : TRUE;
        }

        // What one worker thread saw. Workers never assert: a failing REQUIRE throws, and an
        // exception leaving a thread terminates the process; the main thread asserts after the join.
        std::string workerRoundTrip(const PluginExports &exports, const Fixture &fixture, const OpenMode mode)
        {
            const auto &file = fixture.file;
            const auto headSize =
                mode == OpenMode::Disk ? std::min(file.bytes.size(), e2e::kHostHeadSize) : file.bytes.size();
            const auto fileSize = mode == OpenMode::Disk ? static_cast<INT64>(file.bytes.size()) : INT64{0};
            pvdInfoImage info{};
            void *context = nullptr;
            if (exports.fileOpen(file.utf8Path.c_str(), fileSize, reinterpret_cast<const BYTE *>(file.bytes.data()),
                                 static_cast<UINT32>(headSize), &info, &context) == FALSE) {
                return "pvdFileOpen answered FALSE for " + file.utf8Path;
            }
            if (context == nullptr) {
                return "pvdFileOpen answered TRUE without a context for " + file.utf8Path;
            }
            std::string failure;
            for (std::uint32_t page = 0; page < info.nPages && failure.empty(); ++page) {
                DecodedPage decoded;
                if (exports.pageInfo(context, page, &decoded.page) == FALSE) {
                    failure = "pvdPageInfo answered FALSE";
                } else if (exports.pageDecode(context, page, &decoded.decode, nullptr, nullptr) == FALSE) {
                    failure = "pvdPageDecode answered FALSE";
                } else if (decoded.decode.pImage == nullptr ||
                           !e2e::isSupportedDecodeLayout(decoded.page.lWidth, decoded.decode.nBPP,
                                                         decoded.decode.lImagePitch)) {
                    failure = "pvdPageDecode handed out an unusable page";
                } else {
                    static_cast<void>(checksum(decoded));
                    exports.pageFree(context, &decoded.decode);
                }
            }
            exports.fileClose(context);
            return failure.empty() ? failure : failure + " for " + file.utf8Path;
        }

    } // namespace

    TEST_CASE("leak: init, open from disk, page info, decode, free, close, exit")
    {
        const auto &fixtures = corpus().accepted;
        const auto plugin = loadPlugin();
        const auto &exports = plugin.exports();
        const auto report = measureLeaks("disk round trip", fixtures.size(), iterations(), [&](const std::size_t i) {
            REQUIRE(exports.init() == PVD_CURRENT_INTERFACE_VERSION);
            roundTrip(exports, cycle(fixtures, i), OpenMode::Disk, Free::BeforeClose);
            exports.exit();
        });
        requireNoLeak(report);
    }

    TEST_CASE("leak: init, open from memory, page info, decode, free, close, exit")
    {
        const auto &fixtures = corpus().accepted;
        const auto plugin = loadPlugin();
        const auto &exports = plugin.exports();
        const auto report = measureLeaks("memory round trip", fixtures.size(), iterations(), [&](const std::size_t i) {
            REQUIRE(exports.init() == PVD_CURRENT_INTERFACE_VERSION);
            roundTrip(exports, cycle(fixtures, i), OpenMode::Memory, Free::BeforeClose);
            exports.exit();
        });
        requireNoLeak(report);
    }

    TEST_CASE("leak: open, decode, close without freeing the pages")
    {
        const auto &fixtures = corpus().accepted;
        const auto plugin = e2e::loadInitializedPlugin();
        const auto &exports = plugin.exports();
        const auto report = measureLeaks("close without free", fixtures.size(), iterations(), [&](const std::size_t i) {
            roundTrip(exports, cycle(fixtures, i), alternate(i), Free::Never);
        });
        requireNoLeak(report);
        exports.exit();
    }

    TEST_CASE("leak: decode aborted by the callback at step 0, 1 and 2")
    {
        const auto &fixtures = corpus().accepted;
        const auto plugin = e2e::loadInitializedPlugin();
        const auto &exports = plugin.exports();
        const auto report = measureLeaks("abort at each step", fixtures.size(), iterations(), [&](const std::size_t i) {
            const auto image = open(exports, cycle(fixtures, i), alternate(i));
            for (std::uint32_t step = 0; step < 3; ++step) {
                pvdInfoDecode info{};
                CHECK(exports.pageDecode(image.context, 0, &info, abortAtStep, &step) == FALSE);
                CHECK(info.pImage == nullptr);
                exports.pageFree(image.context, &info); // nothing was handed out: a no-op
            }
            exports.fileClose(image.context);
        });
        requireNoLeak(report);
        exports.exit();
    }

    TEST_CASE("leak: every rejection path")
    {
        const auto &accepted = corpus().accepted;
        const auto &rejected = corpus().rejected;
        const auto plugin = e2e::loadInitializedPlugin();
        const auto &exports = plugin.exports();
        const FixtureFile missing{e2e::fixturePath("does-not-exist.pvdkit").string(), accepted.front().file.bytes};
        const auto report = measureLeaks("rejections", accepted.size(), iterations(), [&](const std::size_t i) {
            // Not our format, garbage, truncated: refused in both modes.
            for (const auto &fixture : rejected) {
                CHECK_FALSE(e2e::openImage(exports, fixture.file, OpenMode::Disk).has_value());
                CHECK_FALSE(e2e::openImage(exports, fixture.file, OpenMode::Memory).has_value());
            }
            // A valid file behind an empty or too short head, in both modes.
            const auto &valid = cycle(accepted, i).file;
            const auto *head = reinterpret_cast<const BYTE *>(valid.bytes.data());
            const auto size = static_cast<INT64>(valid.bytes.size());
            pvdInfoImage info{};
            void *context = nullptr;
            CHECK(exports.fileOpen(valid.utf8Path.c_str(), size, nullptr, 0, &info, &context) == FALSE);
            CHECK(exports.fileOpen(valid.utf8Path.c_str(), 0, nullptr, 0, &info, &context) == FALSE);
            CHECK(exports.fileOpen(valid.utf8Path.c_str(), size, head, 11, &info, &context) == FALSE);
            CHECK(exports.fileOpen(valid.utf8Path.c_str(), 0, head, 11, &info, &context) == FALSE);
            CHECK(context == nullptr);
            // A valid head whose file does not exist on disk.
            CHECK_FALSE(e2e::openImage(exports, missing, OpenMode::Disk).has_value());
            // Pages out of range on an open file.
            const auto image = open(exports, cycle(accepted, i), alternate(i));
            pvdInfoPage page{};
            CHECK(exports.pageInfo(image.context, image.info.nPages, &page) == FALSE);
            CHECK(exports.pageInfo(image.context, 0xFFFFFFFFU, &page) == FALSE);
            pvdInfoDecode decodeInfo{};
            CHECK(exports.pageDecode(image.context, image.info.nPages, &decodeInfo, nullptr, nullptr) == FALSE);
            CHECK(decodeInfo.pImage == nullptr);
            exports.fileClose(image.context);
        });
        requireNoLeak(report);
        exports.exit();
    }

    TEST_CASE("leak: two pages outstanding, freed in both orders")
    {
        const auto &fixtures = corpus().accepted;
        const auto plugin = e2e::loadInitializedPlugin();
        const auto &exports = plugin.exports();
        const auto report =
            measureLeaks("two pages outstanding", fixtures.size(), iterations(), [&](const std::size_t i) {
                const auto &fixture = cycle(fixtures, i);
                const auto image = open(exports, fixture, alternate(i));
                const std::uint32_t second = fixture.pages > 1 ? 1 : 0; // a still image: page 0 twice
                auto a = decode(exports, image.context, 0);
                auto b = decode(exports, image.context, second);
                CHECK(a.decode.pImage != b.decode.pImage);
                // Both orders, and each order in both host modes: the mode alternates with i, so
                // the order alternates with i / 2 (disk+in order, memory+in order, disk+reverse,
                // memory+reverse, ...).
                const bool inOrder = (i / 2) % 2 == 0;
                auto &first = inOrder ? a : b;
                auto &last = inOrder ? b : a;
                exports.pageFree(image.context, &first.decode);
                exports.pageFree(image.context, &last.decode);
                exports.fileClose(image.context);
            });
        requireNoLeak(report);
        exports.exit();
    }

    TEST_CASE("leak: N sessions open at once, then closed")
    {
        const auto &fixtures = corpus().accepted;
        const auto plugin = e2e::loadInitializedPlugin();
        const auto &exports = plugin.exports();
        const auto sessions = iterations();
        const auto report = measureLeaks("N sessions at once", 1, 1, [&](const std::size_t) {
            struct Session
            {
                OpenedImage image;
                std::vector<DecodedPage> pages;
            };
            std::vector<Session> live;
            live.reserve(sessions);
            for (std::size_t k = 0; k < sessions; ++k) {
                Session session{open(exports, cycle(fixtures, k), alternate(k)), {}};
                // Every 8th session also holds its first page; half of those are freed before
                // the close, the other half rely on the close.
                if (k % 8 == 0) {
                    session.pages.push_back(decode(exports, session.image.context, 0));
                    if (k % 16 == 0) {
                        freeAll(exports, session.image, session.pages);
                    }
                }
                live.push_back(std::move(session));
            }
            CHECK(live.size() == sessions);
            for (auto &session : live) {
                exports.fileClose(session.image.context);
            }
        });
        requireNoLeak(report);
        exports.exit();
    }

    TEST_CASE("leak: pvdInit / pvdExit cycled")
    {
        const auto plugin = loadPlugin();
        const auto &exports = plugin.exports();
        const auto report = measureLeaks("init/exit cycle", 1, iterations(), [&](const std::size_t) {
            CHECK(exports.init() == PVD_CURRENT_INTERFACE_VERSION);
            pvdInfoPlugin info{};
            exports.pluginInfo(&info);
            CHECK(info.pName != nullptr);
            exports.exit();
        });
        requireNoLeak(report);
    }

    TEST_CASE("leak: 8 threads decoding concurrently")
    {
        const auto &fixtures = corpus().accepted;
        const auto plugin = e2e::loadInitializedPlugin();
        const auto &exports = plugin.exports();
        const auto perThread = std::max<std::size_t>(1, iterations() / kThreads);
        const auto report = measureLeaks("8 threads x " + std::to_string(perThread), 1, 1, [&](const std::size_t) {
            std::latch start{kThreads};
            std::array<std::string, kThreads> failures;
            {
                std::vector<std::jthread> threads;
                threads.reserve(kThreads);
                for (int index = 0; index < kThreads; ++index) {
                    threads.emplace_back([&, index] {
                        start.arrive_and_wait();
                        auto &failure = failures[static_cast<std::size_t>(index)];
                        for (std::size_t k = 0; k < perThread && failure.empty(); ++k) {
                            const auto n = static_cast<std::size_t>(index) + k * kThreads;
                            failure = workerRoundTrip(exports, cycle(fixtures, n), alternate(n));
                        }
                    });
                }
            }
            for (const auto &failure : failures) {
                CHECK_MESSAGE(failure.empty(), failure);
            }
        });
        requireNoLeak(report);
        exports.exit();
    }

    TEST_CASE("leak: LoadLibrary / FreeLibrary cycled")
    {
        const auto &fixtures = corpus().accepted;
        const auto report = measureLeaks("LoadLibrary/FreeLibrary", 1, kLibraryCycles, [&](const std::size_t i) {
            const auto plugin = loadPlugin();
            const auto &exports = plugin.exports();
            REQUIRE(exports.init() == PVD_CURRENT_INTERFACE_VERSION);
            roundTrip(exports, cycle(fixtures, i), alternate(i), Free::BeforeClose);
            exports.exit();
        });
        // In coverage builds the DLL carries the LLVM profile runtime, which keeps one heap block (its
        // file-name pattern) and two handles per load and writes a profile on every unload; the DLL
        // cannot free what its runtime owns, so exactly that much is allowed there and nothing more.
        // Plain builds allow nothing (PVDKIT_COVERAGE comes from pvdkit_add_plugin_e2e_tests).
#if defined(PVDKIT_COVERAGE)
        const test::Allowance profileRuntime{static_cast<std::int64_t>(kLibraryCycles),
                                             static_cast<std::int64_t>(256 * kLibraryCycles),
                                             static_cast<std::int64_t>(2 * kLibraryCycles)};
        std::printf("[leak] LoadLibrary/FreeLibrary: coverage build, allowing the profile runtime's %zu blocks, "
                    "%zu bytes and %zu handles\n",
                    kLibraryCycles, 256 * kLibraryCycles, 2 * kLibraryCycles);
        requireNoLeak(report, test::kPrivateBytesTolerance, profileRuntime);
#else
        requireNoLeak(report);
#endif
    }

    TEST_CASE("leak: host-like browsing with a sliding window of open files")
    {
        const auto &fixtures = corpus().accepted;
        const auto plugin = e2e::loadInitializedPlugin();
        const auto &exports = plugin.exports();
        const auto passes = std::max<std::size_t>(1, iterations() / fixtures.size());
        std::size_t aborted = 0;
        std::size_t switched = 0;
        const auto report =
            measureLeaks("browsing " + std::to_string(passes) + " passes", 1, passes, [&](const std::size_t pass) {
                struct Open
                {
                    OpenedImage image;
                    std::vector<DecodedPage> pages;
                };
                std::deque<Open> window;
                const auto closeOldest = [&](const bool freeFirst) {
                    auto &oldest = window.front();
                    if (freeFirst) {
                        freeAll(exports, oldest.image, oldest.pages);
                    }
                    exports.fileClose(oldest.image.context);
                    window.pop_front();
                };
                for (std::size_t k = 0; k < fixtures.size(); ++k) {
                    const auto &fixture = fixtures[k];
                    // The next file is opened before the previous one is closed.
                    Open next{open(exports, fixture, alternate(k + pass)), {}};
                    if ((k + 1) % kAbortEvery == 0) {
                        std::uint32_t step = 1;
                        pvdInfoDecode info{};
                        CHECK(exports.pageDecode(next.image.context, 0, &info, abortAtStep, &step) == FALSE);
                        ++aborted;
                    } else {
                        next.pages.push_back(decode(exports, next.image.context, 0));
                        if (fixture.pages > 1) {
                            // Page switch: the next frame comes in, the previous one goes.
                            auto frame = decode(exports, next.image.context, 1);
                            freeAll(exports, next.image, next.pages);
                            next.pages.push_back(frame);
                            ++switched;
                        }
                    }
                    window.push_back(std::move(next));
                    if (window.size() > kWindow) {
                        closeOldest(k % 2 == 0);
                    }
                }
                while (!window.empty()) {
                    closeOldest(window.size() % 2 == 0);
                }
            });
        std::printf("[leak] browsing: %zu files per pass, %zu decodes aborted, %zu page switches (incl. warm-up)\n",
                    fixtures.size(), aborted, switched);
        requireNoLeak(report);
        exports.exit();
    }

    TEST_CASE("leak: hostile corpus - every rejected fixture and 200 mutants of the accepted ones")
    {
        const auto &accepted = corpus().accepted;
        std::vector<test::CorpusSource> sources;
        sources.reserve(accepted.size());
        for (const auto &fixture : accepted) {
            sources.push_back({std::filesystem::path{fixture.file.utf8Path}.filename().string(), fixture.file.bytes});
        }
        const test::HostileCorpus generated{sources, kMutants, kCorpusSeed};
        std::vector<FixtureFile> hostile;
        hostile.reserve(corpus().rejected.size() + generated.files().size());
        for (const auto &fixture : corpus().rejected) {
            hostile.push_back(fixture.file);
        }
        for (const auto &mutant : generated.files()) {
            hostile.push_back({mutant.utf8Path, mutant.bytes});
        }

        const auto plugin = e2e::loadInitializedPlugin();
        const auto &exports = plugin.exports();
        std::size_t opened = 0;
        std::size_t refused = 0;
        std::size_t pagesDecoded = 0;
        std::size_t pagesRefused = 0;
        // One full pass over the corpus as warm-up, like every other cycling scenario: each mutant
        // takes its own path through the parser, and a path first taken in the measured pass would
        // charge its lazy one-time allocations to the plugin.
        const auto report = measureLeaks("hostile corpus", hostile.size(), hostile.size(), [&](const std::size_t i) {
            const auto &file = hostile[i];
            CAPTURE(file.utf8Path);
            for (const auto mode : {OpenMode::Disk, OpenMode::Memory}) {
                const auto image = e2e::openImage(exports, file, mode);
                if (!image) {
                    ++refused;
                    continue;
                }
                ++opened;
                for (std::uint32_t page = 0; page < image->info.nPages; ++page) {
                    // FALSE or a valid image: decodePage REQUIREs the handed-out page to be usable,
                    // and the checksum reads every byte it claims.
                    auto decoded = e2e::decodePage(exports, image->context, page, nullptr, nullptr);
                    if (!decoded) {
                        ++pagesRefused;
                        continue;
                    }
                    static_cast<void>(checksum(*decoded));
                    ++pagesDecoded;
                    exports.pageFree(image->context, &decoded->decode);
                }
                exports.fileClose(image->context);
            }
        });
        std::printf("[leak] hostile corpus: %zu files (%zu rejected fixtures + %zu mutants) x 2 modes: %zu opens "
                    "refused, %zu accepted; %zu pages decoded, %zu page decodes refused (incl. warm-up)\n",
                    hostile.size(), corpus().rejected.size(), generated.files().size(), refused, opened, pagesDecoded,
                    pagesRefused);
        requireNoLeak(report);
        exports.exit();
    }

} // namespace pvdkit::leak
