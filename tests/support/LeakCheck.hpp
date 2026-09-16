#pragma once

// Process-wide resource accounting for the leak gates (test support only; never linked into a
// plugin). Four exact counters and one coarse one:
//
// 1. Busy blocks of the process heap (count and payload bytes). Why the heap is walked instead of
//    _CrtMemCheckpoint / _CrtMemDifference: every module built with the static CRT (/MT, /MTd)
//    carries its own CRT instance with its own debug-heap bookkeeping, so _CrtMemCheckpoint in a
//    test executable only ever sees that executable's allocations and never the plugin DLL's -
//    and the DLL is the thing under test. Since the UCRT (VS 2015) every CRT instance, static or
//    dynamic, debug or release, serves malloc / new / _aligned_malloc from the one process heap
//    (GetProcessHeap()), and so do libavif, dav1d and libyuv inside the DLL. Walking that heap
//    (HeapWalk under HeapLock) therefore counts the live blocks of every module in the process
//    with one mechanism in Debug and Release alike. Verified on this toolchain (clang-cl 19, UCRT
//    10.0.26100, x64 and x86): every malloc'd block shows up once, a freed block vanishes,
//    LoadLibrary / thread churn is zero after one warm-up round, and the low-fragmentation heap
//    activating a size class in the middle of a run does not change the busy count (its
//    containers are heap-internal, not busy user blocks).
// 2. Handles: GetProcessHandleCount.
// 3. Mapped views (count and bytes): the committed MEM_MAPPED regions of the address space,
//    enumerated with VirtualQuery. A view of a file mapping is neither a heap block nor a handle,
//    and a leaked one hardly moves the commit charge either (file-backed pages are shared, not
//    private), so without this counter a FileMapping that never unmapped would pass every other
//    check - proven with an injected no-op UnmapViewOfFile before this counter existed. Image
//    sections (MEM_IMAGE: the DLLs) and private memory (heap segments, stacks, ASan's shadow) are
//    not counted, so the view count is stable after warm-up, under ASan too; the bytes are not
//    under ASan (see below). Every region is recorded (base, size, the file behind it as
//    GetMappedFileNameW names it - K32GetMappedFileNameW of kernel32 under PSAPI_VERSION 2, an
//    import of the test executables only) so that a change is reported as the regions that
//    appeared and vanished, by name: the CRT and the OS map system sections lazily (an NLS
//    table on the first locale-aware call, the sorting tables on the first CompareString), once
//    per process, and a warm-up that did not reach that first call leaves it to the measured
//    pass - the name tells such a mapping from a FileMapping of the plugin's own.
// 4. The commit charge (PrivateUsage) is the coarse cross-check for whatever is none of the above
//    - thread stacks, VirtualAlloc - and coarse it is: measured on the AVIF plugin it wanders by
//    up to +-9 MiB between two quiescent snapshots with the process heap's own committed size
//    flat and every block accounted for, and the drift is not proportional to the iteration count
//    (41 browsing passes: -7 MiB; 125 passes: +40 KiB), i.e. kernel and heap-manager timing, not
//    growth. It is therefore gated with a wide tolerance (kPrivateBytesTolerance) and printed with
//    the heap's committed size next to it.
//
// A negative delta in an exact counter is not a leak: something warm-up allocated was released
// during the run (a lazy cache); the gate accepts <= 0 and reports the number.
//
// Under AddressSanitizer the picture changes for the heap: ASan intercepts HeapAlloc (every heap,
// every module) and serves it from its own allocator, and it quarantines freed memory, so heap
// blocks and private bytes say nothing there. Nor do the mapped bytes: the runtime keeps its own
// MEM_MAPPED regions and grows them in place while the tests run (views +0, bytes +24 KiB
// measured on the AVIF memory round trip), which is not the plugin's. The gate then checks
// handles and the mapped-view count only and reports the rest, the mapped bytes included;
// memory errors are ASan's own job in that preset.

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace pvdkit::test
{

    /// One busy block of the process heap as the walk saw it: where, how big, and its first bytes
    /// (so a block that shows up between two snapshots can be recognised in the log).
    struct HeapBlock
    {
        std::uintptr_t address = 0;
        std::uint32_t size = 0;
        std::array<std::byte, 16> head{};
    };

    /// One committed MEM_MAPPED region as the walk saw it: where, how big, and the file behind it
    /// (GetMappedFileNameW: an NT device path such as \Device\HarddiskVolume3\Windows\System32\
    /// locale.nls; empty for a pagefile-backed section), so a region that appears or vanishes
    /// between two snapshots can be recognised in the log. The name is UTF-8 in a fixed buffer
    /// (nothing may be allocated per record: the record buffers are reserved once, like the
    /// blocks'); a longer name keeps its tail behind "...", which is where the file name is.
    struct MappedRegion
    {
        std::uintptr_t base = 0;
        std::uint64_t size = 0;
        std::array<char, 200> name{}; // NUL-terminated
    };

    /// The mapped file's name as recorded; empty for a pagefile-backed section.
    [[nodiscard]] std::string_view name(const MappedRegion &region) noexcept;

    /// What the process holds right now.
    struct ResourceUsage
    {
        std::uint64_t heapBlocks = 0;    // busy blocks in the process heap
        std::uint64_t heapBytes = 0;     // their payload bytes (cbData)
        std::uint64_t handles = 0;       // GetProcessHandleCount
        std::uint64_t mappedViews = 0;   // committed MEM_MAPPED regions (VirtualQuery)
        std::uint64_t mappedBytes = 0;   // their size
        std::uint64_t privateBytes = 0;  // PROCESS_MEMORY_COUNTERS_EX::PrivateUsage
        std::uint64_t heapCommitted = 0; // bytes committed to the process heap's regions (part of privateBytes)
        /// Every busy block, sorted by address, in one of two static buffers the accounting owns:
        /// valid until the snapshot after next (before and after of one measurement never share).
        std::span<const HeapBlock> blocks;
        /// Every committed MEM_MAPPED region, sorted by base, with the same lifetime as `blocks`.
        std::span<const MappedRegion> regions;
    };

    struct ResourceDelta
    {
        std::int64_t heapBlocks = 0; // new minus vanished busy blocks, critical-section debug blocks excluded
        std::int64_t heapBytes = 0;  // likewise
        std::int64_t handles = 0;
        std::int64_t mappedViews = 0;
        std::int64_t mappedBytes = 0;
        std::int64_t privateBytes = 0;
        std::int64_t heapCommitted = 0;
        /// New RTL_CRITICAL_SECTION_DEBUG blocks (see the header comment): reported, not charged.
        std::int64_t csDebugBlocks = 0;
    };

    /// Snapshots the process. Call it only while no other thread of ours runs: the heap is locked
    /// for the walk, and a thread still winding down would make the numbers drift.
    [[nodiscard]] ResourceUsage snapshotResources();

    [[nodiscard]] ResourceDelta operator-(const ResourceUsage &after, const ResourceUsage &before) noexcept;

    /// The busy blocks present in `after` but not in `before` (by address and size), one line each:
    /// `  0x... <n> bytes: <first bytes in hex>`; empty when nothing appeared. This is what turns
    /// a "+1 block" into something a reader can recognise.
    [[nodiscard]] std::string describeNewBlocks(const ResourceUsage &after, const ResourceUsage &before);

    /// The committed MEM_MAPPED regions of `after` that are not in `before` and the other way
    /// round (by base and size), one line each: `  + 0x... <n> KiB <file>` for a region that
    /// appeared, `  - 0x... <n> KiB <file>` for one that vanished, `(pagefile-backed)` in place of
    /// the file when there is none; empty when the region set is the same. A region that grew in
    /// place is one line of each. This is what turns a "views +0 (+2712 KiB)" into a file name.
    [[nodiscard]] std::string describeRegionChanges(const ResourceUsage &after, const ResourceUsage &before);

    /// Whether this binary was built with -fsanitize=address (see the header comment).
    [[nodiscard]] bool underAddressSanitizer() noexcept;

    /// Whether a delta shows growth in something the gate counts exactly.
    [[nodiscard]] constexpr bool grows(const ResourceDelta &delta) noexcept
    {
        return delta.heapBlocks > 0 || delta.heapBytes > 0 || delta.handles > 0 || delta.mappedViews > 0 ||
               delta.mappedBytes > 0;
    }

    /// The growth a first measured pass may show and still be given a second, deciding pass. What
    /// this is for: (1) a critical section deleted right after its first contended acquisition
    /// within a measured pass leaves ntdll's zeroed, free-listed RTL_CRITICAL_SECTION_DEBUG block
    /// (LeakCheck.cpp), which the recognition cannot claim once the section is gone - at most one
    /// small block per such section, once, never again for the same section; (2) the per-thread
    /// data of a worker thread the plugin itself started and joined (the presentation's row
    /// bands for a picture of at least 256 Ki pixels, docs/ARCHITECTURE.md section 3.7), which
    /// the CRT and the OS release on their own schedule, several passes after the thread exited.
    /// Measured on the EXR plugin in Task 27 (Debug and coverage builds, both architectures; a
    /// scratch host reading the debug-CRT block headers named the blocks): the vcruntime
    /// per-thread data, a _CRT_BLOCK of 128 bytes from vcruntime's per_thread_data.cpp:128 (180
    /// bytes under the debug header), the UCRT __acrt_ptd, a _CRT_BLOCK of 968 bytes from the
    /// UCRT's per_thread_data.cpp:245 (1020 bytes), the CRT's small per-thread blocks (68 x 6,
    /// 880) and the OS's per-thread FLS/TLS/activation-context blocks holding ntdll and
    /// kernelbase pointers (80, 360, 912, 24, 24, 136, 136 or 728): 11 to 16 blocks, 3200 to
    /// 3888 bytes on x64 (15 blocks, 2188 bytes on x86), appearing about once in twenty passes
    /// of banded decodes under load, released as a whole several passes later (a HeapCompact, a
    /// wait, a further thread or a pvdExit/pvdInit cycle do not release it earlier); with the
    /// band workers disabled every scenario measures +0 in 3 of 3 runs. Sixteen blocks and
    /// 4 KiB cover that set with a little room. What the bound still refuses: anything larger
    /// in the first pass (a cache that filled after the warm-up, +1 block of 64 KiB in the
    /// self-test) and any handle; and whatever the first pass showed, the second pass decides,
    /// so a growth that recurs - a real leak, or this residue left at the end of two consecutive
    /// passes (seen once in six x64 coverage runs: first pass +15, second +16) - fails as the
    /// rule intends. Mapped regions are not bounded here: a region that appeared in the first
    /// pass and does not appear again in the second was mapped once (a system section the CRT
    /// or the OS maps on first use: an NLS table, the sorting tables - a one-time mapping, not a
    /// leak), and a leaked view recurs, so the second pass charges it again; either way the
    /// second pass decides and both passes list their regions by name.
    struct RetryNoise
    {
        std::int64_t heapBlocks = 16;
        std::int64_t heapBytes = 4096;
        std::int64_t handles = 0;
    };

    inline constexpr RetryNoise kRetryNoise{};

    [[nodiscard]] constexpr bool withinRetryNoise(const ResourceDelta &delta,
                                                  const RetryNoise &noise = kRetryNoise) noexcept
    {
        return delta.heapBlocks <= noise.heapBlocks && delta.heapBytes <= noise.heapBytes &&
               delta.handles <= noise.handles;
    }

    /// The outcome of one measured scenario.
    struct LeakReport
    {
        std::string scenario;
        std::size_t warmUpIterations = 0;
        std::size_t iterations = 0;
        ResourceDelta delta; // the deciding pass: the second when the first grew within noise, else the first
        std::chrono::milliseconds warmUp{0};
        std::chrono::milliseconds measured{0}; // both passes when a second one ran
        bool secondPassRan = false;            // the first pass grew (by any amount): a second pass was measured
        bool firstPassWithinNoise = false;     // ... and it was small enough for the second to decide
        ResourceDelta firstPass;               // meaningful when secondPassRan
        ResourceDelta secondPass;              // meaningful when secondPassRan
        std::string newBlocks;                 // describeNewBlocks of the deciding pass (when any block appeared)
        std::string firstPassNewBlocks;        // the same for the first pass, when a second one ran
        std::string secondPassNewBlocks;       // the same for the second pass, when it ran
        std::string regionChanges;             // describeRegionChanges of the deciding pass
        std::string firstPassRegionChanges;    // the same for the first pass, when a second one ran
        std::string secondPassRegionChanges;   // the same for the second pass, when it ran
    };

    /// Runs `body(i)` for i in [0, warmUpIterations) as warm-up - lazy one-time allocations of the
    /// libraries, the loader and the CRT are not leaks - then snapshots, runs `body(i)` for i in
    /// [0, iterations), snapshots again and reports the difference. If that pass shows growth, a
    /// second measured pass runs from a fresh snapshot and both are reported; the second one
    /// decides only when the first grew by no more than kRetryNoise (a leak recurs, a one-off
    /// allocation does not), otherwise the first pass stands and the gate fails on it - a cache
    /// that fills after the warm-up is a finding, not noise; mapped regions never hold the second
    /// pass back (RetryNoise). A body that cycles through fixtures by `i` should warm up with one
    /// full cycle so every code path has run once.
    template <class Body>
    [[nodiscard]] LeakReport measureLeaks(const std::string_view scenario, const std::size_t warmUpIterations,
                                          const std::size_t iterations, Body &&body)
    {
        using Clock = std::chrono::steady_clock;
        LeakReport report;
        report.scenario = std::string{scenario};
        report.warmUpIterations = warmUpIterations;
        report.iterations = iterations;

        const auto warmUpStart = Clock::now();
        for (std::size_t iteration = 0; iteration < warmUpIterations; ++iteration) {
            body(iteration);
        }
        report.warmUp = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - warmUpStart);

        // One measured pass: the delta, which blocks appeared (whatever the net delta: a block
        // that appeared while others vanished is still worth a look) and which regions changed.
        const auto measure = [&](ResourceDelta &delta, std::string &appeared, std::string &regions) {
            const auto before = snapshotResources();
            const auto start = Clock::now();
            for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
                body(iteration);
            }
            report.measured += std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start);
            const auto after = snapshotResources();
            delta = after - before;
            appeared = describeNewBlocks(after, before);
            regions = describeRegionChanges(after, before);
        };
        measure(report.firstPass, report.firstPassNewBlocks, report.firstPassRegionChanges);
        report.delta = report.firstPass;
        report.newBlocks = report.firstPassNewBlocks;
        report.regionChanges = report.firstPassRegionChanges;
        if (grows(report.firstPass)) {
            report.secondPassRan = true;
            report.firstPassWithinNoise = withinRetryNoise(report.firstPass);
            measure(report.secondPass, report.secondPassNewBlocks, report.secondPassRegionChanges);
            if (report.firstPassWithinNoise) {
                report.delta = report.secondPass;
                report.newBlocks = report.secondPassNewBlocks;
                report.regionChanges = report.secondPassRegionChanges;
            }
        }
        return report;
    }

    /// One line per report on stdout: `[leak] <scenario>: heap blocks <+n>, heap bytes <+n>, handles
    /// <+n>, views <+n> (<+n KiB>), private <+n KiB> (heap committed <+n KiB>) (warm-up <n> x <ms>,
    /// measured <n> x <ms>)`, followed by `[asan]` under ASan and, when a second pass ran, by both
    /// passes' exact deltas and which one decided.
    [[nodiscard]] std::string formatReport(const LeakReport &report);

    /// The lists under that line, for every pass whose counters moved at all (so a net-negative
    /// pass that still gained a block shows it): `[leak]   blocks that appeared in the <pass>:`
    /// with describeNewBlocks when the heap counters moved, `[leak]   mapped regions that changed
    /// in the <pass> (+ appeared, - vanished):` with describeRegionChanges when the view counters
    /// did; empty when nothing moved.
    [[nodiscard]] std::string formatReportLists(const LeakReport &report);

    /// formatReport and formatReportLists on stdout.
    void printReport(const LeakReport &report);

    /// The failed gate's follow-up, off the fast path (one snapshot pair per item, each waiting
    /// for two agreeing readings): runs `body(i)` once for every i in [0, count) with a snapshot
    /// around each and returns one line per item after which busy blocks stayed - `[leak]   after
    /// <label(i)>: heap blocks +n, heap bytes +n, handles +n, cs-debug +n` followed by the retained
    /// blocks as describeNewBlocks lists them - so a log names the culprit; or one line saying
    /// that no item retained anything (the growth did not recur item by item: a one-time
    /// allocation of the failed pass, not any item's). Call it after the aggregate check failed
    /// on the heap counters, and not under ASan, where the walk sees no malloc'd block.
    [[nodiscard]] std::string localiseHeapGrowth(std::size_t count, const std::function<void(std::size_t)> &body,
                                                 const std::function<std::string(std::size_t)> &label);

    /// Private-bytes slack (see the header comment for the measured noise): wide enough never to
    /// trip on the commit charge's own drift, narrow enough that a leaked thread stack or
    /// VirtualAlloc per iteration still exceeds it within the N = 200 iterations of a scenario.
    inline constexpr std::int64_t kPrivateBytesTolerance = 32 * 1024 * 1024;

    /// What a scenario may keep per run for reasons outside the plugin. The one user is the
    /// LoadLibrary/FreeLibrary scenario in coverage builds: the LLVM profile runtime linked into the
    /// instrumented DLL keeps one heap block (its file-name pattern) and two handles per load, and
    /// the DLL cannot free what its runtime owns. Everything else runs with the default of zero.
    struct Allowance
    {
        std::int64_t heapBlocks = 0;
        std::int64_t heapBytes = 0;
        std::int64_t handles = 0;
    };

    /// The gate: no growth in heap blocks, heap bytes, handles, mapped views and mapped bytes
    /// beyond `allowance` (normally zero; a negative delta is a release, not a leak), private
    /// bytes within `privateBytesTolerance`. Under ASan only handles and the mapped-view count
    /// are gated; heap blocks, heap bytes, private bytes and mapped bytes are printed only, the
    /// last because ASan's runtime grows its own mapped regions in place (see the header comment).
    /// Prints the report first so the numbers are in the log either way. Uses doctest CHECKs.
    void requireNoLeak(const LeakReport &report, std::int64_t privateBytesTolerance = kPrivateBytesTolerance,
                       Allowance allowance = {});

    /// The checking half of requireNoLeak alone, for a scenario that prints the report itself
    /// (printReport) and something of its own - localiseHeapGrowth's lines - in between.
    void checkNoLeak(const LeakReport &report, std::int64_t privateBytesTolerance = kPrivateBytesTolerance,
                     Allowance allowance = {});

    /// Whether the gate would fail `report` on the heap counters (`allowance` as for checkNoLeak):
    /// when to spend on localiseHeapGrowth. Never under ASan (heap counters are not gated there).
    [[nodiscard]] bool heapGrew(const LeakReport &report, Allowance allowance = {}) noexcept;

    /// The iteration count the leak scenarios use: PVDKIT_LEAK_ITERATIONS if set to a positive
    /// number, else `defaultIterations`. A soak run is `PVDKIT_LEAK_ITERATIONS=5000 ctest ...`.
    [[nodiscard]] std::size_t leakIterations(std::size_t defaultIterations);

} // namespace pvdkit::test
