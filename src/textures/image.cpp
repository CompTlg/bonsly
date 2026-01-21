#include <lightwave.hpp>

namespace lightwave {

class ImageTexture : public Texture {
    enum class BorderMode {
        Clamp,
        Repeat,
    };

    enum class FilterMode {
        Nearest,
        Bilinear,
    };

    ref<Image> m_image;
    float m_exposure;
    BorderMode m_border;
    FilterMode m_filter;

public:
    ImageTexture(const Properties &properties) {
        if (properties.has("filename")) {
            m_image = std::make_shared<Image>(properties);
        } else {
            m_image = properties.getChild<Image>();
        }
        m_exposure = properties.get<float>("exposure", 1);

        // clang-format off
        m_border = properties.getEnum<BorderMode>("border", BorderMode::Repeat, {
            { "clamp", BorderMode::Clamp },
            { "repeat", BorderMode::Repeat },
        });

        m_filter = properties.getEnum<FilterMode>("filter", FilterMode::Bilinear, {
            { "nearest", FilterMode::Nearest },
            { "bilinear", FilterMode::Bilinear },
        });
        // clang-format on
    }

    //   ------> x  |
    //              |
    //              |
    //              v y
    Color evaluate(const Point2 &uv) const override {
        return evalFilterMode(Point2(uv.x(), 1 - uv.y()));
    }

    inline Color evalFilterMode(const Point2 &uv) const {
        Point2 scaled = Point2(uv.x() * m_image->resolution().x(),
                               uv.y() * m_image->resolution().y());

        Color endColor;

        if (m_filter == FilterMode::Nearest) {
            // we floor the scaled coordinates to get nearest pixel
            Point2i floored = Point2i(floor(scaled.x()), floor(scaled.y()));
            endColor = m_image->get(evalBorderMode(floored)) * m_exposure;

        } else if (m_filter == FilterMode::Bilinear) {

            // (Chat GPT)
            Point2i floored(floor(scaled.x() - 0.5f), floor(scaled.y() - 0.5f));

            Point2i p00 = evalBorderMode(Point2i(floored.x(), floored.y()));
            Point2i p10 = evalBorderMode(Point2i(floored.x(), floored.y() + 1));
            Point2i p01 = evalBorderMode(Point2i(floored.x() + 1, floored.y()));
            Point2i p11 =
                evalBorderMode(Point2i(floored.x() + 1, floored.y() + 1));

            float fu = (scaled.x() - 0.5f - floor(scaled.x() - 0.5f));
            float fv = (scaled.y() - 0.5f - floor(scaled.y() - 0.5f));

            endColor = ((1 - fu) * (1 - fv) * m_image->get(p00) +
                        (1 - fu) * fv * m_image->get(p10) +
                        fu * (1 - fv) * m_image->get(p01) +
                        fu * fv * m_image->get(p11)) *
                       m_exposure;
        }
        return endColor;
    }

    inline Point2i evalBorderMode(const Point2i &imageCoords) const {

        int x; // the coordinate we return in the end
        int y;

        if (m_border == BorderMode::Repeat) {
            // imageCoords might exceed the border
            // x = ((imageCoords.x() % m_image->resolution().x()) +
            //      m_image->resolution().x()) %
            //     m_image->resolution().x();
            // y = ((imageCoords.y() % m_image->resolution().y()) +
            //      m_image->resolution().y()) %
            //     m_image->resolution().y();
            //  use a custom modulo function here that supports negative values
            x = fmod(fmod(imageCoords.x(), m_image->resolution().x()) +
                         m_image->resolution().x(),
                     m_image->resolution().x());
            y = fmod(fmod(imageCoords.y(), m_image->resolution().y()) +
                         m_image->resolution().y(),
                     m_image->resolution().y());

        } else if (m_border == BorderMode::Clamp) {
            // map between [-inf, inf]^2 -> [0,1]^2
            x = clamp(imageCoords.x(), 0, m_image->resolution().x() - 1);
            y = clamp(imageCoords.y(), 0, m_image->resolution().y() - 1);
        }
        return Point2i(x, y);
    }

    std::string toString() const override {
        return tfm::format(
            "ImageTexture[\n"
            "  image = %s,\n"
            "  exposure = %f,\n"
            "]",
            indent(m_image),
            m_exposure);
    }
};

} // namespace lightwave

REGISTER_TEXTURE(ImageTexture, "image")
