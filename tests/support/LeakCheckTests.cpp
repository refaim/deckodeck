// Self-tests of the leak accounting: a deliberately leaked heap block, handle and mapped view
// must show up in the deltas exactly, warm-up allocations must not, growth beyond noise must not
// be retried away, and the gate's iteration knob must read the environment. These are the "red"
// half of the leak gate's TDD: the scenarios in LeakScenarios.cpp only ever assert zero, so this
// file proves that zero is not what the accounting answers regardless.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <doctest/doctest.h>

#include "LeakCheck.hpp"

namespace
{

    using pvdkit::test::LeakReport;
    using pvdkit::test::measureLeaks;
    using pvdkit::test::ResourceDelta;
    using pvdkit::test::snapshotResources;
    using pvdkit::test::underAddressSanitizer;

    // Under ASan the process heap is not where malloc goes (LeakCheck.hpp), so a leaked block is
    // invisible to the walk there; the handle and view accounting is exact everywhere.
    std::int64_t expectedBlocks(const std::int64_t blocks)
    {
        return underAddressSanitizer() ? 0 : blocks;
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

    UniqueHandle createEvent()
    {
        UniqueHandle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
        REQUIRE(event);
        return event;
    }

    struct ViewDeleter
    {
        void operator()(void *view) const noexcept
        {
            static_cast<void>(UnmapViewOfFile(view));
        }
    };

    using UniqueView = std::unique_ptr<void, ViewDeleter>;

    constexpr DWORD kViewBytes = 1024 * 1024;

    // A 1 MiB view of a pagefile-backed section; the section handle is closed at once, so only the
    // view remains - exactly what a FileMapping that forgot UnmapViewOfFile would leave behind.
    UniqueView mapView()
    {
        const UniqueHandle section{
            CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, kViewBytes, nullptr)};
        REQUIRE(section);
        UniqueView view{MapViewOfFile(section.get(), FILE_MAP_ALL_ACCESS, 0, 0, 0)};
        REQUIRE(view);
        return view;
    }

    // Whether the debug CRT (/MTd) is in: it wraps every block in a bookkeeping header of its own.
    constexpr bool debugCrt()
    {
#if defined(_DEBUG)
        return true;
#else
        return false;
#endif
    }

    // A block the optimiser cannot elide: its address escapes into a volatile sink.
    volatile const void *sink = nullptr;

    std::unique_ptr<std::byte[]> allocateBlock(const std::size_t size)
    {
        auto block = std::make_unique<std::byte[]>(size);
        sink = block.get();
        return block;
    }

    bool exactCountersZero(const ResourceDelta &delta)
    {
        return delta.heapBlocks == 0 && delta.heapBytes == 0 && delta.handles == 0 && delta.mappedViews == 0 &&
               delta.mappedBytes == 0;
    }

    // A critical section contended once from a second thread: ntdll then allocates its
    // RTL_CRITICAL_SECTION_DEBUG - from a 64-entry static pool while that lasts, from the process
    // heap afterwards - and keeps it until DeleteCriticalSection.
    class ContendedSection
    {
      public:
        ContendedSection()
        {
            InitializeCriticalSection(&section_);
            contend();
        }

        ~ContendedSection()
        {
            DeleteCriticalSection(&section_);
        }

        ContendedSection(const ContendedSection &) = delete;
        ContendedSection &operator=(const ContendedSection &) = delete;

        [[nodiscard]] const CRITICAL_SECTION &section() const noexcept
        {
            return section_;
        }

      private:
        // Thread A holds the section until told to let go; thread B blocks on it meanwhile.
        void contend()
        {
            const UniqueHandle held{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
            const UniqueHandle release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
            REQUIRE((held && release));
            std::jthread holder{[&] {
                EnterCriticalSection(&section_);
                SetEvent(held.get());
                WaitForSingleObject(release.get(), INFINITE);
                LeaveCriticalSection(&section_);
            }};
            WaitForSingleObject(held.get(), INFINITE);
            std::jthread waiter{[&] {
                EnterCriticalSection(&section_);
                LeaveCriticalSection(&section_);
            }};
            Sleep(20); // long enough for the waiter to block, which is what allocates the debug block
            SetEvent(release.get());
        }

        CRITICAL_SECTION section_{};
    };

