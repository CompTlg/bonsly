#include <lightwave.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>


namespace lightwave
{
    // What the camera path found for one pixel in the current iteration.
    struct VisiblePoint
    {
        bool valid = false;
        Intersection its;      // the surface point (position, frame, instance, wo)
        Color beta = Color(1); // camera throughput
    };

    // PBRT's SPPMPixel: everything that is remembered per pixel across iterations.
    struct SPPMPixel
    {
        float radius = 0.f;   // current search radius R
        float N = 0.f;        // effective photon count
        Color tau = Color(0); // accumulated flux term
        Color Ld = Color(0);  // summed direct light of all iterations
        VisiblePoint vp;      // this iteration's visible point
        Color phi = Color(0); // this iteration's photon flux (reset after update)
        int64_t M = 0;        // this iteration's photon count (reset after update)
        float area = 1.f;     // fraction of the search disc that lies on the visible point's own surface
    };



    // Uniform grid over this iteration's visible points (PBRT 16.2.4). Every visible point is
    // stored in all cells its search sphere overlaps, so a photon only has to inspect the one
    // cell it lands in. Rebuilt every iteration: the points and their radii change.
    class VisiblePointGrid
    {
        Bounds m_bounds;
        Point m_min;
        float m_inv[3] = {1, 1, 1}; // cells per unit length
        int m_res[3] = {1, 1, 1};
        bool m_empty = true;
        std::vector<int> m_cellStart; // items of cell c: m_items[m_cellStart[c] .. m_cellStart[c + 1])
        std::vector<int> m_items;     // pixel indices

        int cellCoord(float offset, int axis) const
        {
            return std::clamp(int(std::floor(offset * m_inv[axis])), 0, m_res[axis] - 1);
        }

        int cellIndex(int x, int y, int z) const { return (z * m_res[1] + y) * m_res[0] + x; }

        // range of cells overlapped by the search sphere's bounding box
        void cellRange(const SPPMPixel &px, int lo[3], int hi[3]) const
        {
            const Point &p = px.vp.its.position;
            for (int a = 0; a < 3; ++a)
            {
                lo[a] = cellCoord(p[a] - px.radius - m_min[a], a);
                hi[a] = cellCoord(p[a] + px.radius - m_min[a], a);
            }
        }

    public:
        void build(const std::vector<SPPMPixel> &pixels)
        {
            m_bounds = Bounds();
            float maxRadius = 0.f;
            for (const SPPMPixel &px : pixels)
            {
                if (!px.vp.valid)
                    continue;
                const Point &p = px.vp.its.position;
                const float r = px.radius;
                m_bounds.extend(Point{p.x() - r, p.y() - r, p.z() - r});
                m_bounds.extend(Point{p.x() + r, p.y() + r, p.z() + r});
                maxRadius = std::max(maxRadius, r);
            }
            m_empty = maxRadius <= 0.f;
            m_cellStart.clear();
            m_items.clear();
            if (m_empty)
                return;

            // cells about one (maximum) radius wide, with a cap on the total number of cells
            const Vector diag = m_bounds.diagonal();
            long long total = 1;
            for (int a = 0; a < 3; ++a)
            {
                m_res[a] = std::clamp(int(diag[a] / maxRadius), 1, 256);
                total *= m_res[a];
            }
            constexpr long long maxCells = 1 << 22;
            if (total > maxCells)
            {
                const float shrink = std::cbrt(float(maxCells) / float(total));
                total = 1;
                for (int a = 0; a < 3; ++a)
                {
                    m_res[a] = std::max(1, int(m_res[a] * shrink));
                    total *= m_res[a];
                }
            }
            m_min = m_bounds.min();
            for (int a = 0; a < 3; ++a)
                m_inv[a] = m_res[a] / diag[a];

            // counting sort: count per cell, prefix sum, then fill
            std::vector<int> start(total + 1, 0);
            int lo[3], hi[3];
            for (const SPPMPixel &px : pixels)
            {
                if (!px.vp.valid)
                    continue;
                cellRange(px, lo, hi);
                for (int z = lo[2]; z <= hi[2]; ++z)
                    for (int y = lo[1]; y <= hi[1]; ++y)
                        for (int x = lo[0]; x <= hi[0]; ++x)
                            ++start[cellIndex(x, y, z) + 1];
            }
            for (long long c = 0; c < total; ++c)
                start[c + 1] += start[c];

            m_items.resize(start[total]);
            std::vector<int> cursor(start.begin(), start.end() - 1);
            for (size_t i = 0; i < pixels.size(); ++i)
            {
                if (!pixels[i].vp.valid)
                    continue;
                cellRange(pixels[i], lo, hi);
                for (int z = lo[2]; z <= hi[2]; ++z)
                    for (int y = lo[1]; y <= hi[1]; ++y)
                        for (int x = lo[0]; x <= hi[0]; ++x)
                            m_items[cursor[cellIndex(x, y, z)]++] = int(i);
            }
            m_cellStart = std::move(start);
        }

