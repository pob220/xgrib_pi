/* Configuration for this decoder-only build. Clang detects clzll itself. */
#if defined(__GNUC__)
#define HAVE_DECL___BUILTIN_CLZLL 1
#else
#define HAVE_DECL___BUILTIN_CLZLL 0
#endif
