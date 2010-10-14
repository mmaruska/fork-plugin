#ifndef CONFIG_H
#define CONFIG_H


// i want the version string settable from the command line!!
// fixme: if it is not a string it's a segfault !!!
#ifndef VERSION_STRING
#define VERSION_STRING "(unknown version)"
#endif
#define PLUGIN_VERSION 33


// use -DNDEBUG to avoid asserts!
// Trace + debug:
#define DEBUG 1
// I like colored tracing (in 256-color xterm)
#define USE_COLORS 1

#define KEEP_PREVIOUS 0
#define STATIC_LAST 1

#endif
