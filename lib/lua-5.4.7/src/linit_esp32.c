/*
** ESP32-optimized Lua library initialization
** Only includes essential libraries for embedded use
*/

#define linit_c
#define LUA_LIB

#include "lprefix.h"

#include <stddef.h>

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

/*
** These libs are loaded by lua.c and are readily available to any Lua
** program.
*/
static const luaL_Reg loadedlibs[] = {
  {LUA_GNAME, luaopen_base},
  {LUA_TABLIBNAME, luaopen_table},
  {LUA_STRLIBNAME, luaopen_string},
  {LUA_MATHLIBNAME, luaopen_math},
  /* Disabled for embedded: io, os, debug, package, coroutine, utf8 */
  {NULL, NULL}
};

LUALIB_API void luaL_openlibs (lua_State *L) {
  const luaL_Reg *lib;
  /* "require" functions from 'loadedlibs' and set results to global table */
  for (lib = loadedlibs; lib->func; lib++) {
    luaL_requiref(L, lib->name, lib->func, 1);
    lua_pop(L, 1);  /* remove lib */
  }
}
