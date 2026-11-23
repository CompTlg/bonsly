#include <spectral/uplifting.hpp>
#include <spectral/spectrum.hpp>
#include <lightwave.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#ifndef LW_SPECTRAL_TABLE_DIR
#error "LW_SPECTRAL_TABLE_DIR must be defined by the build system (see CMakeLists.txt)"
#endif

namespace lightwave
{
    namespace
    {
        /**
         * The precomputed coefficient table of Jakob and Hanika, "A Low-Dimensional Function Space for
         * Efficient Spectral Upsampling" (2019), for sRGB. For every color on a grid it stores the three
         * coefficients of the spectrum sigmoid(a*l^2 + b*l + c), with the wavelength l in nanometres.
         *
         * File layout (little endian): the characters "SPEC", the grid resolution as uint32, `resolution`
         * floats with the positions of the grid along the brightness axis, and then
         * 3 * resolution^3 * 3 floats: for each choice of the largest rgb component, for each brightness,
         * for each of the two other (rescaled) components, the 3 coefficients.
         */
        class CoefficientTable
        {
        public:
            static const CoefficientTable &instance()
            {
                static const CoefficientTable table(std::string(LW_SPECTRAL_TABLE_DIR) + "/srgb.coeff");
                return table;
            }

            /// @brief The coefficients { quadratic, linear, offset } for a color with components in [0,1].
            std::array<float, 3> fetch(const Color &rgb) const
            {
                // The table is organised by the largest component, so that the other two, divided by
                // it, always lie in [0, 1].
                int largest = 0;
                for (int channel = 1; channel < 3; channel++)
                    if (rgb[channel] >= rgb[largest])
                        largest = channel;

                const int last = int(m_resolution) - 1;
                const float brightness = rgb[largest];
                const float toGrid = float(last) / brightness;
                const float x = rgb[(largest + 1) % 3] * toGrid;
                const float y = rgb[(largest + 2) % 3] * toGrid;

                // The grid cell, and how far inside of it the color lies, along each axis.
                const int xCell = std::min(int(x), last - 1);
                const int yCell = std::min(int(y), last - 1);
                const int zCell = findBrightnessCell(brightness);
                const float xFraction = x - float(xCell);
                const float yFraction = y - float(yCell);
                const float zFraction = (brightness - m_scale[zCell]) / (m_scale[zCell + 1] - m_scale[zCell]);

                // Trilinear interpolation between the 8 corners of the cell.
                const size_t xStep = 3;
                const size_t yStep = 3 * size_t(m_resolution);
                const size_t zStep = 3 * size_t(m_resolution) * m_resolution;
                const size_t base = ((size_t(largest) * m_resolution + zCell) * m_resolution + yCell) * m_resolution + xCell;

                std::array<float, 3> coefficients;
                for (int k = 0; k < 3; k++)
                {
                    const float *corner = &m_data[base * 3 + k];
                    const float lowZ = (corner[0] * (1 - xFraction) + corner[xStep] * xFraction) * (1 - yFraction)
                                     + (corner[yStep] * (1 - xFraction) + corner[yStep + xStep] * xFraction) * yFraction;
                    const float *upper = corner + zStep;
                    const float highZ = (upper[0] * (1 - xFraction) + upper[xStep] * xFraction) * (1 - yFraction)
                                      + (upper[yStep] * (1 - xFraction) + upper[yStep + xStep] * xFraction) * yFraction;
                    coefficients[k] = lowZ * (1 - zFraction) + highZ * zFraction;
                }
                return coefficients;
            }

        private:
            explicit CoefficientTable(const std::string &path)
            {
                std::ifstream file(path, std::ios::binary);
                if (!file)
                    lightwave_throw("cannot open the spectrum table '%s'", path);

                char magic[4];
                file.read(magic, 4);
                file.read(reinterpret_cast<char *>(&m_resolution), sizeof(m_resolution));
                if (!file || std::string(magic, 4) != "SPEC" || m_resolution < 2 || m_resolution > 1024)
                    lightwave_throw("'%s' is not a valid spectrum table", path);

                m_scale.resize(m_resolution);
                m_data.resize(size_t(3) * m_resolution * m_resolution * m_resolution * 3);
                file.read(reinterpret_cast<char *>(m_scale.data()), std::streamsize(m_scale.size() * sizeof(float)));
                file.read(reinterpret_cast<char *>(m_data.data()), std::streamsize(m_data.size() * sizeof(float)));
                if (!file)
                    lightwave_throw("the spectrum table '%s' is truncated", path);
            }

            /// @brief The index of the cell [m_scale[i], m_scale[i+1]] that contains the brightness.
            int findBrightnessCell(float brightness) const
            {
                const auto firstAbove = std::upper_bound(m_scale.begin() + 1, m_scale.end() - 1, brightness);
                return int(firstAbove - m_scale.begin()) - 1;
            }

            uint32_t m_resolution = 0;
            std::vector<float> m_scale;
            std::vector<float> m_data;
        };
    }

    AlbedoSpectrum::AlbedoSpectrum(const Color &rgb)
    {
        // The sigmoid never reaches exactly 0 or 1, so a component of exactly 0 or 1 would need infinite
        // coefficients. Keep the color a tiny bit inside the gamut.
        constexpr float Margin = 5e-4f;
        Color clamped;
        for (int i = 0; i < 3; i++)
            clamped[i] = std::clamp(rgb[i], Margin, 1.f - Margin);

        if (clamped[0] == clamped[1] && clamped[1] == clamped[2])
        {
            // A grey is a constant spectrum. This is exact, and the table cannot be used for it (it is
            // indexed by color / brightness, which is undefined for black).
            const float grey = clamped[0];
            m_offset = (grey - 0.5f) / std::sqrt(grey * (1.f - grey));
            return;
        }

        const std::array<float, 3> coefficients = CoefficientTable::instance().fetch(clamped);
        m_quadratic = coefficients[0];
        m_linear = coefficients[1];
        m_offset = coefficients[2];
    }

    float AlbedoSpectrum::operator()(float lambdaNm) const
    {
        const double lambda = lambdaNm;
        const double x = (double(m_quadratic) * lambda + double(m_linear)) * lambda + double(m_offset);
        return float(0.5 + x / (2.0 * std::sqrt(1.0 + x * x)));
    }

    IlluminantSpectrum::IlluminantSpectrum(const Color &rgb)
        : m_scale(2.f * std::max({ rgb[0], rgb[1], rgb[2] })),
          m_albedo(m_scale > 0.f ? rgb / m_scale : Color(0.f))
    {
    }

    float IlluminantSpectrum::operator()(float lambdaNm) const
    {
        return m_scale * m_albedo(lambdaNm) * illuminantD65(lambdaNm);
    }
}
