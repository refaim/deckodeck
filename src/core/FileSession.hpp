#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/IDecoder.hpp"
#include "core/IFileSource.hpp"
#include "core/PixelBuffer.hpp"
#include "pvd/Plugin.hpp"

namespace pvdkit::core
{

    namespace colour
    {
        class Presentation;
        class SrgbOutputTables;
    } // namespace colour

    class FileSession final : public pvd::IFileSession
    {
      public:
        /// `outputTables` is the plugin-wide instance the composition root owns (ARCHITECTURE section 7);
        /// it is borrowed here and by the session's Presentation, and outlives the session by construction.
        FileSession(std::unique_ptr<IFileData> fileData, std::unique_ptr<IDecoder> decoder, pvd::ImageInfo imageInfo,
                    const DecoderOptions &options, const colour::SrgbOutputTables &outputTables);
        ~FileSession() override;

        FileSession(const FileSession &) = delete;
        FileSession &operator=(const FileSession &) = delete;
        FileSession(FileSession &&) = delete;
        FileSession &operator=(FileSession &&) = delete;

        [[nodiscard]] const pvd::ImageInfo &imageInfo() const override;
        [[nodiscard]] Result<pvd::PageInfo> pageInfo(std::uint32_t page) const override;
        [[nodiscard]] Result<pvd::DecodedPage> decodePage(std::uint32_t page, const pvd::Progress &progress) override;
        bool freePage(std::span<const std::byte> pixels) override;
        /// The borrowed tables, exposed so a test can pin that every session of one plugin shares them.
        [[nodiscard]] const colour::SrgbOutputTables &outputTables() const noexcept;

      private:
        std::unique_ptr<IFileData> fileData_;
        std::unique_ptr<IDecoder> decoder_;
        pvd::ImageInfo imageInfo_;
        std::vector<std::unique_ptr<PixelBuffer>> outstanding_;
        DecoderOptions options_;
        const colour::SrgbOutputTables &outputTables_;
        std::unique_ptr<colour::Presentation> presentation_;
    };

} // namespace pvdkit::core
