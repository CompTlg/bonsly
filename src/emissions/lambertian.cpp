#include <lightwave.hpp>

namespace lightwave {

class Lambertian : public Emission {
    ref<Texture> m_emission;

public:
    Lambertian(const Properties &properties) {
        m_emission = properties.get<Texture>("emission");
    }

    EmissionEval evaluate(const Point2 &uv, const Vector &wo) const override {
        Color c;
        auto cos = Frame::cosTheta(wo);
        if (cos < 0) {
            c = Color::black();
        } else {
            // c = m_emission->evaluate(uv) * clamp(0.f, 1.f, cos);
            c = m_emission->evaluate(uv); // Feedback: no cosine term here
        }

        return EmissionEval{ c };
    }

    std::string toString() const override {
        return tfm::format(
            "Lambertian[\n"
            "  emission = %s\n"
            "]",
            indent(m_emission));
    }
};

} // namespace lightwave

REGISTER_EMISSION(Lambertian, "lambertian")
