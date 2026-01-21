#include <lightwave.hpp>

namespace lightwave {

class Pathtracer : public SamplingIntegrator {

    int m_depth;

public:
    Pathtracer(const Properties &properties) : SamplingIntegrator(properties) {

        m_depth = properties.get<int>("depth", 2);
    }

    Color Li(const Ray &ray, Sampler &rng) override {

        // NOT_IMPLEMENTED

        Intersection its;
        BsdfSample bsdf;
        Ray nextRay = ray; // updated iteratively when new bounce occurs

        Color weight = Color(1); //  keeps track the lights along the path
        Color accum  = Color(0); // the final accumulated color to return

        // iterate over all bounces
        for (int i = 0;; i++) {
            its = m_scene->intersect(nextRay, rng);

            Color hit = (weight * its.evaluateEmission().value);
            accum     = accum + hit;

            if (i >= m_depth - 1) {
                break;
            }

            // check if the scene has lights
            if (m_scene->hasLights()) {

                LightSample lightSample = m_scene->sampleLight(rng);

                if (!lightSample.isInvalid()) {
                    if (!lightSample.light->canBeIntersected()) {

                        DirectLightSample directSample =
                            lightSample.light->sampleDirect(its.position, rng);

                        if (!directSample.isInvalid()) {

                            BsdfEval bsdfEval =
                                its.evaluateBsdf(directSample.wi);

                            if (!bsdfEval.invalid()) {

                                // create a ray from current intersection to
                                // lightsource
                                Ray shadowRay(its.position, directSample.wi);
                                // check if intersection between current point
                                // and lightsource is possible s.t. they are
                                // visible by each other
                                bool visible = !m_scene->intersect(
                                    shadowRay, directSample.distance, rng);

                                if (visible) {
                                    Color total_weight =
                                        (weight * bsdfEval.value *
                                         directSample.weight) /
                                        lightSample.probability;
                                    accum += total_weight;
                                }
                            }
                        }
                    }
                }
                // if lightSample can not be intersected
            }
            // object gets hit -> get bsdf sample and the emission value of the
            // hit object
            bsdf = its.sampleBsdf(rng);
            if (bsdf.isInvalid()) {
                return accum;
            }

            weight *= bsdf.weight;
            // sample invalid, return emission color

            // recursively from the current intersection to the next object or
            // to infinity
            nextRay = Ray(its.position, bsdf.wi);
        }

        return accum;
    }

    std::string toString() const override {
        return tfm::format(
            "Pathtracer[\n"
            "  sampler = %s,\n"
            "  image = %s,\n"
            "]",
            indent(m_sampler),
            indent(m_image));
    }
};

} // namespace lightwave

REGISTER_INTEGRATOR(Pathtracer, "pathtracer")
