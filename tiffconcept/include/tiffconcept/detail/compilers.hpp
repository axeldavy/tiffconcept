#pragma once

// If only compilers could agree...


#if defined(_MSC_VER)
    #define TIFFCONCEPT_FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
    #define TIFFCONCEPT_FORCE_INLINE inline __attribute__((always_inline))
#else
    #define TIFFCONCEPT_FORCE_INLINE inline
#endif