// luablaze.h - Lua C module header for luablaze
//
// This header declares the public interface for the luablaze Lua C module,
// which provides Lua bindings for the Sourcemeta Blaze JSON Schema
// compiler/evaluator.
//
// The module exports:
// - luablaze.new(schema_json[, options_table]) -> CompiledSchema
// - luablaze.validate(compiled_schema, instance_table) -> boolean
// - luablaze.validate_json(compiled_schema, instance_json) -> boolean
// - luablaze.validate_output(compiled_schema, instance_table) -> string
// - luablaze.validate_output_json(compiled_schema, instance_json) -> string
// - CompiledSchema:validate(instance_table) -> boolean
// - CompiledSchema:validate_json(instance_json) -> boolean
// - CompiledSchema:validate_output(instance_table) -> string
// - CompiledSchema:validate_output_json(instance_json) -> string
// - CompiledSchema:evaluate(instance_table) -> boolean (alias for validate)

#ifndef LUABLAZE_H
#define LUABLAZE_H

extern "C" {
#include <lua.hpp>
}

#ifdef _MSC_VER
#define LUABLAZE_EXPORT __declspec(dllexport)
#else
#define LUABLAZE_EXPORT __attribute__((visibility("default")))
#endif

extern "C" {
    // Module entrypoint for `require("luablaze")`.
    LUABLAZE_EXPORT int luaopen_luablaze(lua_State *L);
}

#endif // LUABLAZE_H