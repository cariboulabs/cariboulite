# GNU Radio Python bindings

This directory contains the native source block's Python bindings and docstring
templates. The parent module conditionally includes Python/GRC support through
`ENABLE_PYTHON`; see the [module build](../../../CMakeLists.txt).

The installed namespace is `from gnuradio import caribouLite`; check
`hasattr(caribouLite, "caribouLiteSource")` as well as importing, because the
package initializer suppresses a missing native binding module. See the
[module guide](../../../README.md) for build and source behavior.

The presence of bindings does not establish that a particular GNU Radio/Python
combination builds or installs correctly. Packaging and an import/GRC smoke test
remain [DOC-07](../../../../../ROADMAP.md#documentation-validation-backlog).
