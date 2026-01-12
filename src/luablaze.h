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
// - luablaze.validate_detailed(compiled_schema, instance_table) -> boolean, report_table
// - luablaze.validate_json_detailed(compiled_schema, instance_json) -> boolean, report_table
// - CompiledSchema:validate(instance_table) -> boolean
// - CompiledSchema:validate_json(instance_json) -> boolean
// - CompiledSchema:validate_detailed(instance_table) -> boolean, report_table
// - CompiledSchema:validate_json_detailed(instance_json) -> boolean, report_table
// - CompiledSchema:evaluate(instance_table) -> boolean (alias for validate)
//
// Module constants:
// - luablaze._VERSION (string) - Module version (e.g., "1.0.0")
// - luablaze._NAME (string) - Module name ("luablaze")
// - luablaze._BLAZE_VERSION (string) - Sourcemeta Blaze library version (e.g., "0.0.1")
//
// Thread Safety:
// - CompiledSchema objects are NOT thread-safe. External synchronization is
//   required if the same CompiledSchema instance is used concurrently from
//   multiple threads. Each thread should use its own CompiledSchema instance
//   or proper locking mechanisms.

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