    // Whether a section's DebugInfo lives in a busy block of the process heap (as opposed to
    // ntdll's static pool, or nowhere: -1 before the first contention).
    bool debugInfoOnHeap(const CRITICAL_SECTION &section)
    {
        const auto *info = section.DebugInfo;
        // ntdll leaves DebugInfo at (PRTL_CRITICAL_SECTION_DEBUG)-1 until the first contention.
        if (info == nullptr || reinterpret_cast<std::uintptr_t>(info) == static_cast<std::uintptr_t>(-1)) {
            return false;
        }
        return HeapValidate(GetProcessHeap(), 0, info) != FALSE;
    }

    struct HeapBlockDeleter
    {
        void operator()(void *block) const noexcept
        {
            static_cast<void>(HeapFree(GetProcessHeap(), 0, block));
        }
    };

    using UniqueHeapBlock = std::unique_ptr<void, HeapBlockDeleter>;

    // A raw process-heap block of exactly the size of an RTL_CRITICAL_SECTION_DEBUG, zeroed. Raw
    // HeapAlloc blocks land in the real process heap even under ASan (only malloc/new go to its
    // allocator), so the counts below are exact in every preset.
    UniqueHeapBlock rawBlock()
    {
        UniqueHeapBlock block{HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(RTL_CRITICAL_SECTION_DEBUG))};
        REQUIRE(block);
        return block;
    }

} // namespace

TEST_CASE("a leaked heap block shows up in the snapshot delta once, with at least its payload bytes")
{
    // doctest grows its context stack on the first CAPTURE of the process; that is not this test's
    // business, so it happens before the first snapshot.
    {
        const int warmUp = 0;
        CAPTURE(warmUp);
    }
    const auto before = snapshotResources();
    auto leaked = allocateBlock(4096);
    for (std::size_t index = 0; index < 16; ++index) {
        leaked[index] = static_cast<std::byte>(0xA0 + index);
    }
    const auto afterLeak = snapshotResources();
    const auto during = afterLeak - before;
    CHECK(during.heapBlocks == expectedBlocks(1));
    if (!underAddressSanitizer()) {
        CHECK(during.heapBytes >= 4096);
        // The description names the block: its size and its first bytes. Everything this block
        // of checks allocates dies with it, before the "after" snapshot below.
        const auto appeared = pvdkit::test::describeNewBlocks(afterLeak, before);
        CAPTURE(appeared);
        CHECK(std::ranges::count(appeared, '\n') == 1);
        CHECK(appeared.find(" bytes: ") != std::string::npos);
        // The debug CRT wraps every block in its own header: the walk then sees 4096 plus that
        // header, and the first bytes are the header's. Only the plain CRT shows the payload.
        CHECK((appeared.find(" 4096 bytes: a0 a1 a2 a3 a4 a5 a6 a7 a8 a9 aa ab ac ad ae af") != std::string::npos) ==
              !debugCrt());
        CHECK(pvdkit::test::describeNewBlocks(before, afterLeak).empty());
    }
    CHECK(during.handles == 0);
    CHECK(during.mappedViews == 0);

    leaked.reset();
    const auto after = snapshotResources() - before;
    CHECK(exactCountersZero(after));
}

TEST_CASE("a leaked handle shows up in the snapshot delta exactly")
{
    const auto before = snapshotResources();
    auto leaked = createEvent();
    const auto during = snapshotResources() - before;
    CHECK(during.handles == 1);
    CHECK(during.heapBlocks == 0);
    CHECK(during.mappedViews == 0);

    leaked.reset();
    CHECK(exactCountersZero(snapshotResources() - before));
}

