#include <lightwave.hpp>

#include "../core/plyparser.hpp"
#include "accel.hpp"

namespace lightwave {

/**
 * @brief A shape consisting of many (potentially millions) of triangles, which
 * share an index and vertex buffer. Since individual triangles are rarely
 * needed (and would pose an excessive amount of overhead), collections of
 * triangles are combined in a single shape.
 */
class TriangleMesh : public AccelerationStructure {
    /**
     * @brief The index buffer of the triangles.
     * The n-th element corresponds to the n-th triangle, and each component of
     * the element corresponds to one vertex index (into @c m_vertices ) of the
     * triangle. This list will always contain as many elements as there are
     * triangles.
     */
    std::vector<Vector3i> m_triangles;
    /**
     * @brief The vertex buffer of the triangles, indexed by m_triangles.
     * Note that multiple triangles can share vertices, hence there can also be
     * fewer than @code 3 * numTriangles @endcode vertices.
     */
    std::vector<Vertex> m_vertices;
    /// @brief The file this mesh was loaded from, for logging and debugging
    /// purposes.
    std::filesystem::path m_originalPath;
    /// @brief Whether to interpolate the normals from m_vertices, or report the
    /// geometric normal instead.
    bool m_smoothNormals;

    inline void populate(SurfaceEvent &surf, const Point &position,
                         const Vector &normal,
                         const Vertex interpolated_Ver) const {

        surf.position = position;

        surf.uv = interpolated_Ver.uv;

        // Gouraud shading
        // set both the geometry and shading normal based on smooth shading
        // setting
        if (m_smoothNormals) {
            surf.geometryNormal = normal.normalized();
            surf.shadingNormal  = interpolated_Ver.normal.normalized();
        } else {
            // when smooth shading is disabled, use the geometry normal for both
            surf.geometryNormal = normal.normalized();
            surf.shadingNormal  = surf.geometryNormal;
        }

        // Compute the tangent vector. It should be perpendicular to the shading
        // normal. If the shading normal is close to the x-axis, use (0, 1, 0)
        // as an arbitrary vector to cross with. (?
        if (fabs(surf.shadingNormal.x()) < 0.999) {
            surf.tangent =
                Vector(1, 0, 0).cross(surf.shadingNormal).normalized();
        } else {
            surf.tangent =
                Vector(0, 1, 0).cross(surf.shadingNormal).normalized();
        }

        // surf.shadingFrame() = Frame(normal);
        // surf.shadingFrame() = Frame(surf.shadingNormal);
        surf.pdf = 0; // prob. density function
    }

protected:
    int numberOfPrimitives() const override { return int(m_triangles.size()); }

    bool intersect(int primitiveIndex, const Ray &ray, Intersection &its,
                   Sampler &rng) const override {
        // THANK YOU SCRATCHAPIXEL

        const Vertex &v1 = m_vertices[m_triangles[primitiveIndex][0]];
        const Vertex &v2 = m_vertices[m_triangles[primitiveIndex][1]];
        const Vertex &v3 = m_vertices[m_triangles[primitiveIndex][2]];

        Vector T = Vector(ray.origin - v1.position);   // T = O - A    (notation
                                                       // from scratchPixel)
        Vector e1 = Vector(v2.position - v1.position); // E1 = B - A
        Vector e2 = Vector(v3.position - v1.position); // E2 = C - A

        // helper variables
        Vector P = Vector(ray.direction.cross(e2)); // P = D x E2
        Vector Q = Vector(T.cross(e1));             // Q = T x E1

        float det    = P.dot(e1); // determinant = P . E1
        float invDet = 1.0f / det;

        // If det is close to zero, the ray and the triangle are parallel
        // the give Epislon is still big, 1e-8 is small enough
        if (abs(det) < 1e-8)
            return false;

        Vector tuv = invDet * Vector(Q.dot(e2), P.dot(T), Q.dot(ray.direction));

        // intersection exists if (0 <= u <= 1 && 0 <= v <= 1 && u + v <= 1)
        // in Vector tuv, u = tuv.y(), v = tuv.z()
        if (tuv.y() < 0 || tuv.y() > 1 || tuv.z() < 0 || tuv.z() > 1 ||
            tuv.y() + tuv.z() > 1) {
            return false;
        }

        if (tuv.x() < Epsilon || tuv.x() > its.t)
            return false;

        its.t     = tuv.x(); // its.t = t
        Point pos = ray(tuv.x());

        Vector geometryNormal = e1.cross(e2).normalized();
        // #TODO // check if geometryNormal is 0
        Vertex interpolate_Vert =
            Vertex::interpolate(Vector2(tuv.y(), tuv.z()), v1, v2, v3);

        populate(its, pos, geometryNormal, interpolate_Vert);

        return true;

        // hints:
        // * use m_triangles[primitiveIndex] to get the vertex indices of the
        // triangle that should be intersected
        // * if m_smoothNormals is true, interpolate the vertex normals from
        // m_vertices
        //   * make sure that your shading frame stays orthonormal!
        // * if m_smoothNormals is false, use the geometrical normal (can be
        // computed from the vertex positions)
    }

    Bounds getBoundingBox(int primitiveIndex) const override {

        Bounds box        = Bounds::empty();
        Vector3i triangle = m_triangles[primitiveIndex];

        for (int i = 0; i < 3; ++i) {
            const Vertex &vertex = m_vertices[triangle[i]];
            box.extend(vertex.position);
        }

        return box;
    }

    Point getCentroid(int primitiveIndex) const override {

        Vector3i triangle = m_triangles[primitiveIndex];
        const Vertex &v0  = m_vertices[triangle[0]];
        const Vertex &v1  = m_vertices[triangle[1]];
        const Vertex &v2  = m_vertices[triangle[2]];

        Point centre = Point{
            v0.position.x() + v1.position.x() + v2.position.x() / 3.0f,
            v0.position.y() + v1.position.y() + v2.position.y() / 3.0f,
            v0.position.z() + v1.position.z() + v2.position.z() / 3.0f,
        }; // dumbest code ever awards goes to me

        return centre;
    }

public:
    TriangleMesh(const Properties &properties) {
        m_originalPath  = properties.get<std::filesystem::path>("filename");
        m_smoothNormals = properties.get<bool>("smooth", true);
        readPLY(m_originalPath, m_triangles, m_vertices);
        logger(EInfo,
               "loaded ply with %d triangles, %d vertices",
               m_triangles.size(),
               m_vertices.size());
        buildAccelerationStructure();
    }

    bool intersect(const Ray &ray, Intersection &its,
                   Sampler &rng) const override {
        PROFILE("Triangle mesh")
        return AccelerationStructure::intersect(ray, its, rng);
    }

    AreaSample sampleArea(Sampler &rng) const override{
        // only implement this if you need triangle mesh area light sampling for
        // your rendering competition
        NOT_IMPLEMENTED
    }

    std::string toString() const override {
        return tfm::format(
            "Mesh[\n"
            "  vertices = %d,\n"
            "  triangles = %d,\n"
            "  filename = \"%s\"\n"
            "]",
            m_vertices.size(),
            m_triangles.size(),
            m_originalPath.generic_string());
    }
};

} // namespace lightwave

REGISTER_SHAPE(TriangleMesh, "mesh")
