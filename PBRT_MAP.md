# Your renderer <-> PBRT (3rd ed.) map

Everything in the PBRT column was checked against the real source
(<https://github.com/mmp/pbrt-v3>, which is the code printed in the book) and the book pages.
File/line numbers are from the current master and may drift by a few lines.

---

## 0. The one thing that causes the confusion

PBRT has **no function called "sample photon"**. A photon is just the *algorithm's name* for
"a ray that leaves a light and carries a power `beta`". The function that creates that ray is

```
Light::Sample_Le        <->        your Light::samplePhoton
```

The two letters mean **L**ight, **i**ncident / **e**mitted:

| | PBRT | yours | Question it answers |
|---|---|---|---|
| `Li` | `Sample_Li` | `sampleDirect` | "I am the point **x**. Give me light **arriving at me**." (has a receiver) |
| `Le` | `Sample_Le` | `samplePhoton` | "I am the **light**. Give me a ray **leaving me**." (no receiver) |

```
Sample_Li / sampleDirect                 Sample_Le / samplePhoton

   x (receiver) <----- point on light       point on light -----> random direction
   direction is DETERMINED by x             position AND direction are both random
   used by: direct lighting (NEE)           used by: photon mapping, bidirectional path tracing
```

Who calls them (found with `grep -rn "Sample_Le(" src/integrators`): only **`sppm.cpp:337`** and
`bdpt.cpp:109`. Every integrator that traces paths *from the lights* uses `Sample_Le`; every
integrator that does next-event estimation uses `Sample_Li`.

---

## 1. Lights

| Yours | PBRT | Where in PBRT | Notes |
|---|---|---|---|
| `Light::sampleDirect(origin, rng)` -> `DirectLightSample{wi, weight, distance}` | `Light::Sample_Li(ref, u, &wi, &pdf, &vis)` | `core/light.h`; bodies: book 12.3 Point, 12.4 Distant, 12.5 Area, 12.6 Infinite | PBRT returns `L` and `pdf` **separately**; your `weight = L / pdf` folds them together. |
| the shadow ray in your `directLight` | `VisibilityTester vis` (`vis.Unoccluded(scene)`) | `core/light.h` | your `distance` is where the shadow ray stops. |
| `Light::samplePhoton(rng, bounds)` -> `PhotonSample{origin, direction, power}` | `Light::Sample_Le(u1, u2, time, &ray, &nLight, &pdfPos, &pdfDir)` | decl. `core/light.h:75`; bodies: **book section 16.1.2 "Sampling Light Rays"**, code `lights/*.cpp` | PBRT returns the pieces; the caller combines them (next row). |
| `PhotonSample::power` | `beta = AbsDot(nLight, ray.d) * Le / (lightPdf * pdfPos * pdfDir)` | `integrators/sppm.cpp` right after the `Sample_Le` call; book 16.2 | You fold `Le*cos/(pdfPos*pdfDir)` into `power`. `lightPdf` is your `LightSample::probability`, which the integrator divides by. |
| `Scene::sampleLight(rng)` -> `LightSample{light, probability}` | `lightDistr->SampleDiscrete(u, &lightPdf)` | `sppm.cpp`, `ComputeLightPowerDistribution` | PBRT picks lights by **power**; yours by the `weight` attribute. Both are unbiased because you divide by the probability. |
| (nothing yet) | `Light::Power()` | each `lights/*.cpp` | The total flux of a light: point `4*Pi*I`, distant `L*Pi*R*R`, area `L*area*Pi`, infinite `Pi*R*R*L(center)`. **Use it as your test: the average of `power/probability` must equal the sum of `Power()`.** |
| `Scene::getBoundingBox()` | `Light::Preprocess(scene)` -> `worldCenter`, `worldRadius` | `core/light.h`; book 12.4 | PBRT uses the scene's bounding *sphere*. You use `center()` and `0.5*diagonal().length()`. |
| `Light::canBeIntersected()` | `light->flags` (`DeltaPosition`, `Infinite`, ...) | `core/light.h` | different mechanism, same idea (can a ray hit it by chance?). |
| `AreaLight` | `DiffuseAreaLight` | `lights/diffuse.cpp` | |
| `Instance::sampleArea(rng)` -> `AreaSample{position, pdf, ...}` | `Shape::Sample(u, &pdf)` | `core/shape.h` | `pdf` is per unit **area**. |
| `Emission::evaluate(uv, localDir)` | `DiffuseAreaLight::L(p, n, uv, w)` | `lights/diffuse.h` | zero on the back side in both. |
| `BackgroundLight` / `EnvironmentMap` | `InfiniteAreaLight` | `lights/infinite.cpp` | |

### The four `Sample_Le` bodies, side by side

All four are read from `src/lights/*.cpp`. `R` = `worldRadius`.

| Light | origin | direction | `pdfPos` | `pdfDir` | returns | `cos` term | -> `beta` |
|---|---|---|---|---|---|---|---|
| Point (`point.cpp:61`) | `pLight` | `UniformSampleSphere(u1)` | 1 | `1/(4*Pi)` | `I` | 1 (`nLight = ray.d`) | `4*Pi*I` |
| Distant (`distant.cpp:69`) | `pDisk + R*wLight`, with `pDisk = center + R*(cd.x*v1 + cd.y*v2)`, `cd = ConcentricSampleDisk(u1)`, `CoordinateSystem(wLight, &v1, &v2)` | `-wLight` | `1/(Pi*R*R)` | 1 | `L` | 1 | `L*Pi*R*R` |
| Area (`diffuse.cpp:89`) | `shape->Sample(u1, &pdfPos)` | `CosineSampleHemisphere(u2)` around the normal | the shape's area pdf | `CosineHemispherePdf(w.z)` = `cos/Pi` | `L(p, w)` | `cos` (cancels with `pdfDir`) | `L*Pi/pdfPos` |
| Infinite (`infinite.cpp:134`) | same disk as distant, with the sampled direction `d` | picked from the map's brightness distribution | `1/(Pi*R*R)` | `mapPdf/(2*Pi*Pi*sinTheta)` (uniform sphere would be `1/(4*Pi)`) | `Lmap(uv)` | 1 | `L*Pi*R*R / pdfDir` |

Compare the last column with `Power()` of the same file: for point, distant and area the `beta`
of one photon **is** the light's `Power()` (the lights emit identical photons).

---

## 2. Small helper functions

| Yours | PBRT |
|---|---|
| `squareToUniformSphere(u)`, pdf `Inv4Pi` | `UniformSampleSphere(u)`, `UniformSpherePdf()` |
| `squareToCosineHemisphere(u)` | `CosineSampleHemisphere(u)` |
| `cosineHemispherePdf(w)` | `CosineHemispherePdf(cosTheta)` |
| `squareToUniformDiskConcentric(u)` | `ConcentricSampleDisk(u)` |
| `Frame(normal)` -> `tangent`, `bitangent` | `CoordinateSystem(v, &v1, &v2)` |
| `Frame::toWorld(local)` / `toLocal(world)` | `local.x*v1 + local.y*v2 + local.z*n` / dot products |
| `Frame::absCosTheta(w)` | `AbsCosTheta(w)`, `AbsDot(a, b)` |
| `Color` (RGB) | `Spectrum` |
| `Color::luminance()` | `Spectrum::y()` |
| `Sampler::next()`, `next2D()` | `Sampler::Get1D()`, `Get2D()` |
| `sampler->seed(pixel, iter)` | `tileSampler->StartPixel(pPixel); tileSampler->SetSampleNumber(iter);` (`sppm.cpp:152`) |
| `m_sampler->clone()` | `sampler.Clone(seed)` |
| `for_each_parallel(...)` | `ParallelFor(...)` |
| `atomicAdd(Color&, Color)`, `atomicAdd(int64_t&, int64_t)` | `AtomicFloat::Add`, `std::atomic<int>` |

---

## 3. The integrator (`SPPMIntegrator::Render` in `integrators/sppm.cpp`)

| Yours | PBRT call / fragment |
|---|---|
| `SPPMIntegrator::execute()` | `SPPMIntegrator::Render(scene)` |
| `SPPMPixel`, `VisiblePoint` | `SPPMPixel`, `SPPMPixel::VisiblePoint {p, wo, bsdf, beta}` (same names: `radius`, `Ld`, `Phi`, `M`, `N`, `tau`) |
| number of iterations = `<sampler count>` | `nIterations` (default 64) |
| `m_photonsPerIteration` | `photonsPerIteration` (default -1 means "one photon per pixel", `sppm.h:60`) |
| `m_depth` | `maxDepth` (default 5) |
| `m_initialRadius` | `initialSearchRadius` (default 1.0, in scene units) |
| `m_alpha` | `Float gamma = 2/3` (the book calls it gamma in the code, alpha in the text) |
| `cameraPass` | fragment "Generate SPPM visible points" |
| `Camera::sample(pixel, rng)` | `tileSampler->GetCameraSample(pPixel)` + `camera->GenerateRayDifferential` |
| `Scene::intersect(ray, rng)` | `scene.Intersect(ray, &isect)` |
| `its.evaluateEmission()` | `isect.Le(wo)` |
| `directLight(its, rng)` | `UniformSampleOneLight(isect, scene, arena, sampler)` |
| `its.evaluateBsdf(wi)` | `bsdf.f(wo, wi)` **but yours includes `|cos|`, PBRT's does not** |
| `its.sampleBsdf(rng)` -> `weight` | `bsdf.Sample_f(wo, &wi, u, &pdf, ...)`; `weight = f * AbsDot(wi, n) / pdf` (`bnew = beta * fr * AbsDot / pdf`) |
| visible point stored | `pixel.vp = {isect.p, wo, &bsdf, beta}` |
| `photonPass` | fragment "Trace photons and accumulate contributions" |
| `m_scene->sampleLight` + `samplePhoton` | `lightDistr->SampleDiscrete` + `light->Sample_Le(...)` |
| Russian roulette in the photon pass | `q = max(0, 1 - bnew.y()/beta.y())`; `if (u < q) break;` then `beta = bnew / (1 - q)` |
| photon contribution | `Phi = beta * pixel.vp.bsdf->f(pixel.vp.wo, wi)`; `++pixel.M` (only for `depth > 0`) |
| `updatePixels` | fragment "Update pixel values from this pass's photons": `Nnew = N + gamma*M`; `Rnew = radius*sqrt(Nnew/(N+M))`; `tau = (tau + vp.beta*Phi) * Rnew^2/radius^2` |
| `writeImage` | `L = Ld/(iter+1)`; `L += tau / (Np * Pi * radius^2)` |
| visible-point grid (Step 4) | fragment "Create grid of all SPPM visible points" (hash table of `SPPMPixelListNode`) and `ToGrid` |

---

## 4. How to find things yourself

**Method A: read the source, not the book.** The book prints this exact code, but the source is
searchable.

```bash
git clone --depth 1 https://github.com/mmp/pbrt-v3.git ~/pbrt-v3
cd ~/pbrt-v3
grep -rn "Sample_Le" src/          # the declaration, every implementation, every caller
grep -rn "Sample_Li" src/lights/   # the other half of the interface
grep -rn "Power()"  src/lights/    # the totals you should be able to reproduce
```

Reading the output tells you the whole story: `core/light.h` is the interface,
`lights/<kind>.cpp` has one body per light, and the callers (`integrators/sppm.cpp`,
`integrators/bdpt.cpp`) show what the outputs are used for.

**Method B: go from the algorithm to the function.**
1. On the SPPM page you see `light->Sample_Le(...)` and a `beta` line, but not the function.
2. Search for the function name (method A) to find its bodies, one per light type.
3. The book text explaining *why* is in section 16.1.2 "Sampling Light Rays" (on the page titled
   "The Path-Space Measurement Equation") and in section 12.4 for the scene bounds.

**Method C: let `Power()` tell you the answer.** If you don't know what a photon's `power` should
be for a new light, find that light's `Power()` in PBRT. Total flux / number of photons is what
each photon carries (when lights emit identical photons), and the average of
`power / probability` over many samples must reproduce it. That is the flux check in the tutorial.

**Method D: translate types before translating logic.** PBRT's pointer outputs
(`Ray *ray`, `Float *pdfPos`) become your struct fields; `Spectrum` becomes `Color`;
`Normal3f nLight` is only used for `AbsDot(nLight, ray.d)` (the cosine at the light).
Then the logic is the same line for line.

**Common traps while translating**
- PBRT's `bsdf.f` has **no** cosine; your `evaluateBsdf` has. Divide it out for photon gathering.
- PBRT's `wLight` and your `direction` both point **toward** the light (the photon travels `-wLight`).
- PBRT returns `L` and `pdf` separately from `Sample_Li`; your `weight` already divides them.
- `pdf` for shapes is per unit **area**; make sure yours is in world units (the instance fix).
