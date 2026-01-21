#include <lightwave.hpp>

#include <vector>

namespace lightwave {

class EnvironmentMap final : public BackgroundLight {
    /// @brief The texture to use as background
    ref<Texture> m_texture;
    /// @brief An optional transform from local-to-world space
    ref<Transform> m_transform;

public:
    EnvironmentMap(const Properties &properties) : BackgroundLight(properties) {
        m_texture   = properties.getChild<Texture>();
        m_transform = properties.getOptionalChild<Transform>();
    }

    EmissionEval evaluate(const Vector &direction) const override {
        Point2 warped = Point2(0);

        // goal: light direciton -> image coords

        Vector transfDirection;

        // inverse transformation seems to be the only one works
        if (m_transform) {
            transfDirection = m_transform->inverse(direction).normalized();
        } else {
            transfDirection = direction;
        }

        // spherical coords -> texture coords
        // remap from [x,y] => [phi, theta] = [-pi;pi] x [0;pi] to [0;1]^2
        // maps from the point samples to its left lower coordinates index
        // same formula as sphere.cpp
        warped.x() =
            0.5 + atan2(-transfDirection.z(), transfDirection.x()) * Inv2Pi;
        warped.y() = -0.5 + asin(-transfDirection.y()) * InvPi;

        // hints:
        // * if (m_transform) { transform direction vector from world to local
        // coordinates }
        // * find the corresponding pixel coordinate for the given local
        // direction
        // * make use of std::atan2 instead of tangent function.
        // * check out the safe versions of sine and cosine, e.g. safe_acos
        // in math.hpp to avoid problematic edge cases
        return {
            .value = m_texture->evaluate(warped),
        };
    }

    DirectLightSample sampleDirect(const Point &origin,
                                   Sampler &rng) const override {
        Point2 warped    = rng.next2D();
        Vector direction = squareToUniformSphere(warped);
        auto E           = evaluate(direction);

        // implement better importance sampling here, if you ever need it
        // (useful for environment maps with bright tiny light sources, like the
        // sun for example)

        return {
            .wi       = direction,
            .weight   = E.value * Inv4Pi,
            .distance = Infinity,
        };
    }

    std::string toString() const override {
        return tfm::format(
            "EnvironmentMap[\n"
            "  texture = %s,\n"
            "  transform = %s\n"
            "]",
            indent(m_texture),
            indent(m_transform));
    }
};

} // namespace lightwave

REGISTER_LIGHT(EnvironmentMap, "envmap")
