#pragma once

#include <cstdint>

namespace pvdkit::core::colour::Transfer
{

    [[nodiscard]] float srgbToLinear(float encoded) noexcept;
    [[nodiscard]] float linearToSrgb(float linear) noexcept;
    [[nodiscard]] float bt1886ToLinear(float encoded) noexcept;
    [[nodiscard]] float linearToBt1886(float linear) noexcept;
    [[nodiscard]] float gamma22ToLinear(float encoded) noexcept;
    [[nodiscard]] float linearToGamma22(float linear) noexcept;
    [[nodiscard]] float gamma28ToLinear(float encoded) noexcept;
    [[nodiscard]] float linearToGamma28(float linear) noexcept;
    [[nodiscard]] float linearToLinear(float encoded) noexcept;
    [[nodiscard]] float pqToNits(float encoded) noexcept;
    [[nodiscard]] float nitsToPq(float nits) noexcept;
    [[nodiscard]] float hlgToScene(float encoded) noexcept;
    [[nodiscard]] float sceneToHlg(float scene) noexcept;

    /// Decodes an H.273 transfer characteristic. SDR and HLG results are normalized linear light;
    /// PQ results are absolute cd/m2. Code 2 and unknown codes deliberately fall back to sRGB.
    [[nodiscard]] float toLinear(std::uint16_t characteristics, float encoded) noexcept;
    [[nodiscard]] float fromLinear(std::uint16_t characteristics, float linear) noexcept;
    [[nodiscard]] bool isKnown(std::uint16_t characteristics) noexcept;
    [[nodiscard]] bool isHdr(std::uint16_t characteristics) noexcept;

} // namespace pvdkit::core::colour::Transfer
