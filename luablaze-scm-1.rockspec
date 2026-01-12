rockspec_format = "3.0"
package = "luablaze"
version = "scm-1"

source = {
   url = "git+https://github.com/jimmystewpot/lua-blaze-jsonschema.git"
}

description = {
   summary = "JSON Schema validation for Lua using Blaze",
   homepage = "https://github.com/jimmystewpot/lua-blaze-jsonschema",
   license = "MIT"
}

dependencies = {
   "lua >= 5.3"
}

build = {
   type = "cmake",
   variables = {
      CMAKE_BUILD_TYPE = "Release",
      LUABLAZE_INIT_SUBMODULES = "ON",
      LUA_INCDIR = "$(LUA_INCDIR)",
      LUA_LIBDIR = "$(LUA_LIBDIR)",
      INSTALL_CMOD = "$(LIBDIR)"
   }
}
