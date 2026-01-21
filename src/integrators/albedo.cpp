#include <lightwave.hpp>

namespace lightwave {

class AlbedoIntegrator : public SamplingIntegrator {

public:
    AlbedoIntegrator(const Properties &properties)
        : SamplingIntegrator(properties) {}

    Color Li(const Ray &ray, Sampler &rng) override {

        Intersection intersection = m_scene->intersect(ray, rng);

        // if no intersection, return background color
        if (!intersection) {
            return intersection.evaluateEmission().value;
        }

        // if (!intersection || !intersection.instance->bsdf()){
        //     return Color(0.f);
        // }

        // return albedo evaluated at uv
        return Color(intersection.wo); // ???????
    }

    std::string toString() const override {
        return tfm::format(
            "AlbedoIntegrator[\n"
            " sampler = %s,\n"
            " image =%s, \n"
            "]",
            indent(m_sampler),
            indent(m_image));
    }
};
} // namespace lightwave

REGISTER_INTEGRATOR(AlbedoIntegrator, "albedo")