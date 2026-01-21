#include <lightwave.hpp>

namespace lightwave {

class CheckerboardTexture : public Texture {
    Color color0 = Color(0);
    Color color1 = Color(1);
    Vector2 scale;

public:
    CheckerboardTexture(const Properties &properties) {
        color0 = properties.get<Color>("color0", Color(0));
        color1 = properties.get<Color>("color1", Color(1));
        scale  = properties.get<Vector2>("scale");
    }

    Color evaluate(const Point2 &uv) const override {
        // rescale the uv coordinate values
        int xchecker = fmod(floor(uv.x() * scale.x()), 2);
        int ychecker = fmod(floor(uv.y() * scale.y()), 2);
        if (xchecker == 0) {
            if (ychecker == 0) {
                return color0;
                // return color1;
            }
            return color1;
        }
        if (ychecker == 0) {
            return color1;
        }
        return color0;
        // return color1;
    }

    std::string toString() const override {
        return tfm::format(
            // im not sure if this is even necesssary,
            // just copy from other files
            "CheckerboardTexture[\n"
            "  color0 = %s\n"
            "  color1 = %s\n"
            "  scale = %s\n"
            "]",
            indent(color0),
            (color1),
            (scale));
    }
};

} // namespace lightwave

REGISTER_TEXTURE(CheckerboardTexture, "checkerboard")
