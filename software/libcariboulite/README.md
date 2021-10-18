# CaribouLite RPI API
This folder contains the low-level and SoapySDR APIs for CaribouLite

# Building
To start from scratch we need to check for the dependencies and install them as needed.

Dependencies installation:
```
# Update the package definitions
sudo apt update

# We used gcc version 8.3+
sudo apt install gcc

# We used libsoapysdr-dev version 0.6.1-4+
sudo apt install libsoapysdr-dev

# cmake version 3.15+
sudo apt install cmake
```

Now to compile we use `cmake` as follows:

```
# create the building directory to contain 
# compilation / linking artifacts. if already exists skip
# the creation
mkdir build 

# goto the build directory
cd build

# tell cmake to create the Makefiles 
# according to the code in the parent directory
cmake ../

# build the code (this will take about 30 seconds @ RPi4)
make

# install the package in your Linux 
# environment (including SoapyAPIs)
sudo make install
```

# License
<a rel="license" href="http://creativecommons.org/licenses/by/4.0/"><img alt="Creative Commons License" style="border-width:0" src="https://i.creativecommons.org/l/by/4.0/88x31.png" /></a><br />This work is licensed under a <a rel="license" href="http://creativecommons.org/licenses/by/4.0/">Creative Commons Attribution 4.0 International License</a>.