# GNU Radio Conda packaging template

This directory contains inherited packaging scaffolding, not a validated
CaribouLite installation recipe. The native Linux/Raspberry Pi library and
hardware dependencies still apply; the presence of `bld.bat` does not establish
Windows support.

The current [recipe](recipe/meta.yaml) lists GNU Radio, Python, pybind11 and
other generic OOT dependencies, but does not declare the native `cariboulite`
header/library/pkg-config dependency required by `lib/CMakeLists.txt`. Its
project metadata still contains placeholders, and the build scripts do not
explicitly enable the module's conditional Python/GRC build. Resolve those
items and record a build/import/GRC check before distributing a package.
The [module guide](../README.md) documents the direct source build.

Packaging validation remains
[DOC-07](../../../ROADMAP.md#documentation-validation-backlog).
