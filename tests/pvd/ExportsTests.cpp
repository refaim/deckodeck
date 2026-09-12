#include <array>
#include <cstddef>
#include <memory>
#include <new>
#include <ostream>
#include <stdexcept>
#include <string_view>

#include <doctest/doctest.h>

#include "pvd/PluginFactory.hpp"

#include "Fakes.hpp"
#include "pvd/PvdApi.hpp"

namespace {

enum class FactoryMode { Fake, Null, BadAlloc, RuntimeError, Integer };

FactoryMode factoryMode = FactoryMode::Null;
avifpvd::pvd::test::FakeState* factoryState = nullptr;

void resetExports() {
  pvdExit();
  factoryMode = FactoryMode::Null;
  factoryState = nullptr;
}

// Resets the process-wide export state on entry and on exit of every test case, so a failed
// REQUIRE cannot leave `pvdInit`'s plugin pointing at a dead `FakeState`. The guard owns that
// state: members are destroyed after the destructor body, so `pvdExit()` (which destroys the
// registered `FakePlugin`, writing `--livePlugins`) always runs while the state is still alive.
// A stack `FakeState` declared after the guard would die first and take that write.
class ExportsGuard {
 public:
  explicit ExportsGuard(const FactoryMode mode = FactoryMode::Null) {
    resetExports();
    factoryState = &state_;
    factoryMode = mode;
  }
  ~ExportsGuard() { resetExports(); }
  ExportsGuard(const ExportsGuard&) = delete;
  ExportsGuard& operator=(const ExportsGuard&) = delete;

  [[nodiscard]] avifpvd::pvd::test::FakeState& state() noexcept { return state_; }

 private:
  avifpvd::pvd::test::FakeState state_;
};

}  // namespace

namespace avifpvd::pvd {

std::unique_ptr<IPlugin> makePlugin() {
  switch (factoryMode) {
    case FactoryMode::Fake:
      return std::make_unique<test::FakePlugin>(*factoryState);
    case FactoryMode::Null:
      return {};
    case FactoryMode::BadAlloc:
      throw std::bad_alloc{};
    case FactoryMode::RuntimeError:
      throw std::runtime_error{"factory failure"};
    case FactoryMode::Integer:
      throw 11;
  }
  return {};
}

}  // namespace avifpvd::pvd

