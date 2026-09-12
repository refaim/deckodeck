// The hostile corpus generator: deterministic for a seed, every mutant differs from its source,
// every mutation strategy is used, the files land on disk with the bytes reported and vanish with
// the corpus object.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>

#include <random>
#include <set>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "HostileCorpus.hpp"

namespace
{

    using pvdkit::test::CorpusSource;
    using pvdkit::test::HostileCorpus;
    using pvdkit::test::MutatedFile;
    using pvdkit::test::Mutation;

    std::vector<std::byte> pattern(const std::size_t size, const std::uint8_t start)
    {
        std::vector<std::byte> bytes(size);
        for (std::size_t index = 0; index < size; ++index) {
            bytes[index] = static_cast<std::byte>(static_cast<std::uint8_t>(start + index));
        }
        return bytes;
    }

    std::vector<CorpusSource> sources()
    {
        return {{"one.avif", pattern(300, 1)}, {"two.bin", pattern(5000, 7)}, {"tiny.avif", pattern(8, 3)}};
    }

    std::vector<std::byte> readAll(const std::filesystem::path &path)
    {
        std::ifstream stream{path, std::ios::binary};
        REQUIRE(stream.good());
        std::vector<std::byte> bytes(static_cast<std::size_t>(std::filesystem::file_size(path)));
        stream.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        REQUIRE(stream.good());
        return bytes;
    }

    constexpr auto kMutationCount = static_cast<std::size_t>(Mutation::Count);

} // namespace

TEST_CASE("every mutation strategy changes the source and describes itself")
{
    const auto source = pattern(2000, 11);
    for (std::size_t index = 0; index < kMutationCount; ++index) {
        const auto mutation = static_cast<Mutation>(index);
        CAPTURE(pvdkit::test::name(mutation));
        std::mt19937 rng{42};
        for (int round = 0; round < 25; ++round) {
            const MutatedFile file = pvdkit::test::mutate(source, "src.avif", mutation, rng);
            CHECK(file.origin == "src.avif");
            CHECK_FALSE(file.mutation.empty());
            CHECK(file.utf8Path.empty());
            CHECK(file.bytes != source);
            switch (mutation) {
            case Mutation::FlipBytes:
            case Mutation::BoxSize:
            case Mutation::Splice:
            case Mutation::ZeroRange:
                CHECK(file.bytes.size() == source.size());
                break;
            case Mutation::Truncate:
            case Mutation::FlipAndTruncate:
                CHECK(file.bytes.size() < source.size());
                CHECK(file.bytes.size() >= 1);
                break;
            case Mutation::TrailingGarbage:
                CHECK(file.bytes.size() > source.size());
                CHECK(std::equal(source.begin(), source.end(), file.bytes.begin()));
                break;
            case Mutation::Count:
                FAIL("unreachable");
            }
        }
    }
}

TEST_CASE("the box-size and zero-range mutations still change a source they would leave untouched")
{
    // All zero: writing 0 into a box size or zeroing a range would be a no-op without the fallback.
    const std::vector<std::byte> zeros(64, std::byte{0});
    for (const auto mutation : {Mutation::BoxSize, Mutation::ZeroRange, Mutation::Splice}) {
        std::mt19937 rng{7};
        for (int round = 0; round < 50; ++round) {
            const auto file = pvdkit::test::mutate(zeros, "zeros.bin", mutation, rng);
            CHECK(file.bytes != zeros);
            CHECK(file.bytes.size() == zeros.size());
        }
    }
}

TEST_CASE("a corpus is deterministic for its seed, cycles sources and strategies, and lives in %TEMP%")
{
    const auto inputs = sources();
    std::vector<std::filesystem::path> paths;
    std::filesystem::path directory;
    {
        const HostileCorpus corpus{inputs, 30, 20260912};
        directory = corpus.directory();
        CHECK(std::filesystem::is_directory(directory));
        CHECK(std::filesystem::equivalent(directory.parent_path(), std::filesystem::temp_directory_path()));
        REQUIRE(corpus.files().size() == 30);

        // Sources shorter than 16 bytes are skipped: "tiny.avif" never appears, the others alternate.
        std::set<std::string> origins;
        std::set<std::string> strategies;
        for (std::size_t index = 0; index < corpus.files().size(); ++index) {
            const auto &file = corpus.files()[index];
            CAPTURE(index);
            CAPTURE(file.utf8Path);
            origins.insert(file.origin);
            CHECK(file.origin == (index % 2 == 0 ? "one.avif" : "two.bin"));
            const auto &source = file.origin == "one.avif" ? inputs[0].bytes : inputs[1].bytes;
            CHECK(file.bytes != source);
            const std::filesystem::path path{std::u8string{file.utf8Path.begin(), file.utf8Path.end()}};
            CHECK(path.parent_path() == directory);
            CHECK(path.extension() == std::filesystem::path{file.origin}.extension());
            CHECK(readAll(path) == file.bytes);
            strategies.insert(std::string{pvdkit::test::name(static_cast<Mutation>(index % kMutationCount))});
            CHECK(path.filename().string().find(pvdkit::test::name(static_cast<Mutation>(index % kMutationCount))) !=
                  std::string::npos);
            paths.push_back(path);
        }
        CHECK(origins == std::set<std::string>{"one.avif", "two.bin"});
        CHECK(strategies.size() == kMutationCount);

        const HostileCorpus again{inputs, 30, 20260912};
        REQUIRE(again.files().size() == 30);
        for (std::size_t index = 0; index < 30; ++index) {
            CHECK(again.files()[index].bytes == corpus.files()[index].bytes);
            CHECK(again.files()[index].mutation == corpus.files()[index].mutation);
        }
        CHECK(again.directory() != corpus.directory());

        const HostileCorpus other{inputs, 30, 1};
        std::size_t different = 0;
        for (std::size_t index = 0; index < 30; ++index) {
            different += other.files()[index].bytes != corpus.files()[index].bytes ? 1 : 0;
        }
        CHECK(different > 20);
    }
    CHECK_FALSE(std::filesystem::exists(directory));
    for (const auto &path : paths) {
        CHECK_FALSE(std::filesystem::exists(path));
    }
}

TEST_CASE("the mutation names are stable words a file name can carry")
{
    CHECK(pvdkit::test::name(Mutation::FlipBytes) == "flip");
    CHECK(pvdkit::test::name(Mutation::Truncate) == "truncate");
    CHECK(pvdkit::test::name(Mutation::FlipAndTruncate) == "flip+truncate");
    CHECK(pvdkit::test::name(Mutation::BoxSize) == "boxsize");
    CHECK(pvdkit::test::name(Mutation::Splice) == "splice");
    CHECK(pvdkit::test::name(Mutation::TrailingGarbage) == "append");
    CHECK(pvdkit::test::name(Mutation::ZeroRange) == "zero");
    CHECK(pvdkit::test::name(Mutation::Count) == "?");
}
