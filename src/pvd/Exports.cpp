#include <memory>
#include <utility>

#include "pvd/Firewall.hpp"
#include "pvd/PluginFactory.hpp"
#include "pvd/PvdApi.hpp"
#include "pvd/Shim.hpp"

namespace {

class ProcessState final {
 public:
  explicit ProcessState(std::unique_ptr<avifpvd::pvd::IPlugin> plugin)
      : plugin_{std::move(plugin)}, shim_{*plugin_} {}

  [[nodiscard]] avifpvd::pvd::Shim& shim() noexcept { return shim_; }

 private:
  std::unique_ptr<avifpvd::pvd::IPlugin> plugin_;
  avifpvd::pvd::Shim shim_;
};

std::unique_ptr<ProcessState> processState;

}  // namespace

extern "C" UINT32 __stdcall pvdInit(void) {
  return avifpvd::pvd::guarded(
      [] {
        processState.reset();
        auto plugin = avifpvd::pvd::makePlugin();
        if (!plugin) {
          return UINT32{0};
        }
        processState = std::make_unique<ProcessState>(std::move(plugin));
        return processState->shim().init();
      },
      UINT32{0});
}

extern "C" void __stdcall pvdExit(void) {
  avifpvd::pvd::guarded([] { processState.reset(); });
}

extern "C" void __stdcall pvdPluginInfo(pvdInfoPlugin* output) {
  avifpvd::pvd::guarded(
      [&] {
        processState ? processState->shim().pluginInfo(output)
                     : avifpvd::pvd::fillDefaultPluginInfo(output);
      });
}

extern "C" BOOL __stdcall pvdFileOpen(const char* fileName, const INT64 fileSize, const BYTE* head,
                                        const UINT32 headSize, pvdInfoImage* output, void** context) {
  return avifpvd::pvd::guarded(
      [&] {
        return processState
                   ? processState->shim().fileOpen(fileName, fileSize, head, headSize, output, context)
                   : FALSE;
      },
      FALSE);
}

extern "C" BOOL __stdcall pvdPageInfo(void* context, const UINT32 page, pvdInfoPage* output) {
  return avifpvd::pvd::guarded(
      [&] { return processState ? processState->shim().pageInfo(context, page, output) : FALSE; },
      FALSE);
}

extern "C" BOOL __stdcall pvdPageDecode(void* context, const UINT32 page, pvdInfoDecode* output,
                                          const pvdDecodeCallback callback,
                                          void* callbackContext) {
  return avifpvd::pvd::guarded(
      [&] {
        return processState ? processState->shim().pageDecode(context, page, output, callback,
                                                               callbackContext)
                            : FALSE;
      },
      FALSE);
}

extern "C" void __stdcall pvdPageFree(void* context, pvdInfoDecode* decoded) {
  avifpvd::pvd::guarded([&] {
    if (processState) {
      processState->shim().pageFree(context, decoded);
    }
  });
}

extern "C" void __stdcall pvdFileClose(void* context) {
  avifpvd::pvd::guarded([&] {
    if (processState) {
      processState->shim().fileClose(context);
    }
  });
}
