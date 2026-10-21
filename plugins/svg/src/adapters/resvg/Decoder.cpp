#include "adapters/resvg/Decoder.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <resvg.h>

#include "adapters/win/Utf8.hpp"
#include "core/Format.hpp"

namespace pvdkit::svg
{
    namespace
    {

        core::Error parseFailure(const std::int32_t status)
        {
            return {core::ErrorCode::ParseFailed,
                    "resvg rejected the SVG input (status " + std::to_string(status) + ")"};
        }

        core::Result<std::string> resourcesDirectory(const std::wstring_view sourcePath)
        {
            const std::filesystem::path parent = std::filesystem::path{std::wstring{sourcePath}}.parent_path();
            return win::wideToUtf8(parent.native());
        }

    } // namespace

    void OptionsDeleter::operator()(resvg_options *options) const noexcept
    {
        resvg_options_destroy(options);
    }

    void TreeDeleter::operator()(resvg_render_tree *tree) const noexcept
    {
        resvg_tree_destroy(tree);
    }

    SvgState::~SvgState() = default;

    core::Result<TreeHandle> SvgState::parse(std::span<const std::byte> data,
                                             const std::optional<std::wstring_view> sourcePath)
    {
        if (data.empty()) {
            return std::unexpected(core::Error{core::ErrorCode::ParseFailed, "the SVG input is empty"});
        }

        std::optional<std::string> resources;
        if (sourcePath) {
            auto directory = resourcesDirectory(*sourcePath);
            if (!directory) {
                return std::unexpected(directory.error());
            }
            resources = std::move(*directory);
        }

        const std::lock_guard lock{mutex_};
        ensureOptions();

        // The options object is shared between parses, so each parse owns the resources directory
        // for its duration. A memory-only input resets it to the default (the current directory).
        resvg_options_set_resources_dir(options_.get(), resources ? resources->c_str() : nullptr);

        TreeHandle tree;
        const std::int32_t status =
            resvg_parse_tree_from_data(reinterpret_cast<const char *>(data.data()),
                                       static_cast<std::uintptr_t>(data.size()), options_.get(), std::out_ptr(tree));
        // The pinned C API returns RESVG_OK only after Box::into_raw has written a non-null tree.
        if (status != RESVG_OK) {
            return std::unexpected(parseFailure(status));
        }
        return tree;
    }

    void SvgState::ensureOptions()
    {
        if (!options_) {
            // resvg_options_create is Box::into_raw(Box::new(..)): null is impossible, Rust aborts
            // first, so no inner check remains; the outer lazy guard and OOM firewall still apply.
            options_.reset(resvg_options_create());
            resvg_options_load_system_fonts(options_.get());
        }
    }

    Decoder::Decoder(Key, TreeHandle tree, RasterPlan plan) noexcept : tree_(std::move(tree)), plan_(std::move(plan))
    {
    }

    const core::ImageMeta &Decoder::meta() const
    {
        return plan_.meta;
    }

    core::Result<core::FrameTiming> Decoder::frameTiming(std::uint32_t frame) const
    {
        if (frame != 0) {
            return std::unexpected(core::Error{core::ErrorCode::PageOutOfRange, "an SVG has exactly one page"});
        }
        return core::FrameTiming{0};
    }

    core::Result<void> Decoder::decodeFrame(std::uint32_t frame, pvd::PixelFormat format,
                                            std::span<std::byte> destination, std::uint32_t pitchBytes)
    {
        if (frame != 0) {
            return std::unexpected(core::Error{core::ErrorCode::PageOutOfRange, "an SVG has exactly one page"});
        }

        const auto &meta = plan_.meta;
        return checkDestination(meta, format, destination.size(), pitchBytes).and_then([&]() -> core::Result<void> {
            // tiny-skia writes premultiplied RGBA8; the core conversion applies straight alpha
            // and the host's channel order. The plan already proved the byte count.
            std::vector<std::byte> premultiplied(plan_.rasterBytes);
            resvg_render(tree_.get(), resvg_transform_identity(), meta.width, meta.height,
                         reinterpret_cast<char *>(premultiplied.data()));

            convertPremultiplied(premultiplied, meta.width, meta.height, format, destination, pitchBytes);
            return core::Result<void>{};
        });
    }

    bool DecoderFactory::recognises(std::span<const std::byte> head) const
    {
        return svg::recognises(head);
    }

    core::Result<std::unique_ptr<core::IDecoder>> DecoderFactory::create(std::span<const std::byte> file,
                                                                         const core::DecoderOptions &options)
    {
        auto parsed = state_.parse(file, options.sourcePath);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }

        const resvg_size size = resvg_get_image_size(parsed->get());
        auto plan = planRaster(size.width, size.height, options);
        if (!plan) {
            return std::unexpected(plan.error());
        }
        plan->meta.compression = svg::isGzip(file) ? "GZip" : "None";

        return std::unique_ptr<core::IDecoder>{
            std::make_unique<Decoder>(Decoder::Key{}, std::move(*parsed), std::move(*plan))};
    }

} // namespace pvdkit::svg
