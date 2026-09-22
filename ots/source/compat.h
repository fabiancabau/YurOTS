// Compatibility for the original Lua 5.0 API on the supported Linux build.
#pragma once
#include "definitions.h"
#include <stdint.h>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <climits>
#include <unistd.h>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
#define lua_dofile luaL_dofile
#define luaopen_loadlib luaopen_package

#define _atoi64 atoll
#define _timeb timeb
#define _ftime ftime
