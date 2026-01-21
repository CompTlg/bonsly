#include <lightwave.hpp>

namespace lightwave {

class NormalIntegrator : public SamplingIntegrator {

    bool remap;

public:
    NormalIntegrator(const Properties &properties)
        : SamplingIntegrator(properties) {
        remap = properties.get<bool>("remap", true);
    }

    Color Li(const Ray &ray, Sampler &rng) override {

        Intersection intersection = m_scene->intersect(ray, rng);
        Vector normal             = intersection.shadingNormal;

        if (remap) { // remap == true, remap normals between [-1,1] to the
                     // interval [0,1]
                     // #TODO // no intersection case in the normal integrator
            normal = (normal + Vector(1.0f, 1.0f, 1.0f)) / 2.0f;

        } else {
            if (!intersection) { // remap == false && intersections occur
                normal = { 0, 0, 0 };
            } else { // remap == false && no intersection
                normal = intersection.wo;
            }
        }
        return Color(normal);
    }

    /// @brief An optional textual representation of this class, which can be
    /// useful for debugging.
    std::string toString() const override {
        return tfm::format(
            "  NormalIntegrator[\n"
            "  sampler = %s,\n"
            "  image = %s,\n"
            "]",
            indent(m_sampler),
            indent(m_image));
    }
};
} // namespace lightwave

REGISTER_INTEGRATOR(NormalIntegrator, "normal")