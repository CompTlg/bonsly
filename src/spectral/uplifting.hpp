#pragma once

#include <lightwave/color.hpp>

namespace lightwave
{
    /**
     * @brief Reflectance spectrum for a linear sRGB color with all components in [0,1].
     * Lit by CIE D65 and integrated against the colour matching functions, it must give back the
     * original color: with
     *   Y_n = sum_l  cie1931(l).y * illuminantD65(l)
     *   xyz = sum_l  (*this)(l) * illuminantD65(l) * cie1931(l) / Y_n         (l = 360, 361, ..., 830 nm)
     * the color xyzToLinearSRGB(xyz) is equal to the rgb passed to the constructor. Values stay in [0,1],
     * grey colors give a constant spectrum, and the spectrum varies continuously with the color.
     */
    class AlbedoSpectrum
    {
    public:
        explicit AlbedoSpectrum(const Color &rgb);
        /// @brief The reflectance at a wavelength given in nanometres.
        float operator()(float lambdaNm) const;

    private:
        // spectrum = sigmoid(m_quadratic * l^2 + m_linear * l + m_offset), with l the wavelength in nanometres
        float m_quadratic = 0.f;
        float m_linear = 0.f;
        float m_offset = 0.f;
    };

    /**
     * @brief Emission spectrum for a linear sRGB color with non-negative components of any magnitude
     * (e.g. the radiance of a lamp). The spectrum already contains the illuminant, so with Y_n as above
     *   xyz = sum_l  (*this)(l) * cie1931(l) / Y_n
     * the color xyzToLinearSRGB(xyz) is equal to the rgb passed to the constructor. In particular
     * (1,1,1) gives illuminantD65 itself, and scaling the color scales the spectrum.
     */
    class IlluminantSpectrum
    {
    public:
        explicit IlluminantSpectrum(const Color &rgb);
        /// @brief The spectral power at a wavelength given in nanometres.
        float operator()(float lambdaNm) const;

    private:
        // m_scale must be declared before m_albedo: the constructor uses it to initialise m_albedo.
        float m_scale;
        AlbedoSpectrum m_albedo;
    };
}
