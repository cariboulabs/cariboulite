# Coding examples

This demo directory shows how filters are set up, how data is
processed sample by sample and how the frequency responses
look like with the help of python scripts.

## Impulse responses and spectra of different filters:
```
./iirdemo
python3 plot_impulse_fresponse.py
```

## 50Hz removal and lowpass filtering:
```
./ecg50hzfilt
python3 plot_ecg_filt.py
```

## Optimisation tips

The actual filter operations are header-only which means that
they will never be pre-compiled in the library but the compiler
can optimise the filter routine for your local processor. Consider
using:
```
add_compile_options(-O3 -march=native)
```