TEST_CASE("a leaked mapped view is charged exactly, even with its section handle closed")
{
    const auto before = snapshotResources();
    auto leaked = mapView();
    const auto during = snapshotResources() - before;
    CHECK(during.mappedViews == 1);
    CHECK(during.mappedBytes == kViewBytes);
    CHECK(during.handles == 0);
    CHECK(during.heapBlocks == 0);

    // Three more: still one region per view. Everything this block allocates (the vector's
    // buffer, the report line) dies with it, before the final snapshot.
    {
        std::vector<UniqueView> more;
        more.reserve(3);
        for (int index = 0; index < 3; ++index) {
            more.push_back(mapView());
        }
        const auto four = snapshotResources() - before;
        CHECK(four.mappedViews == 4);
        CHECK(four.mappedBytes == 4 * kViewBytes);

        // The report names the views on their own, before any other counter moves.
        LeakReport report;
        report.scenario = "view-leaking body";
        report.delta = four;
        const auto line = pvdkit::test::formatReport(report);
        CAPTURE(line);
        CHECK(line.find("views +4 (+4096 KiB)") != std::string::npos);
    }
    leaked.reset();
    CHECK(exactCountersZero(snapshotResources() - before));
}

TEST_CASE("a critical section's debug block is recognised and reported, not charged")
{
    // Exhaust ntdll's static pool first so the section under test gets a heap block: up to 80
    // contended sections that stay alive across the measurement (their blocks exist in both
    // snapshots). Nothing is deleted before the measurement: ntdll keeps a deleted section's block
    // for reuse, and the section under test must allocate a new one. The sections themselves are
    // stack objects so that only ntdll's block appears.
    std::vector<std::unique_ptr<ContendedSection>> pool;
    pool.reserve(80);
    while (pool.size() < 80 && (pool.empty() || !debugInfoOnHeap(pool.back()->section()))) {
        pool.push_back(std::make_unique<ContendedSection>());
    }
    REQUIRE(debugInfoOnHeap(pool.back()->section()));

    const auto before = snapshotResources();
    std::optional<ContendedSection> fresh;
    fresh.emplace();
    REQUIRE(debugInfoOnHeap(fresh->section()));
    const auto after = snapshotResources();
    const auto during = after - before;
    // Exactly one block, recognised - under ASan too: ntdll allocates it with its own
    // RtlAllocateHeap, which lands in the real process heap like every raw HeapAlloc.
    CHECK(during.csDebugBlocks == 1);
    CHECK(during.heapBlocks == 0);
    CHECK(during.heapBytes == 0);
    CHECK(during.handles == 0);
    if (!underAddressSanitizer()) {
        const auto appeared = pvdkit::test::describeNewBlocks(after, before);
        CAPTURE(appeared);
        CHECK(std::ranges::count(appeared, '\n') == 1);
        CHECK(appeared.find(" [RTL_CRITICAL_SECTION_DEBUG of a live critical section: Windows', not charged]") !=
              std::string::npos);
    }

    // DeleteCriticalSection does not free the block: ntdll zeroes it and keeps it for the next
    // section (measured), so from here on it is an ordinary busy block to the accounting - the
    // documented one-off that the retry rule absorbs - and the next contended section reuses it
    // instead of allocating.
    fresh.reset();
    const auto afterDelete = snapshotResources();
    const auto orphaned = afterDelete - before;
    CHECK(orphaned.heapBlocks == 1);
    CHECK(orphaned.heapBytes == sizeof(RTL_CRITICAL_SECTION_DEBUG));
    CHECK(orphaned.csDebugBlocks == 0);
    CHECK(orphaned.handles == 0);

    fresh.emplace();
    REQUIRE(debugInfoOnHeap(fresh->section()));
    const auto reused = snapshotResources() - afterDelete;
    CHECK(exactCountersZero(reused));
    CHECK(reused.csDebugBlocks == 0);
    fresh.reset();
}

