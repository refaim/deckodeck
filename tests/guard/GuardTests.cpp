#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>

namespace {

enum class ScanState { Code, LineComment, BlockComment, StringLiteral, CharacterLiteral };

[[nodiscard]] bool isDigitSeparator(const std::string_view source, const std::size_t index) {
  if (index == 0 || index + 1 >= source.size()) {
    return false;
  }

  const auto isIdentifierCharacter = [](const unsigned char character) {
    return std::isalnum(character) != 0 || character == '_';
  };
  if (!isIdentifierCharacter(static_cast<unsigned char>(source[index - 1])) ||
      !isIdentifierCharacter(static_cast<unsigned char>(source[index + 1]))) {
    return false;
  }

  auto tokenStart = index;
  while (tokenStart > 0) {
    const auto character = static_cast<unsigned char>(source[tokenStart - 1]);
    if (!isIdentifierCharacter(character) && character != '.') {
      break;
    }
    --tokenStart;
  }
  return std::ranges::any_of(source.substr(tokenStart, index - tokenStart),
                             [](const unsigned char character) { return std::isdigit(character) != 0; });
}

[[nodiscard]] bool hasCharacterLiteralTerminator(const std::string_view source, const std::size_t opening) {
  for (auto index = opening + 1; index < source.size(); ++index) {
    if (source[index] == '\n' || source[index] == '\r') {
      return false;
    }
    if (source[index] == '\\' && index + 1 < source.size()) {
      ++index;
    } else if (source[index] == '\'') {
      return true;
    }
  }
  return false;
}

[[nodiscard]] std::string stripIgnored(const std::string_view source, const bool stripLiterals) {
  std::string stripped;
  stripped.reserve(source.size());
  auto state = ScanState::Code;

  for (std::size_t index = 0; index < source.size(); ++index) {
    const char current = source[index];
    const char next = index + 1 < source.size() ? source[index + 1] : '\0';

    if (state == ScanState::Code) {
      if (current == '/' && next == '/') {
        stripped.append(2, ' ');
        ++index;
        state = ScanState::LineComment;
      } else if (current == '/' && next == '*') {
        stripped.append(2, ' ');
        ++index;
        state = ScanState::BlockComment;
      } else if (current == '"') {
        stripped.push_back(stripLiterals ? ' ' : current);
        state = ScanState::StringLiteral;
      } else if (current == '\'' && !isDigitSeparator(source, index) &&
                 hasCharacterLiteralTerminator(source, index)) {
        stripped.push_back(stripLiterals ? ' ' : current);
        state = ScanState::CharacterLiteral;
      } else {
        stripped.push_back(current);
      }
      continue;
    }

    if (state == ScanState::LineComment) {
      stripped.push_back(current == '\n' ? '\n' : ' ');
      if (current == '\n') {
        state = ScanState::Code;
      }
      continue;
    }

    if (state == ScanState::BlockComment) {
      if (current == '*' && next == '/') {
        stripped.append(2, ' ');
        ++index;
        state = ScanState::Code;
      } else {
        stripped.push_back(current == '\n' ? '\n' : ' ');
      }
      continue;
    }

    const auto terminator = state == ScanState::StringLiteral ? '"' : '\'';
    if (current == '\\' && next != '\0') {
      if (stripLiterals) {
        stripped.append(2, ' ');
      } else {
        stripped.push_back(current);
        stripped.push_back(next);
      }
      ++index;
    } else {
      stripped.push_back(stripLiterals && current != '\n' ? ' ' : current);
      if (current == terminator) {
        state = ScanState::Code;
      }
    }
  }

  return stripped;
}

[[nodiscard]] std::string stripCommentsAndLiterals(const std::string_view source) {
  return stripIgnored(source, true);
}

[[nodiscard]] std::string stripComments(const std::string_view source) {
  return stripIgnored(source, false);
}

[[nodiscard]] std::string lowercase(std::string text) {
  std::ranges::transform(text, text.begin(),
                         [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
  return text;
}

[[nodiscard]] std::string normalizedLowerPath(const std::filesystem::path& path) {
  auto normalized = path.generic_string();
  std::ranges::transform(normalized, normalized.begin(),
                         [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
  return normalized;
}

[[nodiscard]] bool isAdapterOrPvd(const std::string_view path) {
  return path.find("/src/adapters/") != std::string_view::npos ||
         path.find("/src/pvd/") != std::string_view::npos;
}

[[nodiscard]] bool mayIncludeForeignHeaders(const std::string_view path) {
  return path.find("/src/adapters/") != std::string_view::npos || path.ends_with("/src/pvd/exports.cpp");
}

// pvd/PvdApi.hpp exists to include <Windows.h> (for the SDK's fixed-width typedefs) exactly once
// on behalf of the marshalling layer; it may not reach for libavif.
[[nodiscard]] bool mayIncludeWindowsHeader(const std::string_view path) {
  return mayIncludeForeignHeaders(path) || path.ends_with("/src/pvd/pvdapi.hpp");
}

[[nodiscard]] bool isPvd(const std::string_view path) {
  return path.find("/src/pvd/") != std::string_view::npos;
}

[[nodiscard]] bool isAdapters(const std::string_view path) {
  return path.find("/src/adapters/") != std::string_view::npos;
}

struct Violation {
  std::string token;
};

struct CompiledRule {
  std::regex pattern;
  std::string_view label;
};

// Every rule below is matched with std::regex against every scanned file, and "source tree obeys
// ownership and layering rules" scans the whole src/ tree. Compiling a std::regex is comparatively
// expensive, so each pattern is compiled exactly once into this table (a function-local static is
// initialized on first use only) instead of once per rule per file.
struct GuardRegexTable {
  std::vector<CompiledRule> forbiddenEverywhere;
  std::regex reinterpretCast;
  std::regex windowsHeader;
  std::regex avifHeader;
  std::regex pvdInclude;
  std::regex adaptersInclude;
  std::regex catchOpen;
};

[[nodiscard]] const GuardRegexTable& guardRegexes() {
  static const GuardRegexTable table{
      .forbiddenEverywhere =
          {
              CompiledRule{std::regex{R"(\bnew\b)"}, "new"},
              // `= delete;` on a special member (required by the architecture contract for
              // classes holding references) must not be flagged; only `delete` used as the
              // deallocation operator (optionally `delete[]`), i.e. followed by an operand, is
              // forbidden.
              CompiledRule{std::regex{R"(\bdelete\b\s*(\[\s*\])?\s*[^\s;])"}, "delete"},
              CompiledRule{std::regex{R"(\bmalloc\b)"}, "malloc"},
              CompiledRule{std::regex{R"(\bcalloc\b)"}, "calloc"},
              CompiledRule{std::regex{R"(\brealloc\b)"}, "realloc"},
              CompiledRule{std::regex{R"(\bfree\s*\()"}, "free("},
              CompiledRule{std::regex{R"(\bshared_ptr\b)"}, "shared_ptr"},
              CompiledRule{std::regex{R"(\bweak_ptr\b)"}, "weak_ptr"},
              CompiledRule{std::regex{R"(\blcov_excl)"}, "LCOV_EXCL"},
              CompiledRule{std::regex{R"(\b__builtin_unreachable\b)"}, "__builtin_unreachable"},
              CompiledRule{std::regex{R"(\[\[\s*assume\b)"}, "[[assume"},
          },
      .reinterpretCast = std::regex{R"(\breinterpret_cast\b)"},
      .windowsHeader = std::regex{R"((^|\n)[ \t]*#[ \t]*include[ \t]*[<"][ \t]*windows[.]h[ \t]*[>"])"},
      .avifHeader = std::regex{R"((^|\n)[ \t]*#[ \t]*include[ \t]*[<"][ \t]*avif/avif[.]h[ \t]*[>"])"},
      // Captures the whole header path after `pvd/` (nested directories included, `.h` or `.hpp`)
      // so callers can check it against an allowlist, rather than hard-coding the forbidden names
      // in the pattern itself.
      .pvdInclude = std::regex{
          R"((?:^|\n)[ \t]*#[ \t]*include[ \t]*[<"][ \t]*pvd/([a-z0-9_/]+)[.]h(?:pp)?[ \t]*[>"])"},
      .adaptersInclude =
          std::regex{R"((?:^|\n)[ \t]*#[ \t]*include[ \t]*[<"][ \t]*adapters/[a-z0-9_/]+[.]h(?:pp)?[ \t]*[>"])"},
      .catchOpen = std::regex{R"(\bcatch\s*\()"},
  };
  return table;
}

// Every `pvd/<name>.h(pp)` header path a file includes, lowercased and without the extension.
[[nodiscard]] std::vector<std::string> pvdHeadersIncludedBy(const std::string_view includeCode,
                                                            const std::regex& pvdIncludePattern) {
  std::vector<std::string> headers;
  const auto end = std::cregex_iterator{};
  for (auto it = std::cregex_iterator{includeCode.data(), includeCode.data() + includeCode.size(),
                                      pvdIncludePattern};
       it != end; ++it) {
    headers.push_back((*it)[1].str());
  }
  return headers;
}

void appendIfMatches(std::vector<Violation>& violations, const std::string_view code,
                     const std::regex& pattern, const std::string_view label) {
  if (std::regex_search(code.begin(), code.end(), pattern)) {
    violations.push_back({std::string{label}});
  }
}

[[nodiscard]] bool isAllowedCoreHeader(const std::string_view headerName) {
  // src/core/** may reach into the shared PVD boundary value/plugin contracts only; every other
  // pvd/... header (Shim, Firewall, Marshal, ContextHandle, PluginFactory, ...) belongs to the
  // adapters/pvd marshalling layer and is forbidden here. This is an allowlist, not a blocklist
  // of currently-known headers, so a new pvd header is forbidden in core by default. The whole
  // captured path is compared, so `pvd/detail/types` or `pvd/types/extra` do not qualify either.
  return headerName == "types" || headerName == "plugin";
}

// ARCHITECTURE §2: `pvd` (shim side) and `adapters` never include each other. They meet only in
// the two composition roots, and pvd/PluginFactory.hpp is the seam between them:
//   - src/pvd/Exports.cpp is the only pvd file that may include pvd/PluginFactory.hpp; no pvd file
//     may include an adapters header;
//   - src/adapters/DefaultPlugin.cpp is the only adapters file that may include pvd headers, and
//     only the seam, the boundary contracts and the plugin constants it publishes through them.
[[nodiscard]] bool isAllowedDefaultPluginHeader(const std::string_view headerName) {
  return headerName == "pluginfactory" || headerName == "plugin" || headerName == "types" ||
         headerName == "pluginconstants";
}

void appendLayeringViolations(std::vector<Violation>& violations, const std::string_view normalizedPath,
                              const std::string_view includeCode, const GuardRegexTable& regexes) {
  const auto pvdHeaders = pvdHeadersIncludedBy(includeCode, regexes.pvdInclude);

  if (normalizedPath.find("/src/core/") != std::string_view::npos &&
      !std::ranges::all_of(pvdHeaders, isAllowedCoreHeader)) {
    violations.push_back({"forbidden pvd header"});
  }

  if (isPvd(normalizedPath)) {
    appendIfMatches(violations, includeCode, regexes.adaptersInclude, "#include adapters/ in pvd");
    if (!normalizedPath.ends_with("/src/pvd/exports.cpp") &&
        std::ranges::find(pvdHeaders, "pluginfactory") != pvdHeaders.end()) {
      violations.push_back({"pvd/PluginFactory.hpp outside Exports.cpp"});
    }
  }

  if (isAdapters(normalizedPath)) {
    const bool isDefaultPlugin = normalizedPath.ends_with("/src/adapters/defaultplugin.cpp");
    const bool allowed = isDefaultPlugin ? std::ranges::all_of(pvdHeaders, isAllowedDefaultPluginHeader)
                                         : pvdHeaders.empty();
    if (!allowed) {
      violations.push_back({"#include pvd/ in adapters"});
    }
  }
}

[[nodiscard]] std::vector<Violation> scan(const std::filesystem::path& path, const std::string_view source) {
  const auto& regexes = guardRegexes();
  const auto code = lowercase(stripCommentsAndLiterals(source));
  // Include-directive rules intentionally run on comment-stripped but literal-preserving text
  // (stripComments, not stripCommentsAndLiterals): stripping literals would blank out the quoted
  // form `#include "windows.h"` and the guard would stop detecting it.
  const auto includeCode = lowercase(stripComments(source));
  const auto normalizedPath = normalizedLowerPath(std::filesystem::absolute(path));
  std::vector<Violation> violations;

  for (const auto& rule : regexes.forbiddenEverywhere) {
    appendIfMatches(violations, code, rule.pattern, rule.label);
  }

  if (!isAdapterOrPvd(normalizedPath)) {
    appendIfMatches(violations, code, regexes.reinterpretCast, "reinterpret_cast");
  }

  if (!mayIncludeWindowsHeader(normalizedPath)) {
    appendIfMatches(violations, includeCode, regexes.windowsHeader, "#include windows.h");
  }
  if (!mayIncludeForeignHeaders(normalizedPath)) {
    appendIfMatches(violations, includeCode, regexes.avifHeader, "#include avif/avif.h");
  }

  appendLayeringViolations(violations, normalizedPath, includeCode, regexes);

  if (!normalizedPath.ends_with("/src/pvd/firewall.hpp")) {
    appendIfMatches(violations, code, regexes.catchOpen, "catch(");
  }

  return violations;
}

[[nodiscard]] std::string readFile(const std::filesystem::path& path) {
  std::ifstream input{path, std::ios::binary};
  return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

TEST_CASE("scanner strips comments and literals") {
  const auto stripped = stripCommentsAndLiterals(
      "keep; // new Thing\n/* delete value */ keep2; \"malloc\"; 'x'; \"escaped \\\" free(\";");
  CHECK(stripped.find("keep;") != std::string::npos);
  CHECK(stripped.find("keep2;") != std::string::npos);
  CHECK(stripped.find("new Thing") == std::string::npos);
  CHECK(stripped.find("delete value") == std::string::npos);
  CHECK(stripped.find("malloc") == std::string::npos);
  CHECK(stripped.find("free(") == std::string::npos);
}

TEST_CASE("every unconditional rule has positive and negative scanner samples") {
  constexpr std::array samples{
      std::string_view{"new Thing;"},
      std::string_view{"new(Thing);"},
      std::string_view{"delete value;"},
      std::string_view{"malloc(4);"},
      std::string_view{"calloc(1, 4);"},
      std::string_view{"realloc(value, 4);"},
      std::string_view{"free(value);"},
      std::string_view{"std::shared_ptr<int> value;"},
      std::string_view{"std::weak_ptr<int> value;"},
      std::string_view{"LCOV_EXCL_LINE"},
      std::string_view{"__builtin_unreachable();"},
      std::string_view{"[[assume(true)]];"},
  };

  for (const auto sample : samples) {
    CAPTURE(sample);
    CHECK_FALSE(scan("project/src/core/Sample.cpp", sample).empty());
    CHECK(scan("project/src/core/Sample.cpp", std::string{"// "} + std::string{sample} + "\n").empty());
    CHECK(scan("project/src/core/Sample.cpp", std::string{"constexpr auto text = \""} +
                                                   std::string{sample} + "\";")
              .empty());
  }
}

TEST_CASE("scanner catches token spellings that previously bypassed the guard") {
  constexpr std::array forbiddenSamples{
      std::string_view{"delete[] p;"},
      std::string_view{"new\n  Foo();"},
      std::string_view{"new\tFoo();"},
      std::string_view{"catch(...) {}"},
      std::string_view{"#include <Windows.h>"},
      std::string_view{"#include \"windows.h\""},
      std::string_view{"std::free (p);"},
      std::string_view{"constexpr auto size = 268'435'456; delete p;"},
      std::string_view{"delete p;"},
      std::string_view{"malloc(4);"},
  };

  for (const auto sample : forbiddenSamples) {
    CAPTURE(sample);
    CHECK_FALSE(scan("project/src/core/Sample.cpp", sample).empty());
  }
}

TEST_CASE("scanner respects token boundaries and stripped text") {
  constexpr std::array allowedSamples{
      std::string_view{"deleted = true;"},
      std::string_view{"newline();"},
      std::string_view{"renew();"},
      std::string_view{"free_list.clear();"},
      std::string_view{R"(constexpr char quote = '\'';)"},
      std::string_view{R"(constexpr auto text = "delete value";)"},
      std::string_view{"// malloc(4);\nreturn;"},
      std::string_view{"constexpr auto size = 268'435'456;"},
  };

  for (const auto sample : allowedSamples) {
    CAPTURE(sample);
    CHECK(scan("project/src/core/Sample.cpp", sample).empty());
  }
}

TEST_CASE("delete rule flags the deallocation operator but not defaulted or deleted special members") {
  constexpr std::array notFlagged{
      std::string_view{"S(const S&) = delete;"},
      std::string_view{"S& operator=(S&&) = delete;"},
      std::string_view{"S(S&&)=delete;"},
      std::string_view{"S(const S&) = delete ;"},
      std::string_view{"~S() = default;"},
      std::string_view{"bool deleted = true;"},
      std::string_view{"std::default_delete<int> d;"},
  };
  for (const auto sample : notFlagged) {
    CAPTURE(sample);
    CHECK(scan("project/src/core/Sample.cpp", sample).empty());
  }

  constexpr std::array flagged{
      std::string_view{"delete p;"},
      std::string_view{"delete[] p;"},
      std::string_view{"delete [] p;"},
      std::string_view{"delete\np;"},
      std::string_view{"delete\tp;"},
      std::string_view{"operator delete(void*);"},
  };
  for (const auto sample : flagged) {
    CAPTURE(sample);
    CHECK_FALSE(scan("project/src/core/Sample.cpp", sample).empty());
  }
}

TEST_CASE("reinterpret casts are restricted to adapters and pvd") {
  CHECK(scan("project/src/core/Sample.cpp", "reinterpret_cast<int>(value);").empty() == false);
  CHECK(scan("project/src/adapters/avif/Sample.cpp", "reinterpret_cast<int>(value);").empty());
  CHECK(scan("project/src/pvd/Sample.cpp", "reinterpret_cast<int>(value);").empty());
  CHECK(scan("project/src/core/Sample.cpp", "// reinterpret_cast<int>(value);").empty());
}

TEST_CASE("foreign headers are restricted to adapters and Exports") {
  for (const auto include : {std::string_view{"#include <windows.h>"},
                             std::string_view{"#include \"windows.h\""},
                             std::string_view{"#include <Windows.h>"},
                             std::string_view{"#include <avif/avif.h>"},
                             std::string_view{"#include \"AVIF/AVIF.H\""}}) {
    CAPTURE(include);
    CHECK(scan("project/src/core/Sample.cpp", include).empty() == false);
    CHECK(scan("project/src/adapters/Sample.cpp", include).empty());
    CHECK(scan("project/src/pvd/Exports.cpp", include).empty());
    CHECK(scan("project/src/core/Sample.cpp", std::string{"// "} + std::string{include}).empty());
  }
}

TEST_CASE("core may include only the shared pvd boundary contracts") {
  CHECK(scan("project/src/core/Sample.cpp", "#include \"pvd/Types.hpp\"").empty());
  CHECK(scan("project/src/core/Sample.cpp", "#include <pvd/Plugin.hpp>").empty());

  for (const auto header : {std::string_view{"Shim.hpp"}, std::string_view{"ContextHandle.hpp"},
                            std::string_view{"Firewall.hpp"}, std::string_view{"PluginFactory.hpp"}}) {
    CAPTURE(header);
    CHECK_FALSE(scan("project/src/core/Sample.cpp", std::string{"#include \"pvd/"} +
                                                       std::string{header} + "\"")
                    .empty());
  }
}

TEST_CASE("core layering matches nested pvd paths and both header extensions") {
  // The pattern must see `pvd/<anything>.h` and `pvd/<anything>.hpp` and compare the whole captured
  // name (nested directories included) with the allowlist, so a header placed one level deeper or
  // spelled with `.h` cannot slip past the guard.
  CHECK(scan("project/src/core/Sample.cpp", "#include \"pvd/Types.h\"").empty());
  CHECK(scan("project/src/core/Sample.cpp", "#include <pvd/Plugin.h>").empty());
  CHECK_FALSE(scan("project/src/core/Sample.cpp", "#include \"pvd/Shim.h\"").empty());
  CHECK_FALSE(scan("project/src/core/Sample.cpp", "#include \"pvd/nested/Shim.hpp\"").empty());
  CHECK_FALSE(scan("project/src/core/Sample.cpp", "#include \"pvd/detail/Types.hpp\"").empty());
  CHECK_FALSE(scan("project/src/core/Sample.cpp", "#include \"pvd/Types/Extra.hpp\"").empty());
  CHECK_FALSE(scan("project/src/core/Sample.cpp", "#include \"pvd/PvdApi.hpp\"").empty());
  CHECK_FALSE(scan("project/src/core/Sample.cpp", "#include \"pvd/PluginConstants.hpp\"").empty());
}

TEST_CASE("pvd and adapters never include each other except through the two composition roots") {
  // ARCHITECTURE §2: `pvd` (shim side) and `adapters` meet only in Exports.cpp / DefaultPlugin.cpp,
  // and pvd/PluginFactory.hpp is the seam those two use.
  for (const auto path : {std::string_view{"project/src/pvd/Shim.cpp"},
                          std::string_view{"project/src/pvd/Shim.hpp"},
                          std::string_view{"project/src/pvd/Exports.cpp"}}) {
    CAPTURE(path);
    CHECK_FALSE(scan(path, "#include \"adapters/win/FileSource.hpp\"").empty());
    CHECK_FALSE(scan(path, "#include <adapters/avif/Decoder.hpp>").empty());
    CHECK(scan(path, "// #include \"adapters/win/FileSource.hpp\"").empty());
    CHECK(scan(path, "#include \"pvd/Shim.hpp\"").empty());
  }
  CHECK(scan("project/src/pvd/Exports.cpp", "#include \"pvd/PluginFactory.hpp\"").empty());
  CHECK_FALSE(scan("project/src/pvd/Shim.cpp", "#include \"pvd/PluginFactory.hpp\"").empty());
  CHECK_FALSE(scan("project/src/pvd/Shim.hpp", "#include \"pvd/PluginFactory.h\"").empty());

  for (const auto path : {std::string_view{"project/src/adapters/avif/Decoder.hpp"},
                          std::string_view{"project/src/adapters/win/FileSource.cpp"}}) {
    CAPTURE(path);
    CHECK_FALSE(scan(path, "#include \"pvd/Shim.hpp\"").empty());
    CHECK_FALSE(scan(path, "#include \"pvd/Types.hpp\"").empty());
    CHECK_FALSE(scan(path, "#include \"pvd/PluginFactory.hpp\"").empty());
    CHECK(scan(path, "#include \"core/IDecoder.hpp\"").empty());
    CHECK(scan(path, "#include \"adapters/win/Utf8.hpp\"").empty());
    CHECK(scan(path, "// #include \"pvd/Shim.hpp\"").empty());
  }
  for (const auto header : {std::string_view{"pvd/PluginFactory.hpp"}, std::string_view{"pvd/Plugin.hpp"},
                            std::string_view{"pvd/Types.hpp"}, std::string_view{"pvd/PluginConstants.hpp"},
                            std::string_view{"adapters/win/FileSource.hpp"}}) {
    CAPTURE(header);
    CHECK(scan("project/src/adapters/DefaultPlugin.cpp",
               std::string{"#include \""} + std::string{header} + "\"")
              .empty());
  }
  for (const auto header : {std::string_view{"pvd/Shim.hpp"}, std::string_view{"pvd/ContextHandle.hpp"},
                            std::string_view{"pvd/Firewall.hpp"}, std::string_view{"pvd/PvdApi.hpp"},
                            std::string_view{"pvd/nested/Types.hpp"}}) {
    CAPTURE(header);
    CHECK_FALSE(scan("project/src/adapters/DefaultPlugin.cpp",
                     std::string{"#include \""} + std::string{header} + "\"")
                    .empty());
  }
}

TEST_CASE("PvdApi.hpp is the one pvd header that may include Windows.h") {
  CHECK(scan("project/src/pvd/PvdApi.hpp", "#include <Windows.h>").empty());
  CHECK_FALSE(scan("project/src/pvd/PvdApi.hpp", "#include <avif/avif.h>").empty());
  CHECK_FALSE(scan("project/src/pvd/Shim.hpp", "#include <Windows.h>").empty());
  CHECK_FALSE(scan("project/src/pvd/Shim.cpp", "#include \"windows.h\"").empty());
}

TEST_CASE("core layering is an allowlist, not a fixed blocklist") {
  // Any pvd/... header other than the two boundary contracts is forbidden in core, not just the
  // four historically enumerated ones.
  CHECK(scan("project/src/core/Sample.cpp", "#include \"pvd/Types.hpp\"").empty());
  CHECK_FALSE(scan("project/src/core/Sample.cpp", "#include \"pvd/Marshal.hpp\"").empty());
}

TEST_CASE("catch-all handling is restricted to Firewall") {
  CHECK(scan("project/src/core/Sample.cpp", "try {} catch (...) {}").empty() == false);
  CHECK(scan("project/src/pvd/Firewall.hpp", "try {} catch (...) {}").empty());
  CHECK(scan("project/src/core/Sample.cpp", "// catch (...)").empty());
}

TEST_CASE("source tree obeys ownership and layering rules") {
  const auto sourceRoot = std::filesystem::path{AVIFPVD_SOURCE_DIR} / "src";
  std::ostringstream failures;
  std::size_t failureCount = 0;

  for (const auto& entry : std::filesystem::recursive_directory_iterator{sourceRoot}) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const auto extension = entry.path().extension().string();
    if (extension != ".cpp" && extension != ".hpp" && extension != ".h") {
      continue;
    }
    for (const auto& violation : scan(entry.path(), readFile(entry.path()))) {
      ++failureCount;
      failures << entry.path().generic_string() << ": forbidden token " << violation.token << '\n';
    }
  }

  INFO(failures.str());
  CHECK(failureCount == 0);
}

}  // namespace
