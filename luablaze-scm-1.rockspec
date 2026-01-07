package = "luablaze"
version = "scm-1"
source = {
  url = "git+https://github.com/jimmystewpot/lua-blaze-jsonschema.git"
}
description = {
  summary = "Lua bindings for sourcemeta/blaze JSON Schema validation",
  detailed = [[
Lua C module that compiles JSON Schema with sourcemeta/blaze and validates JSON
instances.
  ]],
  homepage = "https://github.com/jimmystewpot/lua-blaze-jsonschema",
  license = "MIT"
}
dependencies = {
  "lua >= 5.2"
}
external_dependencies = {
  LUA = {
    header = "lua.h",
    library = "lua"
  },
  CMAKE = {
    program = "cmake"
  }
}
build = {
  type = "command",
  build_command = "cmake -S . -B build.luarocks -DLUABLAZE_INIT_SUBMODULES=ON -DCMAKE_BUILD_TYPE=Release && cmake --build build.luarocks --parallel",
  install_command = "mkdir -p $(LIBDIR) && cp build.luarocks/luablaze.$(LIB_EXTENSION) $(LIBDIR)/luablaze.$(LIB_EXTENSION)"
}
