#pragma once

// The test-side reference of the EXR colour path, written independently of src/core/colour in
// double precision from the standards' equations: scene-linear values -> straight alpha -> nits
// -> 16-bit PQ codes (what the adapter hands the core), then what the shared presentation makes
// of those codes -> linear nits -> the file's primaries to BT.709 (RGB to XYZ, Bradford to D65,
// XYZ to BT.709) -> BT.2390 EETF on maxRGB for a 100-nit display -> sRGB OETF -> 16-bit codes
// (what the host receives). The adapter tests compare the PQ codes, the e2e tests the host codes.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "support/ReferencePixels.hpp"

namespace pvdkit::exr::tests
{

    struct Rgba16
    {
        std::uint16_t b;
        std::uint16_t g;
        std::uint16_t r;
        std::uint16_t a;
    };

    inline double sanitized(const double value)
    {
        return value > 0.0 ? value : 0.0; // NaN, negatives and -inf are black; +inf stays
    }

    // SMPTE ST 2084 in double precision.
    inline double pqEncode(const double nits)
    {
        constexpr double m1 = 2610.0 / 16384.0;
        constexpr double m2 = 2523.0 / 4096.0 * 128.0;
        constexpr double c1 = 3424.0 / 4096.0;
        constexpr double c2 = 2413.0 / 4096.0 * 32.0;
        constexpr double c3 = 2392.0 / 4096.0 * 32.0;
        const double y = std::pow(std::clamp(nits, 0.0, 10'000.0) / 10'000.0, m1);
        return std::pow((c1 + c2 * y) / (1.0 + c3 * y), m2);
    }

    inline double pqDecode(const double code)
    {
        constexpr double m1 = 2610.0 / 16384.0;
        constexpr double m2 = 2523.0 / 4096.0 * 128.0;
        constexpr double c1 = 3424.0 / 4096.0;
        constexpr double c2 = 2413.0 / 4096.0 * 32.0;
        constexpr double c3 = 2392.0 / 4096.0 * 32.0;
        const double raised = std::pow(std::clamp(code, 0.0, 1.0), 1.0 / m2);
        return 10'000.0 * std::pow(std::max(raised - c1, 0.0) / (c2 - c3 * raised), 1.0 / m1);
    }

    inline std::uint16_t code16(const double unit)
    {
        return static_cast<std::uint16_t>(std::lround(std::clamp(unit, 0.0, 1.0) * 65'535.0));
    }

    /// Straight-alpha nits of one raw sample: the plugin's exposure and alpha rules.
    struct Nits
    {
        double r;
        double g;
        double b;
        double alpha; // 0..1
    };

    inline Nits nitsOf(const ReferenceSample &sample, const ReferenceFixture &fixture)
    {
        if (!sample.inside) {
            return Nits{0.0, 0.0, 0.0, fixture.alpha ? 0.0 : 1.0};
        }
        double r = sanitized(sample.r) * fixture.whiteNits;
        double g = fixture.grey ? r : sanitized(sample.g) * fixture.whiteNits;
        double b = fixture.grey ? r : sanitized(sample.b) * fixture.whiteNits;
        double alpha = 1.0;
        if (fixture.alpha) {
            alpha = std::min(sanitized(sample.a), 1.0);
            if (alpha > 0.0) {
                r /= alpha;
                g /= alpha;
                b /= alpha;
            }
        }
        return Nits{r, g, b, alpha};
    }

    /// The BGRA64 PQ codes the adapter is expected to produce for one sample.
    inline Rgba16 expectedPqCodes(const ReferenceSample &sample, const ReferenceFixture &fixture)
    {
        const auto nits = nitsOf(sample, fixture);
        return Rgba16{code16(pqEncode(nits.b)), code16(pqEncode(nits.g)), code16(pqEncode(nits.r)), code16(nits.alpha)};
    }

    using Matrix = std::array<std::array<double, 3>, 3>;

    inline Matrix multiply(const Matrix &a, const Matrix &b)
    {
        Matrix result{};
        for (std::size_t i = 0; i < 3; ++i) {
            for (std::size_t j = 0; j < 3; ++j) {
                for (std::size_t k = 0; k < 3; ++k) {
                    result[i][j] += a[i][k] * b[k][j];
                }
            }
        }
        return result;
    }

    inline std::array<double, 3> applyMatrix(const Matrix &m, const std::array<double, 3> &v)
    {
        return {m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2], m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2],
                m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2]};
    }

    inline Matrix inverse(const Matrix &m)
    {
        const double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
                           m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                           m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
        return {{{(m[1][1] * m[2][2] - m[1][2] * m[2][1]) / det, (m[0][2] * m[2][1] - m[0][1] * m[2][2]) / det,
                  (m[0][1] * m[1][2] - m[0][2] * m[1][1]) / det},
                 {(m[1][2] * m[2][0] - m[1][0] * m[2][2]) / det, (m[0][0] * m[2][2] - m[0][2] * m[2][0]) / det,
                  (m[0][2] * m[1][0] - m[0][0] * m[1][2]) / det},
                 {(m[1][0] * m[2][1] - m[1][1] * m[2][0]) / det, (m[0][1] * m[2][0] - m[0][0] * m[2][1]) / det,
                  (m[0][0] * m[1][1] - m[0][1] * m[1][0]) / det}}};
    }

