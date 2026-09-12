#include <memory>
#include <utility>

#include "pvd/Firewall.hpp"
// Generated per plugin (cmake/pvdkit-plugin.cmake): this translation unit is compiled once into
// every plugin DLL and is the only shared file that may include it.
#include "pvd/PluginConstants.hpp"
#include "pvd/PluginFactory.hpp"
#include "pvd/PvdApi.hpp"
#include "pvd/Shim.hpp"

namespace {

class ProcessState final {
 public:
  explicit ProcessState(std::unique_ptr<pvdkit::pvd::IPlugin> plugin)
      : plugin_{std::move(plugin)}, shim_{*plugin_, pvdkit::pvd::kPluginIdentity} {}

  [[nodiscard]] pvdkit::pvd::Shim& shim() noexcept { return shim_; }

 private:
  std::unique_ptr<pvdkit::pvd::IPlugin> plugin_;
  pvdkit::pvd::Shim shim_;
};

std::unique_ptr<ProcessState> processState;

}  // namespace

extern "C" UINT32 __stdcall pvdInit(void) {
  return pvdkit::pvd::guarded(
      [] {
        processState.reset();
        auto plugin = pvdkit::pvd::makePlugin();
        if (!plugin) {
          return UINT32{0};
        }
        processState = std::make_unique<ProcessState>(std::move(plugin));
        return processState->shim().init();
      },
      UINT32{0});
}

extern "C" void __stdcall pvdExit(void) {
  pvdkit::pvd::guarded([] { processState.reset(); });
}

extern "C" void __stdcall pvdPluginInfo(pvdInfoPlugin* output) {
  pvdkit::pvd::guarded(
      [&] {
        processState ? processState->shim().pluginInfo(output)
                     : pvdkit::pvd::fillDefaultPluginInfo(output, pvdkit::pvd::kPluginIdentity);
      });
}

extern "C" BOOL __stdcall pvdFileOpen(const char* fileName, const INT64 fileSize, const BYTE* head,
                                        const UINT32 headSize, pvdInfoImage* output, void** context) {
  return pvdkit::pvd::guarded(
      [&] {
        return processState
                   ? processState->shim().fileOpen(fileName, fileSize, head, headSize, output, context)
                   : FALSE;
      },
      FALSE);
}

extern "C" BOOL __stdcall pvdPageInfo(void* context, const UINT32 page, pvdInfoPage* output) {
  return pvdkit::pvd::guarded(
      [&] { return processState ? processState->shim().pageInfo(context, page, output) : FALSE; },
      FALSE);
}

extern "C" BOOL __stdcall pvdPageDecode(void* context, const UINT32 page, pvdInfoDecode* output,
                                          const pvdDecodeCallback callback,
                                          void* callbackContext) {
  return pvdkit::pvd::guarded(
      [&] {
        return processState ? processState->shim().pageDecode(context, page, output, callback,
                                                               callbackContext)
                            : FALSE;
      },
      FALSE);
}

extern "C" void __stdcall pvdPageFree(void* context, pvdInfoDecode* decoded) {
  pvdkit::pvd::guarded([&] {
    if (processState) {
      processState->shim().pageFree(context, decoded);
    }
  });
}

extern "C" void __stdcall pvdFileClose(void* context) {
  pvdkit::pvd::guarded([&] {
    if (processState) {
      processState->shim().fileClose(context);
    }
  });
}
