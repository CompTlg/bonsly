#include <lightwave.hpp>

namespace lightwave {

class PointLight final : public Light {
    Point query_point;
    Color power;

public:
    PointLight(const Properties &properties) : Light(properties) {
        query_point = (properties.get<Point>("position"));
        power       = properties.get<Color>("power");
    }

    DirectLightSample sampleDirect(const Point &origin,
                                   Sampler &rng) const override {
        // NOT_IMPLEMENTED
        Vector wi        = Vector(query_point - origin);
        float vector_dot = wi.lengthSquared(); // wi.dot(wi);
        Color intensity  = Inv4Pi * power / vector_dot;
        Color weight     = intensity;

        return DirectLightSample{ wi.normalized(), weight, wi.length() };
    }

    bool canBeIntersected() const override { return false; }

    std::string toString() const override {
        return tfm::format(
            "PointLight[\n"
            "]");
    }
};

} // namespace lightwave

REGISTER_LIGHT(PointLight, "point")
