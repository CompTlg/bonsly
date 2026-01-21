#include <lightwave.hpp>

namespace lightwave {

class DirectionalLight final : public Light {
    Vector direction;
    Color power;

public:
    DirectionalLight(const Properties &properties) : Light(properties) {
        direction = properties.get<Vector>("direction").normalized();
        power     = properties.get<Color>("intensity");
    }

    DirectLightSample sampleDirect(const Point &origin,
                                   Sampler &rng) const override {
        return DirectLightSample{ direction, power, Infinity };
    }

    bool canBeIntersected() const override { return false; }

    std::string toString() const override {
        return tfm::format(
            "DirectionalLight[\n"
            "]");
    }
};

} // namespace lightwave

REGISTER_LIGHT(DirectionalLight, "directional")
