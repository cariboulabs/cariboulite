# SoapySDR adapter

The parent [CMake build](../../CMakeLists.txt) creates `SoapyCariboulite` when
SoapySDR development files are found. This directory contains device discovery,
session ownership, configuration/sensors and RX/TX streaming implementations.

Discovery uses the case-sensitive driver name `Cariboulite` and exposes the
`S1G` and `HiF` paths as separate device entries. With the plugin installed,
`SoapySDRUtil --find="driver=Cariboulite"` lists them. A selected probe such as
`SoapySDRUtil --probe="driver=Cariboulite,channel=HiF"` initializes hardware;
close other radio-owning applications first. See the
[installation guide](../../../../installation.md) for library/plugin checks.

- `SoapyCariboulite.cpp`: registration and discovery entry points.
- `CaribouliteSession.cpp`: shared device session handling.
- `Cariboulite.cpp` and `CaribouliteSensors.cpp`: configuration and sensors.
- `CaribouliteStreamFunctions.cpp` and `CaribouliteStream.cpp`: stream operations.

Regression harnesses are in [tests](../../tests/), including
`test_soapy_stream.py`. An implemented TX API is not proof of successful
end-to-end GNU Radio transmission. Verified flowgraphs and application recipes
remain [DOC-07](../../../../ROADMAP.md#documentation-validation-backlog).
