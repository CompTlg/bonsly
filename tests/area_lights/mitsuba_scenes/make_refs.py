#!/usr/bin/env python3
"""Renders the Mitsuba reference images for the area light tests.

Output: tests/area_lights/<name>_ref.exr (what run_tests.py compares against).
Usage:  python3 tests/area_lights/mitsuba_scenes/make_refs.py [name ...]
"""
import os
import sys

import mitsuba as mi

mi.set_variant("scalar_rgb")

here = os.path.dirname(os.path.abspath(__file__))
out_dir = os.path.dirname(here)
names = sys.argv[1:] or ["arealight_rect", "arealight_sphere"]

for name in names:
    scene = mi.load_file(os.path.join(here, f"{name}_mitsuba.xml"))
    img = mi.render(scene)
    path = os.path.join(out_dir, f"{name}_ref.exr")
    mi.util.write_bitmap(path, img)
    print("wrote", path)