        // the pixels whose search sphere might contain p (the caller still does the distance test)
        std::pair<const int *, const int *> candidates(const Point &p) const
        {
            if (m_empty || !m_bounds.includes(p))
                return {nullptr, nullptr};
            const int c = cellIndex(cellCoord(p[0] - m_min[0], 0),
                                    cellCoord(p[1] - m_min[1], 1),
                                    cellCoord(p[2] - m_min[2], 2));
            return {m_items.data() + m_cellStart[c], m_items.data() + m_cellStart[c + 1]};
        }
    };

    class SPPMIntegrator : public SamplingIntegrator
    {
        // ---- parameters from the XML ----
        int m_depth;
        int m_photonsPerIteration;
        float m_alpha;
        float m_initialRadius;

        // ---- state shared by the passes ----
        std::vector<SPPMPixel> m_pixels; // one per image pixel, index = y * width + x
        VisiblePointGrid m_grid;         // rebuilt every iteration from the visible points

        // direct light at a surface point, the NEE block of pathtracer.cpp as a function
        Color directLight(const Intersection &its, Sampler &rng) const
        {
            if (!m_scene->hasLights())
                return Color(0);
            LightSample ls = m_scene->sampleLight(rng);
            if (ls.isInvalid() || ls.light->canBeIntersected())
                return Color(0);
            DirectLightSample ds = ls.light->sampleDirect(its.position, rng);
            if (ds.isInvalid())
                return Color(0);
            BsdfEval be = its.evaluateBsdf(ds.wi);
            if (be.isInvalid())
                return Color(0);
            Ray shadow(its.position, ds.wi);
            if (m_scene->intersect(shadow, ds.distance, rng))
                return Color(0);
            return be.value * ds.weight / ls.probability;
        }

        // Which fraction of the tangent disc of radius R around the visible point is still part of the
        // visible point's own surface? Probe rays are shot against the surface from just above the disc;
        // a probe counts if it hits the same instance with a similar normal at the expected distance.
        float surfaceFraction(const Intersection &vp, float radius, Sampler &rng) const
        {
            Vector n = vp.geometryNormal;
            if (n.dot(vp.wo) < 0.f)
                n = -n;
            const Frame frame(n);
            const float h = 0.5f * radius; // height of the probe origins above the tangent plane
            // Does the surface continue under this probe? Only evidence that it ENDS counts as a miss:
            // nothing below, something far below, or a differently oriented surface at the expected place.
            // A probe that is blocked by something above the surface (a sphere resting on the floor) says
            // nothing about where the surface ends, so it is assumed to continue.
            auto probe = [&](float r, float angle)
            {
                const Point q = vp.position + frame.tangent * (r * std::cos(angle)) + frame.bitangent * (r * std::sin(angle));
                Intersection hit = m_scene->intersect(Ray(q + n * h, -n), rng);
                if (!hit)
                    return 0;
                const float tolerance = 0.35f * radius;
                if (hit.t < h - tolerance)
                    return 1; // blocked from above
                return int(std::abs(hit.t - h) < tolerance && std::abs(hit.geometryNormal.dot(n)) > 0.9f);
            };
            const float phase = 2.f * Pi * rng.next();
            // cheap test first: four points near the rim; if the surface continues under all of them,
            // the disc is complete
            int rim = 0;
            for (int i = 0; i < 4; ++i)
                rim += probe(0.95f * radius, phase + 0.5f * Pi * i);
            if (rim == 4)
                return 1.f;
            // otherwise estimate the fraction with probes spread evenly over the disc's AREA (Fermat spiral)
            constexpr int count = 24;
            int hits = 0;
            for (int i = 0; i < count; ++i)
                hits += probe(radius * std::sqrt((i + 0.5f) / count), phase + 2.3999632f * i);
            const int total = count;
            return std::max(float(hits) / float(total), 0.2f);
        }