TEST_CASE("an ordinary block of the same size as a critical section's debug block is charged")
{
    const auto before = snapshotResources();
    auto zeroed = rawBlock(); // Type 0 and a null CriticalSection pointer
    const auto one = snapshotResources() - before;
    CHECK(one.heapBlocks == 1);
    CHECK(one.csDebugBlocks == 0);

    // The same shape with a CriticalSection pointer that leads somewhere readable whose first
    // word is not this block's address: still an ordinary block.
    auto decoy = rawBlock();
    auto *target = static_cast<std::byte *>(decoy.get());
    std::memcpy(target + offsetof(RTL_CRITICAL_SECTION_DEBUG, CriticalSection), static_cast<const void *>(&target),
                sizeof(target));
    const auto two = snapshotResources() - before;
    CHECK(two.heapBlocks == 2);
    CHECK(two.csDebugBlocks == 0);

    decoy.reset();
    zeroed.reset();
    CHECK(exactCountersZero(snapshotResources() - before));
}

TEST_CASE("measureLeaks charges the measured iterations only and reports the counts it ran")
{
    std::vector<std::unique_ptr<std::byte[]>> retained;
    std::vector<UniqueHandle> events;
    std::vector<UniqueView> views;

    SUBCASE("warm-up allocations that stay alive are not a leak; a clean body reports zero")
    {
        const auto report = measureLeaks("clean body", 3, 50, [&](const std::size_t) {
            if (retained.size() < 3) {
                retained.push_back(allocateBlock(1024)); // "lazy one-time allocation" during warm-up
            }
            auto transient = allocateBlock(777);
            auto event = createEvent();
            auto view = mapView();
        });
        CHECK(report.scenario == "clean body");
        CHECK(report.warmUpIterations == 3);
        CHECK(report.iterations == 50);
        CHECK(exactCountersZero(report.delta));
        CHECK_FALSE(report.secondPassRan);
        CHECK(retained.size() == 3);
        pvdkit::test::requireNoLeak(report);
    }

    SUBCASE("a body that leaks one block per iteration is charged one block per measured iteration")
    {
        retained.reserve(64);
        const auto report =
            measureLeaks("leaking body", 1, 5, [&](const std::size_t) { retained.push_back(allocateBlock(2048)); });
        CHECK(report.delta.heapBlocks == expectedBlocks(5));
        if (!underAddressSanitizer()) {
            CHECK(report.delta.heapBytes >= 5 * 2048);
            // Five blocks is beyond noise: the first pass decides, the second is reported.
            CHECK(report.secondPassRan);
            CHECK_FALSE(report.firstPassWithinNoise);
            CHECK(report.firstPass.heapBlocks == 5);
            CHECK(report.secondPass.heapBlocks == 5);
            CHECK(std::ranges::count(report.newBlocks, '\n') == 5);
            CHECK(std::ranges::count(report.firstPassNewBlocks, '\n') == 5);
            CHECK(std::ranges::count(report.secondPassNewBlocks, '\n') == 5);
            CHECK((report.newBlocks.find(" 2048 bytes: ") != std::string::npos) == !debugCrt());
            CHECK(report.newBlocks.find(" bytes: ") != std::string::npos);
        }
        CHECK(report.delta.handles == 0);
    }

    SUBCASE("a body that leaks one handle per iteration is charged one handle per measured iteration")
    {
        events.reserve(64);
        const auto report =
            measureLeaks("handle-leaking body", 2, 7, [&](const std::size_t) { events.push_back(createEvent()); });
        CHECK(report.delta.handles == 7);
        CHECK(report.delta.heapBlocks == 0);
        CHECK(report.secondPassRan);
        CHECK_FALSE(report.firstPassWithinNoise);
    }

    SUBCASE("a body that leaks one view per iteration is charged one view per measured iteration")
    {
        views.reserve(64);
        const auto report =
            measureLeaks("view-leaking body", 2, 6, [&](const std::size_t) { views.push_back(mapView()); });
        CHECK(report.delta.mappedViews == 6);
        CHECK(report.delta.mappedBytes == 6 * kViewBytes);
        CHECK(report.delta.handles == 0);
        CHECK(report.secondPassRan);
        CHECK_FALSE(report.firstPassWithinNoise);
        CHECK(report.secondPass.mappedViews == 6);
    }

    SUBCASE("a one-off allocation within noise is decided by a second pass; anything larger is not")
    {
        retained.reserve(64);
        std::size_t calls = 0;
        const auto lazy = measureLeaks("lazy body", 2, 5, [&](const std::size_t) {
            // Allocates exactly once, on the first measured call: warm-up did not see it.
            if (++calls == 3) {
                retained.push_back(allocateBlock(32));
            }
        });
        CHECK(lazy.secondPassRan == !underAddressSanitizer());
        CHECK(lazy.firstPassWithinNoise == !underAddressSanitizer());
        CHECK(lazy.firstPass.heapBlocks == expectedBlocks(1));
        CHECK(exactCountersZero(lazy.delta));
        CHECK(calls == (lazy.secondPassRan ? 12 : 7)); // 2 warm-up + 5 measured (+ 5 for the second pass)
        if (!underAddressSanitizer()) {
            const auto line = pvdkit::test::formatReport(lazy);
            CAPTURE(line);
            CHECK(line.find("[first pass: heap blocks +1, heap bytes +") != std::string::npos);
            CHECK(line.find("the first pass grew within noise, the second decided]") != std::string::npos);
            pvdkit::test::requireNoLeak(lazy);
        }

        // One block of 64 KiB, once: a cache that filled after the warm-up. Beyond the byte noise,
        // so the first pass stands even though the second is flat.
        calls = 0;
        const auto cache = measureLeaks("cache body", 2, 5, [&](const std::size_t) {
            if (++calls == 3) {
                retained.push_back(allocateBlock(64 * 1024));
            }
        });
        CHECK(cache.secondPassRan == !underAddressSanitizer());
        CHECK_FALSE(cache.firstPassWithinNoise);
        CHECK(cache.delta.heapBlocks == expectedBlocks(1));
        if (!underAddressSanitizer()) {
            CHECK(cache.delta.heapBytes >= 64 * 1024);
            CHECK(cache.secondPass.heapBlocks == 0);
            const auto line = pvdkit::test::formatReport(cache);
            CAPTURE(line);
            CHECK(line.find("the first pass grew beyond noise and decides]") != std::string::npos);
        }

        // One handle, once: handles have no noise allowance at all.
        calls = 0;
        events.reserve(64);
        const auto handle = measureLeaks("one-off handle body", 1, 3, [&](const std::size_t) {
            if (++calls == 2) {
                events.push_back(createEvent());
            }
        });
        CHECK(handle.secondPassRan);
        CHECK_FALSE(handle.firstPassWithinNoise);
        CHECK(handle.delta.handles == 1);
        CHECK(handle.secondPass.handles == 0);

        // One view, once: neither have views.
        calls = 0;
        views.reserve(64);
        const auto view = measureLeaks("one-off view body", 1, 3, [&](const std::size_t) {
            if (++calls == 2) {
                views.push_back(mapView());
            }
        });
        CHECK(view.secondPassRan);
        CHECK_FALSE(view.firstPassWithinNoise);
        CHECK(view.delta.mappedViews == 1);
        CHECK(view.secondPass.mappedViews == 0);
    }
}