    /// RGB-to-XYZ of a chromaticity set (r.x, r.y, g.x, g.y, b.x, b.y, w.x, w.y) in the column form
    /// (Poynton, Imf::RGBtoXYZ): the columns are the primaries' (x, y, 1 - x - y), scaled so that
    /// RGB (1, 1, 1) is the white at Y = 1; nothing divides by a primary's y, so the CIE XYZ set
    /// (two primaries at y = 0) is as valid as any other.
    inline Matrix rgbToXyz(const std::array<double, 8> &c)
    {
        const Matrix columns{
            {{c[0], c[2], c[4]}, {c[1], c[3], c[5]}, {1.0 - c[0] - c[1], 1.0 - c[2] - c[3], 1.0 - c[4] - c[5]}}};
        const std::array<double, 3> xw{c[6] / c[7], 1.0, (1.0 - c[6] - c[7]) / c[7]};
        const auto s = applyMatrix(inverse(columns), xw);
        Matrix result{};
        for (std::size_t row = 0; row < 3; ++row) {
            for (std::size_t column = 0; column < 3; ++column) {
                result[row][column] = columns[row][column] * s[column];
            }
        }
        return result;
    }

    /// The linear matrix from the fixture's primaries to BT.709 with Bradford adaptation to D65,
    /// except for the equal-energy white E (within 1e-3), which declares no viewing white: those
    /// values are absolute tristimulus and pass through unadapted (the plugin's rule, DESIGN.md 4).
    inline Matrix toBt709(const std::array<double, 8> &chromaticities)
    {
        constexpr std::array<double, 8> bt709{0.640, 0.330, 0.300, 0.600, 0.150, 0.060, 0.3127, 0.3290};
        constexpr Matrix bradford{{{0.8951, 0.2664, -0.1614}, {-0.7502, 1.7135, 0.0367}, {0.0389, -0.0685, 1.0296}}};
        const auto source = rgbToXyz(chromaticities);
        if (std::abs(chromaticities[6] - 1.0 / 3.0) <= 1.0e-3 && std::abs(chromaticities[7] - 1.0 / 3.0) <= 1.0e-3) {
            return multiply(inverse(rgbToXyz(bt709)), source);
        }
        const std::array<double, 3> sourceWhite{chromaticities[6] / chromaticities[7], 1.0,
                                                (1.0 - chromaticities[6] - chromaticities[7]) / chromaticities[7]};
        const std::array<double, 3> d65{bt709[6] / bt709[7], 1.0, (1.0 - bt709[6] - bt709[7]) / bt709[7]};
        const auto sourceCone = applyMatrix(bradford, sourceWhite);
        const auto targetCone = applyMatrix(bradford, d65);
        const Matrix scale{{{targetCone[0] / sourceCone[0], 0.0, 0.0},
                            {0.0, targetCone[1] / sourceCone[1], 0.0},
                            {0.0, 0.0, targetCone[2] / sourceCone[2]}}};
        const auto adaptation = multiply(multiply(inverse(bradford), scale), bradford);
        return multiply(multiply(inverse(rgbToXyz(bt709)), adaptation), source);
    }

    /// BT.2390 EETF (Annex 5 of BT.2408) for a 100-nit / 0.005-nit display, on maxRGB.
    inline std::array<double, 3> toneMap(const std::array<double, 3> &nits, const double peakNits)
    {
        const double sourceBlack = pqEncode(0.0);
        const double span = pqEncode(std::clamp(peakNits, 1.0, 10'000.0)) - sourceBlack;
        const double minLum = (pqEncode(0.005) - sourceBlack) / span;
        const double maxLum = (pqEncode(100.0) - sourceBlack) / span;
        const double knee = 1.5 * maxLum - 0.5;
        const auto mapNits = [&](const double value) {
            const double e1 = std::clamp((pqEncode(value) - sourceBlack) / span, 0.0, 1.0);
            double e2 = e1;
            if (knee < 1.0 && e1 >= knee) {
                const double t = (e1 - knee) / (1.0 - knee);
                e2 = (2.0 * t * t * t - 3.0 * t * t + 1.0) * knee + (t * t * t - 2.0 * t * t + t) * (1.0 - knee) +
                     (-2.0 * t * t * t + 3.0 * t * t) * maxLum;
            }
            const double distance = 1.0 - e2;
            const double e3 = e2 + minLum * distance * distance * distance * distance;
            return std::clamp(pqDecode(e3 * span + sourceBlack) / 100.0, 0.0, 1.0);
        };
        const double driving = std::max({nits[0], nits[1], nits[2]});
        if (driving <= 0.0) {
            return {0.005 / 100.0, 0.005 / 100.0, 0.005 / 100.0};
        }
        const double scale = mapNits(driving) / driving;
        return {nits[0] * scale, nits[1] * scale, nits[2] * scale};
    }

    inline double srgbOetf(const double linear)
    {
        return linear <= 0.0031308 ? 12.92 * linear : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
    }

    /// The host pixel the shared presentation is expected to make of the adapter's PQ codes for
    /// one sample: through the real 16-bit codes so the quantisation the plugin performs is part
    /// of the reference, with the fixture's primaries and the tone-mapping peak the generator
    /// computed.
    inline Rgba16 expectedHostPixel(const ReferenceSample &sample, const ReferenceFixture &fixture)
    {
        const auto codes = expectedPqCodes(sample, fixture);
        const std::array<double, 3> nits{pqDecode(codes.r / 65'535.0), pqDecode(codes.g / 65'535.0),
                                         pqDecode(codes.b / 65'535.0)};
        const auto bt709 = applyMatrix(toBt709(fixture.chromaticities), nits);
        const auto mapped = toneMap(bt709, fixture.peakNits);
        return Rgba16{code16(srgbOetf(std::clamp(mapped[2], 0.0, 1.0))),
                      code16(srgbOetf(std::clamp(mapped[1], 0.0, 1.0))),
                      code16(srgbOetf(std::clamp(mapped[0], 0.0, 1.0))), codes.a};
    }

} // namespace pvdkit::exr::tests
