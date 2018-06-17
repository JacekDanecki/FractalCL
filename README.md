# FractalCL

Discover and explore fractals world with OpenCL acceleration.

![parameters](parameters.png)

# Features

* Interactive animated fractals
* Mouse support to zoom in/out
* Zoom limit set to 43000000000000 for fp64 and 300000 for fp32
* Keyboard support for changing fractals/kernel parameters
* OpenCL support to speed up fractals calculations
* 2 colors models: RGB and HSV
* OpenCL kernels can be executed on CPU
* fp64 support checked at runtime, can be disabled in configuration (configure script)
* Suppport multiple OpenCL platforms/devices

# Tested OpenCL implementations

* Neo - Intel Graphics Compute Runtime for OpenCL: https://github.com/intel/compute-runtime
* Beignet - OpenCL Library for Intel GPU's: https://cgit.freedesktop.org/beignet
* OpenCL CPU - OpenCL Runtime for Intel Core and Intel Xeon Processors: https://software.intel.com/en-us/articles/opencl-drivers#latest_CPU_runtime
* Nvidia OpenCL 1.2 CUDA - The NVIDIA Accelerated Graphics Driver Set for Linux-x86_64: http://www.nvidia.com
* Portable Computing Language: http://pocl.sourceforge.net

# Mouse usage

* left button - increase zoom
* right button - decrease zoom
* middle button - stop animations

# Keyboard usage

* ESC - exit application
* i/u - increase/decrease number of calculation's iteriations
* o/p/k/l/n/m - change color in RGB mode
* LEFT/RIGTH - scale horizontally by 0.01
* UP/DOWN - scale vertically
* a/d - shift left/right
* s/w - shift down/up
* z/x - decrease/increase limit for compared modulus
* comma/period - scale horizontally and vertically
* h - change color pallette: RGB/HSV
* [/] - decrease/increase c (real part) of complex number (z^2 + c, z^3 + c)
* -/= - decrease/increase c (imaginary part) of complex number (z^2 + c, z^3 + c)
* F1 - select Julia fractal z^2 + c
* F2 - select Mandelbrot fractal z^2 + c
* F3 - select Julia with gws aligned with window size
* F4 - select dragon fractal
* F5 - select Julia z^3 + c
* v - change device used for calculation:
      0 = CPU
      1,..., n = OCL device
* 1 - show calculated complex number
* 2 - multiply gws (global workgroup size) by 2
* 3 - divide gws (global workgroup size) by 2

# Implemented fractals

* julia z^2 + c
![julia](julia.png)

* julia z^3 + c
![julia3](julia3.png)

* mandelbrot z^2 + c
![mandelbrot](mandelbrot.png)

* burning ship (|z.re| + |z.im|)^2 + c
![burning_ship](burning_ship.png)

* dragon
![dragon](dragon.png)

# Dependencies

* SDL2, SDL2_TTF libraries
* OpenCL library (optional)

# Build instruction

* Run configure script to configure project
* Run make to build project

# Build configuration (configure script)

* FP_64_SUPPORT - Use fp64 extension [ON/OFF] (default ON)
* SDL_ACCELERATED - "Use SDL with GPU acceleration [ON/OFF] (default OFF)
* OPENCL_SUPPORT - Use OpenCL acceleration [ON/OFF] (default ON)

# Run instruction

* Execute FractalCL application in build directory. Use mouse and keyboard to change fractals parameters.
* Press ESC key to exit application.

# Tests (directory tests)

* test_ocl - verify OpenCL support
* test_complex - simple tests with complex numbers
* test_sdl - SDL2 benchmarking test