        // Pass 1: one camera path per pixel -> visible point + direct light
        void cameraPass(int iter, const Vector2i &resolution)
        {
            for_each_parallel(BlockSpiral(resolution, Vector2i(64)), [&](auto block)
                              {
                auto sampler = m_sampler->clone();
                for (auto pixel : block)
                {
                    SPPMPixel &px = m_pixels[pixel.y() * resolution.x() + pixel.x()]; // get the sppmpixel for this pixel
                    px.vp.valid = false; // initializing the visible point of the sppmpixel
                    sampler->seed(pixel, iter); // sampling the pixel, halton or whatever chosen
                    CameraSample cameraSample = m_scene->camera()->sample(pixel, *sampler); // sampling the camera ray for this pixel
                    Intersection its = m_scene->intersect(cameraSample.ray, *sampler); // intersecting the camera ray with the scene
                    
                    px.Ld += cameraSample.weight * its.evaluateEmission().value; // can be beta but first run might not have beta, right
                    if (!its) continue; // if the intersection is invalid, we continue to the next pixel
                    px.Ld += cameraSample.weight * directLight(its, *sampler); // adding the direct light to the pixel's Ld
                    px.vp.valid = true; // we have a valid visible point as the intersection happened from the camera ray
                    px.vp.its = its; // storing the intersection in the visible point 
                    px.vp.beta = cameraSample.weight; // storing the camera sample weight in the visible
                    px.area = surfaceFraction(its, px.radius, *sampler);

                    // if just this step runs, its basically path tracing without indirect lighting. following from pbrt step by step

                } });
        }

        // add one photon hit to every visible point whose search sphere contains it
        // (PBRT: "Add photon contribution to nearby visible points")
        void splatPhoton(const Intersection &photonHit, const Vector &wi, const Color &beta)
        {
            const Point &hit = photonHit.position;
            auto [first, last] = m_grid.candidates(hit);
            for (const int *it = first; it != last; ++it)
            {
                SPPMPixel &px = m_pixels[*it];
                if (Vector(px.vp.its.position - hit).lengthSquared() > px.radius * px.radius)
                    continue;
                // only photons that landed on the same surface belong to this visible point: similar
                // normal, and inside a thin slab around the visible point's tangent plane
                const Vector &n0 = px.vp.its.geometryNormal;
                if (std::abs(photonHit.geometryNormal.dot(n0)) < 0.9f ||
                    std::abs(n0.dot(photonHit.position - px.vp.its.position)) > 0.35f * px.radius)
                    continue;

                // f_r WITHOUT the cosine: evaluateBsdf returns f_r * |cos|, but the photon density
                // on the surface already contains the foreshortening
                const BsdfEval be = px.vp.its.evaluateBsdf(wi);
                if (!be.isInvalid())
                {
                    const float cosine = Frame::absCosTheta(px.vp.its.shadingFrame().toLocal(wi));
                    if (cosine > 0.f)
                        atomicAdd(px.phi, beta * (be.value / cosine));
                }
                atomicAdd(px.M, int64_t(1)); // like PBRT, the photon counts even if f_r = 0
            }
        }

        // Pass 2: shoot photons from the lights and accumulate them into px.phi / px.M
        void photonPass(int iter)
        {
            if (m_photonsPerIteration <= 0 || !m_scene->hasLights())
                return;
            const Bounds sceneBounds = m_scene->getBoundingBox();

            for_each_parallel(ChunkedRange(m_photonsPerIteration, 1024), [&](Range range)
                              {
                auto sampler = m_sampler->clone();
                for (int i : range)
                {
                    // like PBRT's haltonIndex: every photon of every iteration is one sample index
                    sampler->seed(iter * m_photonsPerIteration + i);

                    LightSample ls = m_scene->sampleLight(*sampler);
                    if (ls.isInvalid())
                        continue;
                    PhotonSample ps = ls.light->samplePhoton(*sampler, sceneBounds);
                    if (ps.isInvalid())
                        continue;

                    Color beta = ps.power / ls.probability; // NOT divided by the photon count: Np does that at the end
                    Ray ray(ps.origin, ps.direction);

                    for (int depth = 0; depth < m_depth; ++depth)
                    {
                        Intersection its = m_scene->intersect(ray, *sampler);
                        if (!its)
                            break;

                        // the first hit is direct light, which the camera pass already got via NEE
                        if (depth > 0)
                            splatPhoton(its, -ray.direction, beta);

                        BsdfSample bs = its.sampleBsdf(*sampler);
                        if (bs.isInvalid())
                            break;

                        // Russian roulette on the throughput loss
                        const Color betaNew = beta * bs.weight;
                        const float q = std::max(0.f, 1.f - betaNew.luminance() / beta.luminance());
                        if (sampler->next() < q)
                            break;
                        beta = betaNew / (1.f - q);
                        ray = Ray(its.position, bs.wi);
                    }
                } });
        }

