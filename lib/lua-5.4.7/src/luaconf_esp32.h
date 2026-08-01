/*
** ESP32-optimized Lua configuration
** Minimal memory footprint for embedded systems
*/

#ifndef luaconf_esp32_h
#define luaconf_esp32_h

/* Disable unused standard libraries to save memory */
#define LUA_COMPAT_5_3          /* Keep 5.3 compatibility */

/* Memory optimization: use 32-bit integers and floats */
#define LUA_32BITS
#define LUA_INT_TYPE    LUA_INT_INT
#define LUA_FLOAT_TYPE  LUA_FLOAT_FLOAT

/* Reduce memory usage */
#define LUAI_MAXSTACK   500     /* Reduced from 1000000 */
#define LUA_MINBUFFER   32      /* Reduced from 32 */
#define LUA_MAXCAPTURES 16      /* Reduced from 32 */

/* Disable features not needed for embedded */
#define LUA_USE_C89             /* Use C89 for better compatibility */

/* No file I/O on embedded system */
#define LUA_USE_POSIX   0

/* Memory allocator - use Arduino's malloc/free */
#define LUA_USE_APICHECK 0      /* Disable API checks in production */

#endif
