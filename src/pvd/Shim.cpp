#include "pvd/Shim.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <utility>

#include "pvd/ContextHandle.hpp"
#include "pvd/Firewall.hpp"

namespace pvdkit::pvd
{

    void fillDefaultPluginInfo(pvdInfoPlugin *output, const PluginIdentity &identity) noexcept
    {
        if (output == nullptr) {
            return;
        }
        output->Priority = identity.priority;
        output->pName = identity.name.data();
        output->pVersion = identity.version.data();
        output->pComments = "";
    }

    Shim::Shim(IPlugin &plugin, const PluginIdentity &identity) noexcept : plugin_{plugin}, identity_{identity}
    {
    }

    UINT32 Shim::init() noexcept
    {
        return guarded([] { return UINT32{PVD_CURRENT_INTERFACE_VERSION}; }, UINT32{0});
    }

    void Shim::exit() noexcept
    {
        guarded([] {});
    }

    void Shim::pluginInfo(pvdInfoPlugin *output) noexcept
    {
        fillDefaultPluginInfo(output, identity_);
        guarded([&] {
            if (output == nullptr) {
                return;
            }
            const auto &info = plugin_.info();
            output->Priority = info.priority;
            output->pName = info.name.c_str();
            output->pVersion = info.version.c_str();
            output->pComments = info.comments.c_str();
        });
    }

    BOOL Shim::fileOpen(const char *fileName, const INT64 fileSize, const BYTE *head, const UINT32 headSize,
                        pvdInfoImage *output, void **context) noexcept
    {
        return guarded(
            [&] {
                if (fileName == nullptr) {
                    return BOOL{0};
                }
                if (output == nullptr) {
                    return BOOL{0};
                }
                if (context == nullptr) {
                    return BOOL{0};
                }
                *context = nullptr;
                if (head == nullptr && headSize != 0) {
                    return BOOL{0};
                }
                if (fileSize < 0) {
                    return BOOL{0};
                }

                const auto bytes = std::span<const std::byte>{reinterpret_cast<const std::byte *>(head),
                                                              static_cast<std::size_t>(headSize)};
                auto opened =
                    plugin_.open(OpenRequest{std::string_view{fileName}, static_cast<std::uint64_t>(fileSize), bytes});
                if (!opened) {
                    return BOOL{0};
                }
                auto session = std::move(*opened);
                if (!session) {
                    return BOOL{0};
                }

                const auto &info = session->imageInfo();
                output->nPages = info.pageCount;
                output->Flags = info.animated ? UINT32{PVD_IIF_ANIMATED} : UINT32{0};
                output->pFormatName = info.formatName.c_str();
                output->pCompression = info.compression.c_str();
                output->pComments = info.comments.c_str();
                *context = toHost(std::move(session));
                return BOOL{1};
            },
            BOOL{0});
    }

    BOOL Shim::pageInfo(void *context, const UINT32 page, pvdInfoPage *output) noexcept
    {
        return guarded(
            [&] {
                if (output == nullptr) {
                    return BOOL{0};
                }
                auto *session = borrow(context);
                if (session == nullptr) {
                    return BOOL{0};
                }
                const auto info = session->pageInfo(page);
                if (!info) {
                    return BOOL{0};
                }
                output->lWidth = info->width;
                output->lHeight = info->height;
                output->nBPP = info->bitsPerPixel;
                output->lFrameTime = info->frameTimeMs;
                return BOOL{1};
            },
            BOOL{0});
    }

    BOOL Shim::pageDecode(void *context, const UINT32 page, pvdInfoDecode *output, const pvdDecodeCallback callback,
                          void *callbackContext) noexcept
    {
        return guarded(
            [&] {
                if (output == nullptr) {
                    return BOOL{0};
                }
                auto *session = borrow(context);
                if (session == nullptr) {
                    return BOOL{0};
                }
                const Progress progress =
                    callback == nullptr
                        ? Progress{}
                        : Progress{[callback, callbackContext](const std::uint32_t step, const std::uint32_t steps) {
                              return callback(callbackContext, step, steps) != BOOL{0};
                          }};
                const auto decoded = session->decodePage(page, progress);
                if (!decoded) {
                    return BOOL{0};
                }
                // lImagePitch is a signed 32-bit value whose sign selects the row order; a pitch that
                // does not fit would wrap into a bottom-up pitch, so it is refused (and the page is
                // released, because the host will never see a pointer to free).
                if (decoded->pitchBytes > static_cast<std::uint32_t>(std::numeric_limits<INT32>::max())) {
                    static_cast<void>(session->freePage(decoded->pixels));
                    return BOOL{0};
                }
                output->pImage = const_cast<BYTE *>(reinterpret_cast<const BYTE *>(decoded->pixels.data()));
                output->pPalette = nullptr;
                output->Flags = (decoded->hasAlpha ? UINT32{PVD_IDF_ALPHA} : UINT32{0}) |
                                (static_cast<UINT32>(decoded->hostOrientation) << PVD_IDF_ORIENTATION_SHIFT);
                output->nBPP = decoded->bitsPerPixel;
                output->nColorsUsed = 0;
                output->lImagePitch = static_cast<INT32>(decoded->pitchBytes);
                return BOOL{1};
            },
            BOOL{0});
    }

    void Shim::pageFree(void *context, pvdInfoDecode *decoded) noexcept
    {
        guarded([&] {
            if (decoded == nullptr || decoded->pImage == nullptr) {
                return;
            }
            auto *session = borrow(context);
            if (session == nullptr) {
                return;
            }
            const auto pixels =
                std::span<const std::byte>{reinterpret_cast<const std::byte *>(decoded->pImage), std::size_t{0}};
            static_cast<void>(session->freePage(pixels));
        });
    }

    void Shim::fileClose(void *context) noexcept
    {
        guarded([&] { fromHost(context).reset(); });
    }

} // namespace pvdkit::pvd