        // Pass 3: shrink the radii, fold this iteration's photons into tau, reset phi and M
        void updatePixels()
        {
            for_each_parallel(ChunkedRange(int(m_pixels.size()), 4096), [&](Range range)
                              {
                for (int i : range)
                {
                    SPPMPixel &px = m_pixels[i];
                    if (!px.vp.valid || px.M == 0)
                        continue;
                    const float M = float(px.M);
                    const float Nnew = px.N + m_alpha * M;
                    const float gamma = Nnew / (px.N + M);
                    px.tau = (px.tau + px.vp.beta * px.phi / px.area) * gamma;
                    px.radius *= std::sqrt(gamma);
                    px.N = Nnew;
                    px.phi = Color(0);
                    px.M = 0;
                } });
        }

        // Turn the per-pixel state into the image after iteration `iter`
        void writeImage(int iter, const Vector2i &resolution)
        {
            const float Np = float(iter + 1) * float(std::max(m_photonsPerIteration, 0)); // photons emitted so far
            for (auto pixel : m_image->bounds())
            {
                const SPPMPixel &px = m_pixels[pixel.y() * resolution.x() + pixel.x()];
                Color L = px.Ld / float(iter + 1);
                if (Np > 0.f && px.radius > 0.f)
                    L += px.tau / (Np * Pi * px.radius * px.radius);
                m_image->get(pixel) = L;
            }
        }

    public:
        SPPMIntegrator(const Properties &properties) : SamplingIntegrator(properties)
        {
            m_depth = properties.get<int>("depth", 5);
            m_photonsPerIteration = properties.get<int>("photonsPerIteration", 100000);
            m_alpha = properties.get<float>("alpha", 2.f / 3.f);
            m_initialRadius = properties.get<float>("initialRadius", 0.f); // 0 = automatic, see execute()
        }

        // The whole algorithm: this REPLACES the pixel/sample loop of SamplingIntegrator::execute
        void execute() override
        {
            if (!m_image)
            {
                lightwave_throw(
                    "<integrator /> needs an <image /> child to render into!");
            }

            const Vector2i resolution = m_scene->camera()->resolution();
            m_image->initialize(resolution);

            m_pixels.assign(resolution.product(), SPPMPixel());
            // Initial search radius. The leak of colour across corners (photons from the neighbouring
            // wall) is as wide as this radius, so it should be small; unless given, use 1.5% of the
            // scene size, which keeps the artifact thin without making the first iterations too noisy.
            const float initialRadius = m_initialRadius > 0.f
                                            ? m_initialRadius
                                            : 0.015f * m_scene->getBoundingBox().diagonal().length();
            for (auto &px : m_pixels)
                px.radius = initialRadius;

            // one iteration = one camera sample per pixel, so the sampler's
            // count (<sampler count="..."/>) is the number of iterations
            const int iterations = m_sampler->samplesPerPixel();
            if (int64_t(iterations) * std::max(m_photonsPerIteration, 0) > INT32_MAX)
                lightwave_throw("samples * photonsPerIteration must stay below 2^31 (it is the photon sample index)");

            Streaming stream{*m_image};            // live preview in tev
            ProgressReporter progress{iterations}; // one unit per iteration
            for (int iter = 0; iter < iterations; ++iter)
            {
                cameraPass(iter, resolution);
                if (m_photonsPerIteration > 0)
                {
                    m_grid.build(m_pixels);
                    photonPass(iter);
                    updatePixels();
                }
                writeImage(iter, resolution);
                stream.update(); // send the whole image to tev
                progress += 1;
            }
            progress.finish();

            m_image->save();
        }

        // Required by SamplingIntegrator, but execute() above never calls it.
        Color Li(const Ray &, Sampler &) override
        {
            lightwave_throw("SPPM overrides execute() and does not support Li()");
        }

        std::string toString() const override
        {
            return tfm::format(
                "SPPM[\n"
                "  iterations = %d,\n"
                "  photonsPerIteration = %d,\n"
                "  sampler = %s,\n"
                "  image = %s,\n"
                "]",
                m_sampler->samplesPerPixel(), m_photonsPerIteration,
                indent(m_sampler),
                indent(m_image));
        }
    };
} // namespace lightwave
REGISTER_INTEGRATOR(SPPMIntegrator, "sppm")
