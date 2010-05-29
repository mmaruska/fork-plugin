#ifndef CONFIG_H
#define CONFIG_H


#define xloader 1               // this is loaded as a module!
#define XKB_IN_SERVER 1
#define PLUGIN_VERSION 32
#define VERSION_STRING VERSION


#if 1                           // i like colored tracing (in 256-color xterm)
#define escape_sequence "\x1b"
#define info_color "\x1b[47;31m"
#define fork_color "\x1b[47;30m"
#define key_color "\x1b[01;32;41m"
#define keysym_color "\x1b[33;01m"
#define warning_color "\x1b[38;5;160m"
#define key_io_color "\x1b[38;5;226m"
#define color_reset "\x1b[0m"

#else
#define info_color ""
#define key_color  ""
#define warning_color  ""
#define color_reset ""
#define escape_sequence ""
#endif


// i want the version string settable from the command line!!
// fixme: if it is not a string it's a segfault !!!
#ifndef VERSION
#define VERSION "1.1"
#endif



/// Trace + debug:
#define DEBUG 1

#if (! DEBUG)
#define NODEBUG 1
// #include <assert.h>
#endif



#define KEEP_PREVIOUS 0
#define STATIC_LAST 1

#endif
