#!/bin/sh
doxygen
cd docs
git add .
cd pdf
make
