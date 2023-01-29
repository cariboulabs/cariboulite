#! /bin/bash

# determine the system type

# update the git repo on develop_R1 branch to include sub-modules
git checkout develop_R1
git submodule init
git submodule update

