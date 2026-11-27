/* See neolua.h. */
#include "neolua.h"

#include <math.h>
#include <stdio.h>

int neolua_number2str(char* buff, size_t size, lua_Number n) {
    if (n != n) return snprintf(buff, size, "NaN");
    if (n == HUGE_VAL) return snprintf(buff, size, "Infinity");
    if (n == -HUGE_VAL) return snprintf(buff, size, "-Infinity");
    if (n == 0) return snprintf(buff, size, "0");
    return snprintf(buff, size, "%.15G", n);
}

int neolua_tointeger(lua_State* L, int arg, lua_Integer* out) {
    int isnum;
    const lua_Number n = lua_tonumberx(L, arg, &isnum);
    return isnum && lua_numbertointeger(trunc(n), out);
}
