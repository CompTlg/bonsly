# Implementing SPPM in your renderer, step by step

Follows PBRT 3rd ed. section 16.2 (the page you are reading). Every API name below
was checked against your headers. Code blocks are either **real glue code** (renderer
API calls you would otherwise have to hunt for) or **pseudo-code** (the SPPM logic
itself, which is the part you should write). Things marked **YOUR TURN** are left for you.

Files this tutorial gives you (all new, nothing existing was changed):

| File | What it is |
|---|---|
| `tests/sppm/step1/step1_{rect,sphere}.xml` | your area-light tests, run through `sppm` (direct light only) |
| `tests/sppm/small/sppm_box_small.xml` | 64x64 Cornell box, indirect light only (for brute-force debugging) |
| `tests/sppm/sppm_box.xml` | 512x512 version of the same scene (the real test) |
| `*_ref.exr` next to them | references rendered by YOUR path tracer (4096 spp, depth 6) |
| `tests/sppm/tools/*_reference.xml` | how those references were made, so you can remake them |

Run any of them with `python3 run_tests.py sppm/step1`, `sppm/small` or `sppm`.

> **Status:** Steps 1 to 4 are implemented in `src/integrators/sppm.cpp` (camera pass, photon
> emission, photon pass, visible-point grid, update, output). The code blocks below explain what
> is there. Photons use the scene's `<sampler>` with one sample index per photon (PBRT's
> `haltonIndex`), which only works because the Halton sampler was fixed (see "Photon random
> numbers" in Step 3).

---

## 0. How to figure out what to write (the method)

Whenever you are stuck, do this instead of guessing:

1. **Write the algorithm as comments first.** Copy the structure of PBRT's `Render()`
   (section 16.2.3) as `// 1. ...  // 2. ...` comments inside `execute()`. Do not write code yet.
2. **For each comment, find the PBRT line, then find your equivalent.** Section 2 below is the
   lookup table. If a name is missing, search for it:
   ```bash
   grep -rn "struct BsdfSample" -A 12 include/lightwave/bsdf.hpp
   grep -rn "atomicAdd" include/lightwave/parallel.hpp
   ```
3. **Write one function, then test it with a number you can predict** (a sum, a count, a mean).
   Never write the camera pass, the photon pass and the update all at once.
4. **Print the three numbers that tell you everything**, once per iteration:
   ```cpp
   logger(EInfo, "iter %d: sum(M)=%d  mean radius=%.4f  mean Ld=%.4f", iter, sumM, meanR, meanLd);
   ```
   `sum(M)` must be > 0 and stable, mean radius must shrink slowly, `Ld` must be constant on average.
5. **Multi-threading bugs:** uncomment `// #define SINGLE_THREADED` near the top of
   `include/lightwave/parallel.hpp` while you debug, then comment it out again.
6. **When you ask for help**, say: what you ran, what you expected, what you got. That is
   enough for a diagnosis.

---

## 1. The algorithm on one page

Per pixel you keep a small state (PBRT calls it `SPPMPixel`):

| Field | Meaning | Starts at |
|---|---|---|
| `radius` R | current search radius | `initialRadius` |
| `N` | effective photon count (a float, can be fractional) | 0 |
| `tau` | accumulated flux term | 0 |
| `Ld` | sum over iterations of direct light seen by the camera path | 0 |
| `vp` | this iteration's visible point (intersection + throughput `beta`) | invalid |
| `phi`, `M` | this iteration's photon flux sum and photon count | 0 |

One iteration:

```
1. camera pass   : for every pixel, find the visible point, add direct light to Ld
2. photon pass   : shoot photons, at every hit after the first add beta*f to the pixels
                   whose visible point is within radius  ->  phi, M
3. update pixels : shrink radius, fold phi into tau, reset phi and M
```

The update for pixels with `M > 0` (use the OLD radius in the ratio):

```
gamma = (N + alpha*M) / (N + M)       alpha = 2/3
tau   = (tau + vp.beta * phi) * gamma
radius = radius * sqrt(gamma)
N     = N + alpha*M
```

Final pixel value after iteration `iter` (0-based):

```
Np = (iter+1) * photonsPerIteration                 // photons emitted so far
L  = Ld/(iter+1)  +  tau / (Np * Pi * radius^2)
```

### Three normalization rules (most bugs live here)

1. **Do NOT divide a photon's power by `photonsPerIteration`.** The division by `Np` at the
   end is the only one. (Doing both makes the image far too dark.)
2. **Photon power is divided by the light-selection probability** (`LightSample::probability`).
3. **Contributions are added only from the photon's 2nd surface hit onward** (`depth > 0`).
   The 1st hit is direct light, which the camera pass already got from NEE.

### What `depth` means

`depth="N"` = number of surface hits a photon is traced for. Contributions come from hits
1..N-1. So `depth="1"` means "no photon contributes", i.e. direct light only.
To compare with your path tracer, use `pathtracer depth = N + 1` (that is how the
references here were made: sppm depth 5 vs pathtracer depth 6).

---

## 2. PBRT -> your renderer lookup table

| PBRT | Your renderer |
|---|---|
| `SPPMPixel`, `VisiblePoint` | your own structs (Step 1) |
| camera ray | `CameraSample cs = m_scene->camera()->sample(pixel, *sampler);` then `cs.ray`, `cs.weight` (a `Color`) |
| resolution | `m_scene->camera()->resolution()` (a `Vector2i`) |
| `scene.Intersect` | `Intersection its = m_scene->intersect(ray, *sampler);`  `if (!its)` = miss |
| `isect.p`, `wo` | `its.position`, `its.wo` |
| `Le` at a hit / background | `its.evaluateEmission().value` (on a miss it returns the background) |
| `UniformSampleOneLight` | the NEE block of `pathtracer.cpp` (helper in Step 1) |
| `bsdf.f(wo, wi)` | **`its.evaluateBsdf(wi).value` BUT it already contains `cos(theta_i)`**. Divide it out (Step 3). |
| `bsdf.Sample_f` | `BsdfSample bs = its.sampleBsdf(*sampler);` `bs.wi`, `bs.weight` (= `f*cos/pdf`), `bs.isInvalid()` |
| `Light::Sample_Le` | does not exist yet. You add `samplePhoton` (Step 2) |
| Halton sampler per iteration | `sampler->seed(pixel, iter)` (camera pass) and `sampler->seed(int)` (photons) |
| parallel tiles | `for_each_parallel(BlockSpiral(resolution, Vector2i(64)), [&](auto block){ ... for (auto pixel : block) ... });` |
| parallel over photons | `for_each_parallel(ChunkedRange(count, 4096), [&](Range r){ for (int i : r) { ... } });` |
| atomic add | `atomicAdd(float&, float)`, `atomicAdd(int64_t&, int64_t)`, **`atomicAdd(Color&, const Color&)`**, all in `parallel.hpp` |
| scene bounds | `m_scene->getBoundingBox()` -> `Bounds` with `.center()`, `.diagonal()` |
| writing the image | `m_image->initialize(resolution)`, `m_image->get(pixel) = color;`, `m_image->save()` |
| each thread needs its own RNG | `auto sampler = m_sampler->clone();` inside the parallel lambda |

---

## Step 0: members and properties

Your constructor currently declares `int m_photonCount` as a *local* variable, so it is
thrown away. Make everything a member:

```cpp
class SPPM : public SamplingIntegrator
{
    int   m_depth;
    int   m_photonsPerIteration;
    float m_alpha;
    float m_initialRadius;

public:
    SPPM(const Properties &properties) : SamplingIntegrator(properties)
    {
        m_depth               = properties.get<int>("depth", 5);
        m_photonsPerIteration = properties.get<int>("photonsPerIteration", 100000);
        m_alpha               = properties.get<float>("alpha", 2.f / 3.f);
        m_initialRadius       = properties.get<float>("initialRadius", 0.1f);
    }

    // SamplingIntegrator::Li is pure virtual, so it must exist. Nothing calls it
    // (only SamplingIntegrator::execute does, and we override that), so make misuse loud:
    Color Li(const Ray &, Sampler &) override
    {
        lightwave_throw("SPPM overrides execute() and does not support Li()");
    }
    ...
```

**Why `SamplingIntegrator` and not `Integrator`?** PBRT's `SPPMIntegrator` extends `Integrator`
because PBRT's `SamplerIntegrator` is the camera-ray-only base. In lightwave the roles are
different: `Integrator` is an empty class (no sampler, image or scene), and `SamplingIntegrator`
is what holds `m_sampler`, `m_image`, `m_scene` and `setImage()`. Its own doc comment says to
override `execute()` when you need a custom process, which is exactly SPPM. More importantly,
the test harness (`src/tests/image.cpp`) does `properties.getChild<SamplingIntegrator>()`, so an
integrator that only extends `Integrator` would not be found by any `<test type="image">`.

Note: in SPPM, **the sampler's count is the number of iterations**: `<sampler count="256"/>`
means 256 iterations, and there is no separate `iterations` attribute. Each iteration is
1 camera sample per pixel, so the XMLs look like every other test in the repo.

---

## Step 1: pixel state + camera pass (direct light only)

**Goal:** `sppm` with no photons reproduces your path tracer's direct lighting.

```cpp
struct VisiblePoint {
    bool         valid = false;
    Intersection its;              // the surface point (position, frame, instance...)
    Color        beta  = Color(1); // camera throughput (cs.weight)
};

struct Pixel {
    float        radius = 0;
    float        N      = 0;
    Color        tau    = Color(0);
    Color        Ld     = Color(0);
    VisiblePoint vp;
    Color        phi    = Color(0);
    int64_t      M      = 0;      // int64_t because atomicAdd has an int64_t overload
};
std::vector<Pixel> m_pixels;
```

A NEE helper (this is the block from `pathtracer.cpp`, moved into a function so the camera
pass can call it):

```cpp
Color directLight(const Intersection &its, Sampler &rng) const
{
    if (!m_scene->hasLights()) return Color(0);
    LightSample ls = m_scene->sampleLight(rng);
    if (ls.isInvalid() || ls.light->canBeIntersected()) return Color(0);
    DirectLightSample ds = ls.light->sampleDirect(its.position, rng);
    if (ds.isInvalid()) return Color(0);
    BsdfEval be = its.evaluateBsdf(ds.wi);
    if (be.isInvalid()) return Color(0);
    Ray shadow(its.position, ds.wi);
    if (m_scene->intersect(shadow, ds.distance, rng)) return Color(0);
    return be.value * ds.weight / ls.probability;
}
```

`execute()` skeleton:

```cpp
void execute() override
{
    const Vector2i resolution = m_scene->camera()->resolution();
    m_image->initialize(resolution);
    m_pixels.assign(resolution.product(), Pixel());
    for (auto &px : m_pixels) px.radius = m_initialRadius;

    const int iterations = m_sampler->samplesPerPixel();   // sampler count = iterations
    for (int iter = 0; iter < iterations; ++iter) {
        cameraPass(iter, resolution);
        // photonPass(iter);   // Step 3
        // updatePixels();     // Step 3
        writeImage(iter, resolution);
    }
    m_image->save();
}
```

`cameraPass` (YOUR TURN for the body, here is the shape):

```cpp
void cameraPass(int iter, const Vector2i &resolution)
{
    for_each_parallel(BlockSpiral(resolution, Vector2i(64)), [&](auto block) {
        auto sampler = m_sampler->clone();
        for (auto pixel : block) {
            Pixel &px = m_pixels[pixel.y() * resolution.x() + pixel.x()];
            px.vp.valid = false;
            sampler->seed(pixel, iter);                    // different samples every iteration!
            CameraSample cs  = m_scene->camera()->sample(pixel, *sampler);
            Intersection its = m_scene->intersect(cs.ray, *sampler);

            // YOUR TURN:
            //  - px.Ld += cs.weight * its.evaluateEmission().value;   (also handles a miss)
            //  - if (!its) continue;
            //  - px.Ld += cs.weight * directLight(its, *sampler);
            //  - store the visible point: valid = true, its, beta = cs.weight
        }
    });
}
```

`writeImage`: `m_image->get(pixel) = px.Ld / float(iter + 1);` (the photon term comes in Step 3).

**Check:**
```bash
python3 run_tests.py sppm/step1
```
Both tests must pass with the same thresholds your path tracer passes at. If one fails, use the
analyzer from the area-light work and point it at the output image:
```bash
python3 tests/area_lights/mitsuba_scenes/analyze.py step1_rect tests/sppm/step1/step1_rect_test.exr
```
(the reference is found next to it as `step1_rect_ref.exr`).

Typical mistakes here: forgetting `px.vp.valid = false` first; using the same seed every
iteration (the image never gets less noisy); forgetting emission on camera hits.

---

## Step 2: photon emission on lights

**Goal:** every light can produce a photon. This is PBRT's `Light::Sample_Le`.

Add to `include/lightwave/light.hpp`:

```cpp
struct PhotonSample {
    Point  origin;
    Vector direction;   // normalized, direction the photon travels
    Color  power;       // L_e * cos(theta_light) / (pdfPosition * pdfDirection); zero = invalid
    bool isInvalid() const { return power == Color(0); }
};
```
and in class `Light` (default = "this light cannot emit photons"):

```cpp
virtual PhotonSample samplePhoton(Sampler &rng, const Bounds &sceneBounds) const { return {}; }
```
(`sceneBounds` is a parameter because a directional light needs the scene size.)

How each light fills it in (YOUR TURN, formulas given):

| Light | origin | direction | `power` |
|---|---|---|---|
| point | the light position | `squareToUniformSphere(rng.next2D())` | its `power` property (total flux = intensity * 4pi) |
| directional | `C + R*(d.x*f.tangent + d.y*f.bitangent) + R*dir_toward_light` where `C = bounds.center()`, `R = 0.5*bounds.diagonal().length()`, `d = squareToUniformDiskConcentric(rng.next2D())`, `f = Frame(dir_toward_light)` | `-dir_toward_light` | `intensity * Pi * R*R` (flux through the disk) |
| area | `s.position` from `m_instance->sampleArea(rng)` | `s.shadingFrame().toWorld(local)` with `local = squareToCosineHemisphere(rng.next2D())` | `L_e * Pi / s.pdf` where `L_e = emission->evaluate(s.uv, local).value` |

Where the area light's `L_e*Pi/s.pdf` comes from: `L_e*cos / (pdfPos * pdfDir)` with
`pdfPos = s.pdf` and `pdfDir = cos/Pi`; the cosines cancel. (This is why the pdf-rescale fix
in `Instance::sampleArea` matters: `1/s.pdf` must be the true world area.)

**Check (temporary code at the start of `execute()`):** the average of `power / probability`
over many photons must equal the total flux of all lights. **Accumulate in `double`**: a `float`
(or a `Color`) running sum of a million values of about 18 passes 2^24 and starts dropping
low bits, which reads about 1.3% too low and looks like a bug that is not there.

```cpp
double flux = 0, sumDirY = 0; int down = 0;
const int n = 1000000;
auto s = m_sampler->clone(); s->seed(0);
for (int i = 0; i < n; ++i) {
    LightSample ls = m_scene->sampleLight(*s);
    PhotonSample ps = ls.light->samplePhoton(*s, m_scene->getBoundingBox());
    if (ps.isInvalid()) continue;
    flux    += double(ps.power[0]) / ls.probability;   // channel 0 is enough for a grey light
    sumDirY += ps.direction.y();
    if (ps.direction.y() > 0) ++down;
}
logger(EInfo, "flux = %f   photons going down = %f   mean direction.y = %f",
       flux / n, double(down) / n, sumDirY / n);
```

For `tests/sppm/sppm_box.xml` (one rectangle light, `L = 12`, side 0.7) expect:
- `flux = 18.4726` (that is `pi * 12 * 0.49`; every single photon has exactly this power),
- `photons going down = 1.0` (the lamp faces +y, toward the floor),
- `mean direction.y = 0.6667` (the mean cosine of a cosine-weighted hemisphere is 2/3).

`NaN` anywhere means a zero-length direction got normalized; look at how the direction is computed.

---

## Step 3: photon pass, update, final estimate (brute force, tiny image)

**Goal:** correct image, slow code. Use the **64x64** scene so it is fast enough.

Photon pass, one photon (YOUR TURN for the marked lines). Needs `#include <algorithm>` for `std::max`:

```cpp
const Bounds bounds = m_scene->getBoundingBox();
for_each_parallel(ChunkedRange(m_photonsPerIteration, 4096), [&](Range range) {
    auto sampler = m_sampler->clone();
    for (int i : range) {
        // random numbers: see "Photon random numbers" below, do NOT use the scene's sampler here
        LightSample ls  = m_scene->sampleLight(*sampler);
        if (ls.isInvalid()) continue;
        PhotonSample ps = ls.light->samplePhoton(*sampler, bounds);
        if (ps.isInvalid()) continue;

        Color beta = ps.power / ls.probability;          // NOT divided by photonsPerIteration
        Ray   ray(ps.origin, ps.direction);

        for (int depth = 0; depth < m_depth; ++depth) {
            Intersection its = m_scene->intersect(ray, *sampler);
            if (!its) break;

            if (depth > 0) {
                const Vector wi = -ray.direction;        // points back to where the photon came from
                // YOUR TURN (brute force):
                //   for every pixel px with px.vp.valid:
                //     skip if |px.vp.its.position - its.position|^2 > px.radius^2
                //     BsdfEval be = px.vp.its.evaluateBsdf(wi);
                //     if (!be.isInvalid()) {
                //         float cosi = Frame::absCosTheta(px.vp.its.shadingFrame().toLocal(wi));
                //         Color f = be.value / cosi;    // f_r WITHOUT the cosine
                //         atomicAdd(px.phi, beta * f);
                //     }
                //     atomicAdd(px.M, int64_t(1));      // PBRT counts the photon either way
            }

            BsdfSample bs = its.sampleBsdf(*sampler);
            if (bs.isInvalid()) break;
            Color bnew = beta * bs.weight;               // bs.weight = f*cos/pdf, like PBRT's bnew
            float q = std::max(0.f, 1.f - bnew.luminance() / beta.luminance());
            if (sampler->next() < q) break;              // Russian roulette
            beta = bnew / (1.f - q);
            ray  = Ray(its.position, bs.wi);
        }
    }
});
```

### Photon random numbers (important)

Every photon is one sample index: `sampler->seed(iter * photonsPerIteration + i)`, exactly like
PBRT's `haltonIndex`, so the photons of all iterations form one long low-discrepancy sequence. A
photon path needs 15 to 20 random numbers, which exposed three bugs in the Halton sampler:

1. **The bases were not prime** (2, 3, 4, 5, 6, ...). Base 4 is a function of the digits of base 2,
   so those dimensions are strongly correlated. About 5% too dark in the box scene, and it never
   went away with more iterations. Bases are now the primes 2, 3, 5, 7, 11, ...
2. **One shared random shift for all dimensions** only slides the point set along the diagonal of
   the unit cube. Every dimension now gets its own shift.
3. **It could return exactly 1.0**: the radical inverse of index 2^25 - 1 is 1 - 2^-25, which rounds
   to 1.0f. `Scene::sampleLight` then indexed past the last light: a sporadic crash around
   photon 33.5 million. Samplers must return numbers in [0, 1), so the value is clamped (and
   the light selection no longer runs past the end either).

Check a sampler change with two things: `python3 run_tests.py halton_sampler` and the "SPPM vs path
tracer by depth" comparison (the box scene's mean must match within about 0.5%).

Why divide by `cosi`: your `evaluateBsdf` returns `f*cos(theta_i)` (documented in
`bsdf.hpp`), but the photon estimate `L = sum f * Phi / (pi r^2)` needs `f` alone, because the
photon *density* on the surface already contains the foreshortening. PBRT's `bsdf.f` has no
cosine, which is why its code has no cosine here.

Update step (YOUR TURN; the formulas are in section 1): loop over pixels with
`vp.valid && M > 0`, apply `gamma`, then reset `phi` and `M`. Do this *after* the photon pass
and *before* writing the image.

Final estimate in `writeImage`:
```cpp
const float Np = float(iter + 1) * m_photonsPerIteration;
Color L = px.Ld / float(iter + 1);
if (Np > 0) L += px.tau / (Np * Pi * px.radius * px.radius);
```

**Check:**
```bash
python3 run_tests.py sppm/small
```

### Debug table (what a wrong image tells you)

| Symptom | Likely cause |
|---|---|
| much too dark in indirect areas, by about 0.5-0.7x | cosine counted twice (`be.value` used without dividing by `cosi`) |
| far too dark, by roughly `photonsPerIteration` | photon power divided by the photon count AND `Np` |
| too bright near the lamp | contributions added at `depth == 0` (direct light counted twice) |
| noisy and never gets smoother with more iterations | radius update not applied (e.g. skipped when `M==0` incorrectly, or radius not stored) |
| the image does not change between iterations | same seed every iteration |
| one colour channel looks wrong | `tau` or `phi` combined per-channel incorrectly (use `Color` ops, not luminance) |
| NaN / inf | dividing by `N + M == 0`, or `cosi == 0`; guard both |
| hangs or is extremely slow | brute force on a big image: use the 64x64 scene |

---

## Step 4: the visible-point grid (PBRT 16.2.4)

**Goal:** same image as Step 3, but fast, so you can use the 512x512 scene (262,144 visible points; brute force is impossible there).

Why: brute force costs (number of photon hits) x (number of pixels). A grid reduces the second
factor to a handful.

Design (build it **every iteration**, because radii and visible points change):

```
1. bounds   = union over valid vp of  [p - radius, p + radius]
2. maxRadius = largest radius
3. res[i]   = clamp( floor(bounds.diagonal()[i] / maxRadius), 1, 128 )      // cells ~ maxRadius wide
4. cellSize[i] = bounds.diagonal()[i] / res[i]
5. toCell(p): ix = clamp(int((p.x() - bounds.min().x()) / cellSize.x()), 0, res.x()-1), same for y, z
6. insert vp k into EVERY cell overlapped by [p-radius, p+radius]   (up to 8 cells)
7. query: look only at the cell containing the photon hit, test the distance, splat
```

Memory layout that needs no per-cell vectors (counting sort):
`pass 1` count entries per cell -> prefix sum gives `cellStart[]` -> `pass 2` fill `cellItems[]`.
Building serially is fine; the lookups are read-only so the photon pass stays parallel.

**Check:** add a temporary XML flag `bruteForce="1"` that skips the grid. On the 64x64 scene,
`sum(M)` per iteration and the final image must be identical (up to float summation order) with
and without the grid. Only then switch to the 512x512 scene:

```bash
python3 run_tests.py sppm
```

---

## Step 5: experiments (this is what the thesis needs)

1. **Convergence:** render with `<sampler count>` = 1, 4, 16, 64, 256 and plot MAE against the
   reference. It should keep falling. Vanilla photon mapping plateaus; this is your headline plot.
2. **Initial radius:** try 0.03, 0.1, 0.3. Large radii are blurry at iteration 1 but all of them
   converge to the same image.
3. **Photons per iteration vs iterations** at a fixed total budget.
4. Compare against the path tracer on a scene where it struggles (small light behind glass).

## Step 6: specular visible points (needed for glass and caustics, do it last)

PBRT does not store the visible point on a specular surface. It follows the BSDF-sampled ray
(`beta *= bs.weight`) until it reaches a non-specular surface, and only then stores the visible
point. For that you need a way to ask a BSDF "are you specular?" (a small new virtual on `Bsdf`),
and the camera pass becomes a loop instead of a single intersect. Photons also have to pass
through specular surfaces without contributing. This is also where wavelength-dependent IOR
(dispersion) plugs in later.

---

## Cheat sheet: mistakes people actually make

- Visible point `beta` forgotten in `tau += vp.beta * phi`.
- Using the **new** radius for the ratio instead of `gamma`.
- Not resetting `phi` and `M` after the update.
- Building the grid once instead of every iteration.
- `evaluateBsdf` called with `wi` pointing the wrong way. It must point *away* from the surface,
  toward where the photon came from, so `wi = -ray.direction`.
- Light selection weights: your `sampleLight` picks by the `weight` property (default 1), not by
  power. That is unbiased (you divide by the probability) but noisier for very unequal lights.
- Seeds: `sampler->seed(pixel, iter)` for the camera pass; `sampler->seed(iter * photonsPerIteration + i)`
  for photons (one sample index per photon).
