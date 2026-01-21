#include <lightwave/core.hpp>
#include <lightwave/instance.hpp>
#include <lightwave/registry.hpp>
#include <lightwave/sampler.hpp>

namespace lightwave {

void Instance::transformFrame(SurfaceEvent &surf, const Vector &wo) const {
    surf.tangent = m_transform->apply(surf.tangent).normalized();
    if (m_normal != NULL) {
        auto colortonorm = m_normal.get()->evaluate(surf.uv).data();
        // I get the colours here, to change from (0, 1) to (-1, 1)
        // Tutor said lightwave has 0 to 1 for a colour.
        Vector norm;
        norm.x()           = colortonorm[0] * 2 - 1;
        norm.y()           = colortonorm[1] * 2 - 1;
        norm.z()           = colortonorm[2] * 2 - 1;
        norm               = surf.shadingFrame().toWorld(norm);
        surf.shadingNormal = norm;
    }
    surf.shadingNormal =
        m_transform->applyNormal(surf.shadingNormal).normalized();
    surf.geometryNormal =
        m_transform->applyNormal(surf.geometryNormal).normalized();
    surf.position = m_transform->apply(surf.position);
}

inline void validateIntersection(const Intersection &its) {
    // use the following macros to make debugginer easier:
    // * assert_condition(condition, { ... });
    // * assert_normalized(vector, { ... });
    // * assert_ortoghonal(vec1, vec2, { ... });
    // * assert_finite(value or vector or color, { ... });

    // each assert statement takes a block of code to execute when it fails
    // (useful for printing out variables to narrow done what failed)

    assert_finite(its.t, {
        logger(
            EError,
            "  your intersection produced a non-finite intersection distance");
        logger(EError, "  offending shape: %s", its.instance->shape());
    });
    assert_condition(its.t >= Epsilon, {
        logger(EError,
               "  your intersection is susceptible to self-intersections");
        logger(EError, "  offending shape: %s", its.instance->shape());
        logger(EError,
               "  returned t: %.3g (smaller than Epsilon = %.3g)",
               its.t,
               Epsilon);
    });
}

bool Instance::intersect(const Ray &worldRay, Intersection &its,
                         Sampler &rng) const {

    if (!m_transform) {
        // fast path, if no transform is needed
        const Ray localRay        = worldRay;
        const bool wasIntersected = m_shape->intersect(localRay, its, rng);
        if (wasIntersected) {
            its.instance = this;
            validateIntersection(its);
        }
        return (wasIntersected);
    }

    const float previousT = its.t;
    Ray localRay;
    localRay = m_transform->inverse(worldRay); // convert global space ray into
                                               // local space
    // Transform t (distance from origin to closest known intersection) to local
    // space To do so we multiply by whatever value our unit vector got
    // stretched when going to local space
    float tFactor = localRay.direction.length();
    if (tFactor == 0) {
        return false;
    }
    its.t *= tFactor;
    localRay = localRay.normalized();

    // TODO
    // transform ray into local space, finish everything and then call the
    // transform frame

    // hints:
    // * transform the ray (do not forget to normalize!)
    // * how does its.t need to change?

    const bool wasIntersected = m_shape->intersect(localRay, its, rng);
    bool alphaintersected     = true;
    // make it so that the alpha doesnt change the uv coordinates/intersects it
    // or whatever

    if (m_alpha != NULL) {
        Intersection alphaits = its;
        Color alphaint        = m_alpha.get()->evaluate(alphaits.uv);
        float alphavals       = alphaint[0] + alphaint[1] + alphaint[2];
        if (alphavals < rng.next()) {
            alphaintersected = false;
        }

        // TODO: Have to check if there is another intersection after not
        // intersecting with the mesh that it would have intersected without the
        // alpha masking
        // IDEA: Cast the ray again from the intersection that we miss,
        // Make a ray that starts from the intersection and same direction,
        // Do the alpha intersection checker condition again
        // NOTES: Recursive loop? As it might be a wavy mesh, intersecting 6
        // times with the ray??
    }
    if (wasIntersected && alphaintersected) {

        // now its.t is local space (and its new value)
        // now its.normal, its.tangent etc are all local space as well
        //
        its.instance = this;
        validateIntersection(its);
        // hint: how does its.t need to change?
        // TODO: Transform tangent, normal, bitangent, position, t all back to
        // world space
        its.t /= tFactor;
        transformFrame(its, -localRay.direction);
    } else {
        its.t = previousT;
    }

    return (wasIntersected && alphaintersected);
}

Bounds Instance::getBoundingBox() const {
    if (!m_transform) {
        // fast path
        return m_shape->getBoundingBox();
    }

    const Bounds untransformedAABB = m_shape->getBoundingBox();
    if (untransformedAABB.isUnbounded()) {
        return Bounds::full();
    }

    Bounds result;
    for (int point = 0; point < 8; point++) {
        Point p = untransformedAABB.min();
        for (int dim = 0; dim < p.Dimension; dim++) {
            if ((point >> dim) & 1) {
                p[dim] = untransformedAABB.max()[dim];
            }
        }
        p = m_transform->apply(p);
        result.extend(p);
    }
    return result;
}

Point Instance::getCentroid() const {
    if (!m_transform) {
        // fast path
        return m_shape->getCentroid();
    }

    return m_transform->apply(m_shape->getCentroid());
}

AreaSample Instance::sampleArea(Sampler &rng) const {
    AreaSample sample = m_shape->sampleArea(rng);
    transformFrame(sample, Vector());
    return sample;
}

} // namespace lightwave

REGISTER_CLASS(Instance, "instance", "default")
