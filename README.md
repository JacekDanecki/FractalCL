# FractalCL

Discover and explore fractals world with OpenCL acceleration.

# Features

* Interactive animated fractals
* Mouse support to zoom in/out
* Keyboard support for changing fractals/kernel parameters
* OpenCL support to speed up fractals calculations
* 2 colors models: RGB and HSV
* OpenCL kernels can be executed on CPU
* fp64 support can be enabled
* Suppport multiple OpenCL devices

# Requirements 

* SDL library
* OpenCL library

# Implemented fractals

* julia z^2+c
![julia](julia.png)

* julia z^3+c
![julia3](julia3.png)

* mandelbrot z^2+c
![mandelbrot](mandelbrot.png)

* dragon
![dragon](dragon.png)

# Tested OpenCL implementations

* Neo - Intel Graphics Compute Runtime for OpenCL: https://github.com/intel/compute-runtime
* Beignet - OpenCL Library for Intel GPU's: https://cgit.freedesktop.org/beignet
* OpenCL CPU - OpenCL Runtime for Intel Core and Intel Xeon Processors https://software.intel.com/en-us/articles/opencl-drivers#latest_CPU_runtime

# Build instruction

* Run configure script to configure project
* Run make to build project
* To disable fp64 support modify configure script

# Run instruction

* Execute FractalCL application in build directory. Use mouse and keyboard to change fractals parameters.
* Press ESC key to exit application.

# Mouse usage

* left button - increase zoom
* right button - decrease zoom
* middle button - stop animation

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

