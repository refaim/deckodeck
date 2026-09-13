#include "core/colour/Transfer.hpp"

#include <algorithm>
#include <cmath>

namespace pvdkit::core::colour::Transfer
{
    namespace
    {

        constexpr float kPqM1 = 2610.0F / 16384.0F;
        constexpr float kPqM2 = (2523.0F / 4096.0F) * 128.0F;
        constexpr float kPqC1 = 3424.0F / 4096.0F;
        constexpr float kPqC2 = (2413.0F / 4096.0F) * 32.0F;
        constexpr float kPqC3 = (2392.0F / 4096.0F) * 32.0F;
        constexpr float kPqPeakNits = 10'000.0F;

        constexpr float kHlgA = 0.17883277F;
        constexpr float kHlgB = 1.0F - 4.0F * kHlgA;
        constexpr float kHlgC = 0.5599107295F;

        float power(const float value, const float exponent) noexcept
        {
            return std::pow(std::max(value, 0.0F), exponent);
        }

    } // namespace

    float srgbToLinear(const float encoded) noexcept
    {
        const auto value = std::max(encoded, 0.0F);
        return value <= 0.04045F ? value / 12.92F : std::pow((value + 0.055F) / 1.055F, 2.4F);
    }

    float linearToSrgb(const float linear) noexcept
    {
        return linear <= 0.0031308F ? 12.92F * linear : 1.055F * std::pow(linear, 1.0F / 2.4F) - 0.055F;
    }

    float bt1886ToLinear(const float encoded) noexcept
    {
        return power(encoded, 2.4F);
    }

    float linearToBt1886(const float linear) noexcept
    {
        return power(linear, 1.0F / 2.4F);
    }

    float gamma22ToLinear(const float encoded) noexcept
    {
        return power(encoded, 2.2F);
    }

    float linearToGamma22(const float linear) noexcept
    {
        return power(linear, 1.0F / 2.2F);
    }

    float gamma28ToLinear(const float encoded) noexcept
    {
        return power(encoded, 2.8F);
    }

    float linearToGamma28(const float linear) noexcept
    {
        return power(linear, 1.0F / 2.8F);
    }

    float linearToLinear(const float encoded) noexcept
    {
        return std::max(encoded, 0.0F);
    }

    float pqToNits(const float encoded) noexcept
    {
        // SMPTE ST 2084 / ITU-R BT.2100 Table 4. The edge handling follows the checked libjxl
        // 0.11.2 and zimg implementations: out-of-domain codes are clamped before the powers.
        const auto code = std::clamp(encoded, 0.0F, 1.0F);
        const auto raised = std::pow(code, 1.0F / kPqM2);
        const auto numerator = std::max(raised - kPqC1, 0.0F);
        const auto denominator = kPqC2 - kPqC3 * raised;
        return kPqPeakNits * std::pow(numerator / denominator, 1.0F / kPqM1);
    }

    float nitsToPq(const float nits) noexcept
    {
        const auto luminance = std::clamp(nits, 0.0F, kPqPeakNits);
        // SMPTE ST 2084 defines the inverse EOTF as one equation over the complete luminance
        // domain. In particular, zero luminance encodes to c1^m2 rather than code zero.
        const auto raised = std::pow(luminance / kPqPeakNits, kPqM1);
        return std::pow((kPqC1 + kPqC2 * raised) / (1.0F + kPqC3 * raised), kPqM2);
    }

    float hlgToScene(const float encoded) noexcept
    {
        // ARIB STD-B67 / ITU-R BT.2100 Table 5. The RGB OOTF cannot be expressed by this scalar
        // function; Pipeline applies it from scene luminance for the 1000-nit reference display.
        const auto code = std::max(encoded, 0.0F);
        return code <= 0.5F ? code * code / 3.0F : (std::exp((code - kHlgC) / kHlgA) + kHlgB) / 12.0F;
    }

    float sceneToHlg(const float scene) noexcept
    {
        const auto value = std::max(scene, 0.0F);
        return value <= (1.0F / 12.0F) ? std::sqrt(3.0F * value) : kHlgA * std::log(12.0F * value - kHlgB) + kHlgC;
    }

    float toLinear(const std::uint16_t characteristics, const float encoded) noexcept
    {
        switch (characteristics) {
        case 1:
        case 6:
        case 14:
        case 15:
            return bt1886ToLinear(encoded);
        case 4:
            return gamma22ToLinear(encoded);
        case 5:
            return gamma28ToLinear(encoded);
        case 8:
            return linearToLinear(encoded);
        case 16:
            return pqToNits(encoded);
        case 18:
            return hlgToScene(encoded);
        case 2:
        case 13:
        default:
            return srgbToLinear(encoded);
        }
    }

    float fromLinear(const std::uint16_t characteristics, const float linear) noexcept
    {
        switch (characteristics) {
        case 1:
        case 6:
        case 14:
        case 15:
            return linearToBt1886(linear);
        case 4:
            return linearToGamma22(linear);
        case 5:
            return linearToGamma28(linear);
        case 8:
            return linearToLinear(linear);
        case 16:
            return nitsToPq(linear);
        case 18:
            return sceneToHlg(linear);
        case 2:
        case 13:
        default:
            return linearToSrgb(linear);
        }
    }

    bool isKnown(const std::uint16_t characteristics) noexcept
    {
        switch (characteristics) {
        case 1:
        case 2:
        case 4:
        case 5:
        case 6:
        case 8:
        case 13:
        case 14:
        case 15:
        case 16:
        case 18:
            return true;
        default:
            return false;
        }
    }

    bool isHdr(const std::uint16_t characteristics) noexcept
    {
        return characteristics == 16 || characteristics == 18;
    }

} // namespace pvdkit::core::colour::Transfer
