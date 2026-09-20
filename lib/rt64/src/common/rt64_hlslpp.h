// Disable SIMD on GCC due to UB when compiling with optimizations.

#if defined(__GNUC__) && !defined(__clang__) && !defined(__INTEL_COMPILER)
#define HLSLPP_SCALAR
#endif

// Disable SIMD on Apple Silicon due to UB when compiling with optimizations.

#if defined(__APPLE__) && defined(__aarch64__)
#define HLSLPP_SCALAR
#endif

// Pokemon Snap port: the C standard library before hlsl++, which uses it
// without including it; Apple's libc++ does not bring it in on its own (the
// community's macOS build found this).
#include <cstdlib>

#include "hlsl++.h"