namespace {

TEST_CASE("exports provide safe behavior before init and after exit") {
  const ExportsGuard guard;
  pvdInfoPlugin info{};
  pvdPluginInfo(&info);
  CHECK(info.Priority == 10);
  CHECK(std::string_view{info.pName} == "AVIF");
  CHECK(std::string_view{info.pVersion} == "1.0.0");
  CHECK(std::string_view{info.pComments}.empty());
  CHECK_NOTHROW(pvdPluginInfo(nullptr));

  pvdInfoImage image{};
  pvdInfoPage page{};
  pvdInfoDecode decoded{};
  void* context = nullptr;
  CHECK(pvdFileOpen("x", 0, nullptr, 0, &image, &context) == FALSE);
  CHECK(pvdPageInfo(nullptr, 0, &page) == FALSE);
  CHECK(pvdPageDecode(nullptr, 0, &decoded, nullptr, nullptr) == FALSE);
  CHECK_NOTHROW(pvdPageFree(nullptr, &decoded));
  CHECK_NOTHROW(pvdFileClose(nullptr));
  CHECK_NOTHROW(pvdExit());
  CHECK_NOTHROW(pvdExit());

  pvdPluginInfo(&info);
  CHECK(info.Priority == 10);
}

TEST_CASE("exports forward all eight operations through a live shim") {
  ExportsGuard guard{FactoryMode::Fake};
  auto& state = guard.state();

  CHECK(pvdInit() == PVD_CURRENT_INTERFACE_VERSION);
  CHECK(state.livePlugins == 1);
  pvdInfoPlugin pluginInfo{};
  pvdPluginInfo(&pluginInfo);
  CHECK(pluginInfo.Priority == state.pluginInfo.priority);

  constexpr std::array bytes{std::byte{0x21}, std::byte{0x22}};
  pvdInfoImage imageInfo{};
  void* context = nullptr;
  REQUIRE(pvdFileOpen("export.avif", 0, reinterpret_cast<const BYTE*>(bytes.data()),
                      static_cast<UINT32>(bytes.size()), &imageInfo, &context) == TRUE);
  CHECK(state.liveSessions == 1);

  pvdInfoPage pageInfo{};
  CHECK(pvdPageInfo(context, 1, &pageInfo) == TRUE);
  pvdInfoDecode decodeInfo{};
  CHECK(pvdPageDecode(context, 1, &decodeInfo, nullptr, nullptr) == TRUE);
  pvdPageFree(context, &decodeInfo);
  CHECK(state.freeCalls == 1);
  pvdFileClose(context);
  CHECK(state.liveSessions == 0);

  pvdExit();
  CHECK(state.livePlugins == 0);
  CHECK(pvdPageInfo(nullptr, 0, &pageInfo) == FALSE);
}

TEST_CASE("double init replaces the process-wide plugin and double exit is safe") {
  ExportsGuard guard{FactoryMode::Fake};
  auto& state = guard.state();

  CHECK(pvdInit() == PVD_CURRENT_INTERFACE_VERSION);
  CHECK(state.livePlugins == 1);
  CHECK(pvdInit() == PVD_CURRENT_INTERFACE_VERSION);
  CHECK(state.livePlugins == 1);
  pvdExit();
  CHECK(state.livePlugins == 0);
  CHECK_NOTHROW(pvdExit());
}

TEST_CASE("init rejects a null plugin and firewalls factory exceptions") {
  const ExportsGuard guard;
  CHECK(pvdInit() == 0);

  for (const auto mode : {FactoryMode::BadAlloc, FactoryMode::RuntimeError, FactoryMode::Integer}) {
    factoryMode = mode;
    CHECK(pvdInit() == 0);
  }

  pvdInfoPlugin info{};
  pvdPluginInfo(&info);
  CHECK(info.Priority == 10);
}

TEST_CASE("failed reinitialization leaves exports uninitialized") {
  ExportsGuard guard{FactoryMode::Fake};
  auto& state = guard.state();
  REQUIRE(pvdInit() == PVD_CURRENT_INTERFACE_VERSION);
  CHECK(state.livePlugins == 1);

  factoryMode = FactoryMode::RuntimeError;
  CHECK(pvdInit() == 0);
  CHECK(state.livePlugins == 0);
  pvdInfoPage pageInfo{};
  CHECK(pvdPageInfo(nullptr, 0, &pageInfo) == FALSE);
}

TEST_CASE("exceptions from a live plugin remain behind the export firewall") {
  ExportsGuard guard{FactoryMode::Fake};
  auto& state = guard.state();
  REQUIRE(pvdInit() == PVD_CURRENT_INTERFACE_VERSION);

  // The shim fills the constant identity before asking the plugin, so a throwing info() leaves
  // the host with priority 10 / "AVIF" / "1.0.0" rather than the garbage it passed in.
  state.throwPluginInfo = true;
  pvdInfoPlugin pluginInfo{3, "garbage", "garbage", "garbage"};
  CHECK_NOTHROW(pvdPluginInfo(&pluginInfo));
  CHECK(pluginInfo.Priority == 10);
  CHECK(std::string_view{pluginInfo.pName} == "AVIF");
  CHECK(std::string_view{pluginInfo.pVersion} == "1.0.0");
  CHECK(std::string_view{pluginInfo.pComments}.empty());
  state.throwPluginInfo = false;

  state.throwOpen = true;
  pvdInfoImage imageInfo{};
  void* context = nullptr;
  CHECK(pvdFileOpen("throw.avif", 1, nullptr, 0, &imageInfo, &context) == FALSE);
  CHECK(state.liveSessions == 0);

  // Explicit teardown on the green path: the plugin dies here, while `state` is still alive.
  pvdExit();
  CHECK(state.livePlugins == 0);
}

}  // namespace
