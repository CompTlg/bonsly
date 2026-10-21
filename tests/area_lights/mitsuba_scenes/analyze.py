#!/usr/bin/env python3
"""Localizes *where* and *how* a render disagrees with its reference.

run_tests.py only says "error threshold exceeded". This prints the ratio
ours/reference per image region and a diagnosis hint, so you can tell a
wrong constant factor from a wrong distance falloff from a missing shadow.

Usage: python3 tests/area_lights/mitsuba_scenes/analyze.py arealight_rect
       (run after `build/yoldas tests/area_lights/arealight_rect.xml`,
        which writes tests/area_lights/arealight_rect_test.exr)
"""
import os
import sys

import mitsuba as mi
import numpy as np

mi.set_variant("scalar_rgb")

name = sys.argv[1]
d = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ours = np.array(mi.Bitmap(os.path.join(d, f"{name}_test.exr")), copy=False)
ref = np.array(mi.Bitmap(os.path.join(d, f"{name}_ref.exr")), copy=False)

lo, lr = ours.mean(axis=2), ref.mean(axis=2)
print(f"{name}: mean ours={lo.mean():.4f} ref={lr.mean():.4f} "
      f"ratio={lo.mean() / max(lr.mean(), 1e-9):.3f}  "
      f"MAE={np.abs(ours - ref).mean():.4f}  signed={((ours - ref).mean()):+.4f}")
print(f"  lit pixels: ours={(lo > 1e-4).mean():.2%} ref={(lr > 1e-4).mean():.2%}")

# ratio ours/ref on a 4x4 grid (top row = top of the image)
n = 4
h, w = lo.shape
print("\n  ratio ours/ref per region ('.' = region is black in the reference):")
ratios = []
for gy in range(n):
    row = []
    for gx in range(n):
        a = lo[gy * h // n:(gy + 1) * h // n, gx * w // n:(gx + 1) * w // n].mean()
        b = lr[gy * h // n:(gy + 1) * h // n, gx * w // n:(gx + 1) * w // n].mean()
        if b < 1e-3:
            row.append("   .  ")
        else:
            ratios.append(a / b)
            row.append(f"{a / b:6.2f}")
    print("   " + " ".join(row))

if not ratios or lo.mean() < 1e-6:
    print("\n  -> ours is black: light not registered / faces away / canBeIntersected() true / "
          "weight is zero")
else:
    r = np.array(ratios)
    spread = r.max() - r.min()
    g = lo.mean() / max(lr.mean(), 1e-9)
    print()
    if abs(g - 1) < 0.03 and spread < 0.1:
        print("  -> looks right (within ~3%)")
    elif spread < 0.15:
        print(f"  -> constant factor {g:.2f}x everywhere. Fingerprints:")
        print("       ~4.0x (sphere test) / ~5.6x (rect test): area pdf not rescaled by the "
              "instance transform (see Instance::sampleArea)")
        print("       ~2.0x (sphere test): the far half of the sphere light is contributing "
              "(cos<0 not rejected / abs(cos)), or cos at the light is missing")
        print("       other constant: check the emission value, the 1/pi, light-selection "
              "probability")
    else:
        print("  -> ratio varies across the image: suspect a geometry term "
              "(1/d^2, cos at the light, cos at the surface), the shadow ray, or a "
              "back-facing light that still emits")