TEST_CASE("the retry noise bound is two small blocks and nothing else")
{
    using pvdkit::test::withinRetryNoise;
    CHECK(withinRetryNoise({0, 0, 0, 0, 0, 0, 0}));
    CHECK(withinRetryNoise({2, 1024, 0, 0, 0, 0, 0}));
    CHECK(withinRetryNoise({-5, -4096, -1, -1, -4096, 0, 0}));
    CHECK_FALSE(withinRetryNoise({3, 96, 0, 0, 0, 0, 0}));
    CHECK_FALSE(withinRetryNoise({1, 1025, 0, 0, 0, 0, 0}));
    CHECK_FALSE(withinRetryNoise({0, 0, 1, 0, 0, 0, 0}));
    CHECK_FALSE(withinRetryNoise({0, 0, 0, 1, 4096, 0, 0}));
    CHECK_FALSE(withinRetryNoise({0, 0, 0, 0, 4096, 0, 0}));
    // Private bytes and the heap's committed size are not exact counters and never block a retry.
    CHECK(withinRetryNoise({0, 0, 0, 0, 0, 64 * 1024 * 1024, 64 * 1024 * 1024}));
}

TEST_CASE("the report line carries every number a reader needs")
{
    LeakReport report;
    report.scenario = "disk round trip";
    report.warmUpIterations = 24;
    report.iterations = 200;
    report.delta = {3, -4096, 1, 2, 8192, 2 * 1024 * 1024, 512 * 1024, 5};
    report.warmUp = std::chrono::milliseconds{12};
    report.measured = std::chrono::milliseconds{345};
    const auto line = pvdkit::test::formatReport(report);
    CAPTURE(line);
    CHECK(line.find("[leak] disk round trip:") == 0);
    CHECK(line.find("heap blocks +3") != std::string::npos);
    CHECK(line.find("heap bytes -4096") != std::string::npos);
    CHECK(line.find("handles +1") != std::string::npos);
    CHECK(line.find("views +2 (+8 KiB)") != std::string::npos);
    CHECK(line.find("cs-debug +5") != std::string::npos);
    CHECK(line.find("private +2048 KiB") != std::string::npos);
    CHECK(line.find("heap committed +512 KiB") != std::string::npos);
    CHECK(line.find("warm-up 24 x 12 ms") != std::string::npos);
    CHECK(line.find("measured 200 x 345 ms") != std::string::npos);
    CHECK((line.find("[asan]") != std::string::npos) == underAddressSanitizer());
    CHECK(line.find("[first pass") == std::string::npos);

    report.secondPassRan = true;
    report.firstPassWithinNoise = true;
    report.firstPass = {1, 32, 0, 0, 0, 0, 0, 1};
    report.secondPass = {0, 0, 0, 0, 0, 0, 0, 0};
    const auto decided = pvdkit::test::formatReport(report);
    CAPTURE(decided);
    CHECK(decided.find("[first pass: heap blocks +1, heap bytes +32, handles +0, views +0 (+0 KiB), cs-debug +1; "
                       "second pass: heap blocks +0, heap bytes +0, handles +0, views +0 (+0 KiB), cs-debug +0; the "
                       "first pass grew within noise, the second decided]") != std::string::npos);

    report.firstPassWithinNoise = false;
    report.firstPass = {10000, 640000, 0, 0, 0, 0, 0, 0};
    const auto stands = pvdkit::test::formatReport(report);
    CAPTURE(stands);
    CHECK(stands.find("[first pass: heap blocks +10000, heap bytes +640000, handles +0, views +0 (+0 KiB), cs-debug "
                      "+0; second pass: heap blocks +0, heap bytes +0, handles +0, views +0 (+0 KiB), cs-debug +0; the "
                      "first pass grew beyond noise and decides]") != std::string::npos);
}

TEST_CASE("PVDKIT_LEAK_ITERATIONS overrides the default iteration count when it is a positive number")
{
    const auto setVariable = [](const char *value) {
        // A null value deletes the variable, which fails when it was not set to begin with.
        const BOOL set = SetEnvironmentVariableA("PVDKIT_LEAK_ITERATIONS", value);
        REQUIRE((set != FALSE || value == nullptr));
    };
    setVariable(nullptr);
    CHECK(pvdkit::test::leakIterations(200) == 200);
    setVariable("37");
    CHECK(pvdkit::test::leakIterations(200) == 37);
    setVariable("5000");
    CHECK(pvdkit::test::leakIterations(200) == 5000);
    setVariable("0");
    CHECK(pvdkit::test::leakIterations(200) == 200);
    setVariable("abc");
    CHECK(pvdkit::test::leakIterations(200) == 200);
    setVariable("12x");
    CHECK(pvdkit::test::leakIterations(200) == 200);
    setVariable("");
    CHECK(pvdkit::test::leakIterations(200) == 200);
    setVariable("123456789012345678901234567890123");
    CHECK(pvdkit::test::leakIterations(200) == 200);
    setVariable(nullptr);
}
