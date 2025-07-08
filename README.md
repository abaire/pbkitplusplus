pbkitplusplus
====

Provides slightly higher level functionality for original Microsoft Xbox graphics programming, building on
the [nxdk](https://github.com/XboxDev/nxdk)'s pbkit library.

__Importantly, the intent of this project is not to provide a particularly user-friendly experience.__ This library is
designed to be used in situations where a primary goal is to have fine-grained control over the nv2a pushbuffer (e.g.,
in tests).
Projects such as [pbgl](https://github.com/fgsfdsfgs/pbgl) would be better suited for general use.

If you're interested in nv2a graphics debugging, check out
[the wiki for the nxdk_pgraph_tests repo](https://github.com/abaire/nxdk_pgraph_tests/wiki) for tools and tips.

## Building

By default, this project will compile with optimizations enabled even if the `CMAKE_BUILD_TYPE` is set to debug. This is
done to allow host code to be debugged while minimizing the negative performance impact on this library. This library
may be forced to build without optimization by setting the "PBKPP_NO_OPT" CMake option to "ON". 

### Building with CLion

The CMake target can be configured to use the toolchain from the nxdk:

* CMake options

  `-DCMAKE_TOOLCHAIN_FILE=<absolute_path_to_nxdk_checkout>/share/toolchain-nxdk.cmake`

* Environment

  `NXDK_DIR=<absolute_path_to_nxdk_checkout>`

On macOS you may also have to modify `PATH` in the `Environment` section such that a homebrew version of LLVM
is preferred over Xcode's (to supply `dlltool`).

## Including in CMake-based projects

This project is expected to be used as a dependency of other projects.

You can include it via `FetchContent`. In your `CMakeLists.txt` file:

```cmake
include(FetchContent)

FetchContent_Declare(
        pbkitplusplus
        GIT_REPOSITORY https://github.com/abaire/pbkitplusplus
        GIT_TAG        main
)

FetchContent_MakeAvailable(pbkitplusplus)
```
pbkitplusplus also provides [xbox_math3d](https://github.com/abaire/xbox_math3d) and
[xbox-swizzle](https://github.com/abaire/xbox-swizzle) as these are often needed by users of this library.
