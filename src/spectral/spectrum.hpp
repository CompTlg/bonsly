#pragma once

#include <lightwave/color.hpp>

namespace lightwave
{
    class Spectrum
    {
    public:
        Spectrum();
        ~Spectrum();
    };

    /// @brief The visible range used for all spectral computations, in nanometres.
    inline constexpr float LambdaMin = 360.f;
    inline constexpr float LambdaMax = 830.f;

    /**
     * @brief The CIE 1931 2-degree standard observer: the colour matching functions (x, y, z) at a
     * wavelength given in nanometres. Linearly interpolated between the samples of a table with 1 nm
     * spacing, and (0, 0, 0) outside of [LambdaMin, LambdaMax]. Note that y peaks at 1 for 555 nm.
     * @note The returned Color is only a container for three numbers, not an RGB color.
     */
    Color cie1931(float lambdaNm);

    /**
     * @brief Relative spectral power of the CIE standard illuminant D65 at a wavelength given in
     * nanometres, linearly interpolated, and 0 outside of [LambdaMin, LambdaMax]. Only ratios matter:
     * the absolute scale of your table is up to you, but use the same table everywhere.
     */
    float illuminantD65(float lambdaNm);

    /// @brief Converts CIE XYZ to linear sRGB (IEC 61966-2-1, D65 white point).
    Color xyzToLinearSRGB(const Color &xyz);
}
