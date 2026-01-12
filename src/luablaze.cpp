#include <lua.hpp>
#include <lauxlib.h>

#include "luablaze.h"

#include <sourcemeta/core/json.h>
#include <sourcemeta/core/jsonschema.h>

#include <sourcemeta/blaze/compiler.h>
#include <sourcemeta/blaze/evaluator.h>
#include <sourcemeta/blaze/output_standard.h>

#include <cmath>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

/**
 * @file luablaze.cpp
 * @brief Lua bindings for Sourcemeta Blaze JSON Schema compiler/evaluator
 *
 * This file implements a Lua C module that binds the Sourcemeta Blaze JSON
 * Schema compiler/evaluator.
 *
 * @section API
 *
 * The binding model provides the following API:
 *
 * Module-level functions:
 * - `luablaze.new(schema_json[, options]) -> CompiledSchema`
 * - `luablaze.validate(compiled_schema, instance_table) -> boolean`
 * - `luablaze.validate_json(compiled_schema, instance_json_string) -> boolean`
 * - `luablaze.validate_detailed(compiled_schema, instance_table) -> boolean, report_table`
 * - `luablaze.validate_json_detailed(compiled_schema, instance_json_string) -> boolean, report_table`
 *
 * CompiledSchema methods:
 * - `CompiledSchema:validate(instance_table) -> boolean`
 * - `CompiledSchema:validate_json(instance_json_string) -> boolean`
 * - `CompiledSchema:validate_detailed(instance_table) -> boolean, report_table`
 * - `CompiledSchema:validate_json_detailed(instance_json_string) -> boolean, report_table`
 * - `CompiledSchema:evaluate(instance_table) -> boolean` (alias for validate)
 *
 * The schema is passed as a JSON string and parsed with `sourcemeta::core::parse_json`.
 * Instances can be provided either as Lua tables (converted to a JSON value) or as JSON
 * strings, depending on the method. Compilation produces a Blaze `Template` which is
 * stored in a Lua userdata and later evaluated.
 *
 * @section thread_safety Thread Safety
 *
 * **CompiledSchema objects are NOT thread-safe.**
 *
 * - Each CompiledSchema instance should be used by only one thread at a time
 * - If multiple threads need to validate against the same schema, each thread should
 *   create its own CompiledSchema instance via luablaze.new()
 * - Alternatively, use external synchronization (mutexes) to protect shared access
 * - The compilation process (luablaze.new) is thread-safe as long as each thread
 *   operates on different Lua states
 */

static constexpr const char *LUABLAZE_COMPILEDSCHEMA_MT = "luablaze.CompiledSchema";

// Module version information
static constexpr const char *LUABLAZE_VERSION           = "1.0.0";
static constexpr const char *LUABLAZE_NAME              = "luablaze";

// Blaze library version (passed from CMake)
#ifndef BLAZE_LIBRARY_VERSION
#define BLAZE_LIBRARY_VERSION "unknown"
#endif
static constexpr const char *BLAZE_VERSION                        = BLAZE_LIBRARY_VERSION;

// Default limits for table/JSON conversions
static constexpr std::size_t LUABLAZE_DEFAULT_MAX_ARRAY_LENGTH    = 100000;
static constexpr std::size_t LUABLAZE_DEFAULT_MAX_DEPTH           = 128;
static constexpr std::size_t LUABLAZE_DEFAULT_MAX_RECURSION_DEPTH = 100;

/**
 * @brief Convert a user-facing dialect identifier into a JSON Schema metaschema URI.
 *
 * Blaze/Core determine dialect via the schema's top-level `$schema`, but Blaze
 * also supports a `default_dialect` parameter for schemas that omit `$schema`.
 *
 * For the JSON-Schema-Test-Suite, the dialect is represented by the folder name
 * under `tests/` (e.g. `draft7`, `draft2019-09`, `draft2020-12`). This helper
 * maps those names to the appropriate metaschema URI.
 *
 * If the string already looks like a URI (contains "://"), it's treated as a
 * dialect URI and passed through unchanged.
 *
 * @param name The dialect name or URI
 * @return Optional metaschema URI, or nullopt if unrecognized
 */
static auto dialect_uri_from_name(const std::string_view name) -> std::optional<std::string> {
    if (name.find("://") != std::string::npos) {
        return std::string{name};
    }

    if (name == "draft2020-12") {
        return "https://json-schema.org/draft/2020-12/schema";
    }

    if (name == "draft2019-09") {
        return "https://json-schema.org/draft/2019-09/schema";
    }

    if (name == "draft7" || name == "draft-07") {
        return "http://json-schema.org/draft-07/schema#";
    }

    if (name == "draft6" || name == "draft-06") {
        return "http://json-schema.org/draft-06/schema#";
    }

    if (name == "draft4" || name == "draft-04") {
        return "http://json-schema.org/draft-04/schema#";
    }

    if (name == "draft3" || name == "draft-03") {
        return "http://json-schema.org/draft-03/schema#";
    }

    if (name == "draft2" || name == "draft-02") {
        return "http://json-schema.org/draft-02/schema#";
    }

    if (name == "draft1" || name == "draft-01") {
        return "http://json-schema.org/draft-01/schema#";
    }

    if (name == "draft0" || name == "draft-00") {
        return "http://json-schema.org/draft-00/schema#";
    }

    return std::nullopt;
}

static auto parse_mode_string(const std::string_view value) -> std::optional<sourcemeta::blaze::Mode> {
    if (value == "Fast" || value == "fast" || value == "FastValidation" || value == "fastvalidation") {
        return sourcemeta::blaze::Mode::FastValidation;
    }

    if (value == "Exhaustive" || value == "exhaustive") {
        return sourcemeta::blaze::Mode::Exhaustive;
    }

    return std::nullopt;
}

// Parse `luablaze.new` options table.
//
// Supported keys:
// - `mode`: "Fast" (default) or "Exhaustive"
// - `dialect`: test-suite folder name (e.g. "draft7") or a full dialect URI
static bool validate_options_table_keys(lua_State *L, const int index, std::string &error) {
    const int abs_index = lua_absindex(L, index);
    lua_pushnil(L);
    while (lua_next(L, abs_index) != 0) {
        if (!lua_isstring(L, -2)) {
            lua_pop(L, 2);
            error = "Options table keys must be strings";
            return false;
        }

        lua_pop(L, 1);
    }

    return true;
}

static bool parse_options_table(lua_State *L, const int index, sourcemeta::blaze::Mode &mode,
                                std::optional<std::string> &default_dialect, std::size_t &max_array_length,
                                std::size_t &max_depth, std::size_t &max_recursion_depth, std::string &error) {
    const int abs_index = lua_absindex(L, index);
    if (!validate_options_table_keys(L, abs_index, error)) {
        return false;
    }

    lua_getfield(L, abs_index, "mode");
    if (!lua_isnil(L, -1)) {
        if (!lua_isstring(L, -1)) {
            lua_pop(L, 1);
            error = "options.mode must be a string";
            return false;
        }
        std::size_t mode_len{0};
        const char *mode_str = lua_tolstring(L, -1, &mode_len);
        const auto mode_name = std::string_view{mode_str, mode_len};
        const auto parsed    = parse_mode_string(mode_name);
        if (!parsed.has_value()) {
            lua_pop(L, 1);
            error = "Unknown mode '" + std::string{mode_name} + "'";
            return false;
        }
        mode = parsed.value();
    }
    lua_pop(L, 1);

    lua_getfield(L, abs_index, "dialect");
    if (!lua_isnil(L, -1)) {
        if (!lua_isstring(L, -1)) {
            lua_pop(L, 1);
            error = "options.dialect must be a string";
            return false;
        }
        std::size_t dialect_len{0};
        const char *dialect_str = lua_tolstring(L, -1, &dialect_len);
        const auto dialect_name = std::string_view{dialect_str, dialect_len};
        default_dialect         = dialect_uri_from_name(dialect_name);
        if (!default_dialect.has_value()) {
            lua_pop(L, 1);
            error = "Unknown dialect '" + std::string{dialect_name} + "'";
            return false;
        }
    }
    lua_pop(L, 1);

    lua_getfield(L, abs_index, "max_array_length");
    if (!lua_isnil(L, -1)) {
        if (!lua_isinteger(L, -1)) {
            lua_pop(L, 1);
            error = "options.max_array_length must be an integer";
            return false;
        }
        const lua_Integer v = lua_tointeger(L, -1);
        if (v < 0) {
            lua_pop(L, 1);
            error = "options.max_array_length must be >= 0";
            return false;
        }
        max_array_length = static_cast<std::size_t>(v);
    }
    lua_pop(L, 1);

    lua_getfield(L, abs_index, "max_depth");
    if (!lua_isnil(L, -1)) {
        if (!lua_isinteger(L, -1)) {
            lua_pop(L, 1);
            error = "options.max_depth must be an integer";
            return false;
        }
        const lua_Integer v = lua_tointeger(L, -1);
        if (v < 0) {
            lua_pop(L, 1);
            error = "options.max_depth must be >= 0";
            return false;
        }
        max_depth = static_cast<std::size_t>(v);
    }
    lua_pop(L, 1);

    lua_getfield(L, abs_index, "max_recursion_depth");
    if (!lua_isnil(L, -1)) {
        if (!lua_isinteger(L, -1)) {
            lua_pop(L, 1);
            error = "options.max_recursion_depth must be an integer";
            return false;
        }
        const lua_Integer v = lua_tointeger(L, -1);
        if (v < 0) {
            lua_pop(L, 1);
            error = "options.max_recursion_depth must be >= 0";
            return false;
        }
        max_recursion_depth = static_cast<std::size_t>(v);
    }
    lua_pop(L, 1);

    return true;
}

/**
 * @brief Userdata payload for a compiled schema.
 *
 * We store the Blaze template by value directly inside the userdata.
 *
 * @var schema_template The compiled Blaze template
 * @var max_array_length Maximum array length when converting Lua tables to JSON (0 = unlimited)
 * @var max_depth Maximum nesting depth when parsing JSON strings (0 = unlimited)
 * @var max_recursion_depth Maximum recursion depth for table conversion (0 = unlimited)
 * @var mode_name Mode used for compilation ("Fast" or "Exhaustive")
 * @var dialect_name Dialect used for compilation
 */
struct CompiledSchema {
    sourcemeta::blaze::Template schema_template;
    sourcemeta::blaze::Evaluator evaluator;
    std::size_t max_array_length;
    std::size_t max_depth;
    std::size_t max_recursion_depth;
    const char *mode_name; // Static string pointer ("Fast" or "Exhaustive")
    std::string dialect_name;
};

static bool lua_value_to_json(lua_State *L, int index, std::unordered_set<const void *> &seen,
                              const std::size_t max_array_length, const std::size_t max_recursion_depth,
                              std::size_t depth, sourcemeta::core::JSON &out, std::string &error);

static bool lua_value_to_json_abs(lua_State *L, int abs_index, std::unordered_set<const void *> &seen,
                                  const std::size_t max_array_length, const std::size_t max_recursion_depth,
                                  std::size_t depth, sourcemeta::core::JSON &out, std::string &error);

static bool lua_table_to_json_abs(lua_State *L, int abs_index, std::unordered_set<const void *> &seen,
                                  const std::size_t max_array_length, const std::size_t max_recursion_depth,
                                  std::size_t depth, sourcemeta::core::JSON &out, std::string &error);

// Convert a JSON value to a Lua value and push it onto the stack.
// Returns true on success, false on error (with error message in error string).
static bool json_to_lua_value(lua_State *L, const sourcemeta::core::JSON &value, const std::size_t max_recursion_depth,
                              std::size_t depth, std::string &error);

static bool json_object_to_lua_table(lua_State *L, const sourcemeta::core::JSON &obj,
                                     const std::size_t max_recursion_depth, std::size_t depth, std::string &error) {
    // Ensure we have enough stack space for the table and at least one value
    if (!lua_checkstack(L, 3)) {
        error = "Cannot grow Lua stack (stack overflow risk)";
        return false;
    }
    lua_createtable(L, 0, static_cast<int>(obj.size()));
    for (const auto &pair : obj.as_object()) {
        if (!json_to_lua_value(L, pair.second, max_recursion_depth, depth + 1, error)) {
            lua_pop(L, 1); // Clean up partial table on error
            error = "Error converting JSON object property '" + pair.first + "': " + error;
            return false;
        }
        lua_setfield(L, -2, pair.first.c_str());
    }
    return true;
}

static bool json_array_to_lua_table(lua_State *L, const sourcemeta::core::JSON &arr,
                                    const std::size_t max_recursion_depth, std::size_t depth, std::string &error) {
    const std::size_t size = arr.size();
    // Ensure we have enough stack space for the table and at least one value
    if (!lua_checkstack(L, 3)) {
        error = "Cannot grow Lua stack (stack overflow risk)";
        return false;
    }
    lua_createtable(L, static_cast<int>(size), 0);
    for (std::size_t i = 0; i < size; i++) {
        if (!json_to_lua_value(L, arr.at(i), max_recursion_depth, depth + 1, error)) {
            lua_pop(L, 1); // Clean up partial table on error
            error = "Error converting JSON array at index " + std::to_string(i) + ": " + error;
            return false;
        }
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    return true;
}

static bool json_to_lua_value(lua_State *L, const sourcemeta::core::JSON &value, const std::size_t max_recursion_depth,
                              std::size_t depth, std::string &error) {
    // Check recursion depth to prevent stack overflow (0 = unlimited)
    if (max_recursion_depth > 0 && depth > max_recursion_depth) {
        error = "Maximum recursion depth exceeded in JSON conversion (depth=" + std::to_string(depth) + ")";
        return false;
    }

    if (value.is_null()) {
        lua_pushnil(L);
        return true;
    }
    if (value.is_boolean()) {
        lua_pushboolean(L, value.to_boolean());
        return true;
    }
    if (value.is_integer()) {
        lua_pushinteger(L, static_cast<lua_Integer>(value.to_integer()));
        return true;
    }
    if (value.is_real()) {
        lua_pushnumber(L, static_cast<lua_Number>(value.to_real()));
        return true;
    }
    if (value.is_string()) {
        const auto &str = value.to_string();
        lua_pushlstring(L, str.data(), str.size());
        return true;
    }
    if (value.is_array()) {
        return json_array_to_lua_table(L, value, max_recursion_depth, depth, error);
    }
    if (value.is_object()) {
        return json_object_to_lua_table(L, value, max_recursion_depth, depth, error);
    }
    error = "Unsupported JSON type for Lua conversion";
    return false;
}

static bool lua_table_to_json(lua_State *L, int index, std::unordered_set<const void *> &seen,
                              const std::size_t max_array_length, const std::size_t max_recursion_depth,
                              std::size_t depth, sourcemeta::core::JSON &out, std::string &error) {
    const int abs_index = lua_absindex(L, index);
    return lua_table_to_json_abs(L, abs_index, seen, max_array_length, max_recursion_depth, depth, out, error);
}

static bool lua_table_to_json_abs(lua_State *L, const int abs_index, std::unordered_set<const void *> &seen,
                                  const std::size_t max_array_length, const std::size_t max_recursion_depth,
                                  std::size_t depth, sourcemeta::core::JSON &out, std::string &error) {
    // Check recursion depth to prevent stack overflow (0 = unlimited)
    if (max_recursion_depth > 0 && depth > max_recursion_depth) {
        error = "Maximum recursion depth exceeded (depth=" + std::to_string(depth) + ")";
        return false;
    }

    const void *ptr = lua_topointer(L, abs_index);
    if (ptr != nullptr) {
        if (seen.find(ptr) != seen.end()) {
            error = "Cycle detected in Lua table";
            return false;
        }
        seen.insert(ptr);
    }

    bool is_array              = true;
    std::size_t max_index      = 0;
    std::size_t count_int_keys = 0;

    lua_pushnil(L);
    while (lua_next(L, abs_index) != 0) {
        if (lua_type(L, -2) == LUA_TNUMBER) {
            lua_Number k   = lua_tonumber(L, -2);
            lua_Integer ki = lua_tointeger(L, -2);
            if (static_cast<lua_Number>(ki) != k || ki <= 0) {
                is_array = false;
            } else {
                count_int_keys++;
                if (static_cast<std::size_t>(ki) > max_index) {
                    max_index = static_cast<std::size_t>(ki);
                }
            }
        } else {
            is_array = false;
        }
        lua_pop(L, 1);
    }

    if (is_array && count_int_keys == 0) {
        // Empty table: treat as object by default.
        is_array = false;
    }

    if (is_array) {
        // Check for potential integer overflow before loop
        if (max_index == SIZE_MAX) {
            error = "Array index too large (integer overflow risk)";
            if (ptr != nullptr) {
                seen.erase(ptr);
            }
            return false;
        }

        if (max_array_length > 0 && max_index > max_array_length) {
            error = "Array length exceeds max_array_length";
            if (ptr != nullptr) {
                seen.erase(ptr);
            }
            return false;
        }
        sourcemeta::core::JSON array{sourcemeta::core::JSON::Array{}};
        for (std::size_t i = 1; i <= max_index; i++) {
            lua_rawgeti(L, abs_index, static_cast<lua_Integer>(i));
            sourcemeta::core::JSON element{nullptr};
            if (!lua_isnil(L, -1)) {
                if (!lua_value_to_json_abs(L, lua_gettop(L), seen, max_array_length, max_recursion_depth, depth + 1,
                                           element, error)) {
                    lua_pop(L, 1);
                    if (ptr != nullptr) {
                        seen.erase(ptr);
                    }
                    return false;
                }
            }
            array.push_back(std::move(element));
            lua_pop(L, 1);
        }

        if (ptr != nullptr) {
            seen.erase(ptr);
        }
        out = std::move(array);
        return true;
    }

    sourcemeta::core::JSON object{sourcemeta::core::JSON::Object{}};
    lua_pushnil(L);
    while (lua_next(L, abs_index) != 0) {
        if (lua_type(L, -2) != LUA_TSTRING) {
            const int key_type = lua_type(L, -2);
            lua_pop(L, 2);
            error = std::string{"Object table keys must be strings (found "} + lua_typename(L, key_type) + ")";
            if (ptr != nullptr) {
                seen.erase(ptr);
            }
            return false;
        }
        std::size_t key_len{0};
        const char *key_str = lua_tolstring(L, -2, &key_len);
        const auto key      = std::string{key_str, key_len};
        sourcemeta::core::JSON value{nullptr};
        if (!lua_value_to_json_abs(L, lua_gettop(L), seen, max_array_length, max_recursion_depth, depth + 1, value,
                                   error)) {
            lua_pop(L, 1);
            if (ptr != nullptr) {
                seen.erase(ptr);
            }
            return false;
        }
        object.assign(key, std::move(value));
        lua_pop(L, 1);
    }

    if (ptr != nullptr) {
        seen.erase(ptr);
    }
    out = std::move(object);
    return true;
}

static bool lua_value_to_json(lua_State *L, int index, std::unordered_set<const void *> &seen,
                              const std::size_t max_array_length, const std::size_t max_recursion_depth,
                              std::size_t depth, sourcemeta::core::JSON &out, std::string &error) {
    const int abs_index = lua_absindex(L, index);
    return lua_value_to_json_abs(L, abs_index, seen, max_array_length, max_recursion_depth, depth, out, error);
}

static bool lua_value_to_json_abs(lua_State *L, const int abs_index, std::unordered_set<const void *> &seen,
                                  const std::size_t max_array_length, const std::size_t max_recursion_depth,
                                  std::size_t depth, sourcemeta::core::JSON &out, std::string &error) {
    // Check recursion depth to prevent stack overflow (0 = unlimited)
    if (max_recursion_depth > 0 && depth > max_recursion_depth) {
        error = "Maximum recursion depth exceeded (depth=" + std::to_string(depth) + ")";
        return false;
    }

    const int t = lua_type(L, abs_index);
    switch (t) {
        case LUA_TNIL:
            out = sourcemeta::core::JSON{nullptr};
            return true;
        case LUA_TBOOLEAN:
            out = sourcemeta::core::JSON{static_cast<bool>(lua_toboolean(L, abs_index))};
            return true;
        case LUA_TNUMBER: {
            if (lua_isinteger(L, abs_index)) {
                out = sourcemeta::core::JSON{static_cast<std::int64_t>(lua_tointeger(L, abs_index))};
                return true;
            }
            const double number = static_cast<double>(lua_tonumber(L, abs_index));
            if (!std::isfinite(number)) {
                error = "Non-finite numbers (NaN/Inf) are not valid JSON numbers";
                return false;
            }
            out = sourcemeta::core::JSON{number};
            return true;
        }
        case LUA_TSTRING: {
            std::size_t len{0};
            const char *str = lua_tolstring(L, abs_index, &len);
            out             = sourcemeta::core::JSON{std::string{str, len}};
            return true;
        }
        case LUA_TTABLE:
            return lua_table_to_json_abs(L, abs_index, seen, max_array_length, max_recursion_depth, depth, out, error);
        default: {
            std::ostringstream oss;
            oss << "Unsupported Lua type for JSON conversion: " << lua_typename(L, t);
            if (t == LUA_TUSERDATA || t == LUA_TLIGHTUSERDATA) {
                oss << " (userdata cannot be converted to JSON)";
            } else if (t == LUA_TFUNCTION) {
                oss << " (functions cannot be converted to JSON)";
            } else if (t == LUA_TTHREAD) {
                oss << " (threads cannot be converted to JSON)";
            }
            error = oss.str();
            return false;
        }
    }
}

static auto parse_json_with_depth_limit(const std::string_view input, const std::size_t max_depth)
    -> sourcemeta::core::JSON {
    if (max_depth == 0) {
        return sourcemeta::core::parse_json(std::string{input});
    }

    std::size_t depth{0};
    const sourcemeta::core::JSON::ParseCallback cb = [&](const sourcemeta::core::JSON::ParsePhase phase,
                                                         const sourcemeta::core::JSON::Type type, const std::uint64_t,
                                                         const std::uint64_t, const sourcemeta::core::JSON &) {
        if (type == sourcemeta::core::JSON::Type::Array || type == sourcemeta::core::JSON::Type::Object) {
            if (phase == sourcemeta::core::JSON::ParsePhase::Pre) {
                depth++;
                if (depth > max_depth) {
                    throw std::runtime_error("JSON maximum nesting depth exceeded");
                }
            } else {
                if (depth > 0) {
                    depth--;
                }
            }
        }
    };

    return sourcemeta::core::parse_json(std::string{input}, cb);
}

// Validate and extract a `CompiledSchema*` from a Lua userdata at `index`.
// Raises a Lua error if the type does not match.
static auto check_compiled_schema(lua_State *L, const int index) -> CompiledSchema * {
    auto *ud = static_cast<CompiledSchema *>(luaL_checkudata(L, index, LUABLAZE_COMPILEDSCHEMA_MT));
    luaL_argcheck(L, ud != nullptr, index, "CompiledSchema expected");
    return ud;
}

// Lua GC metamethod: destroy the object stored inside the userdata.
static int compiled_schema_gc(lua_State *L) {
    auto *ud = static_cast<CompiledSchema *>(luaL_checkudata(L, 1, LUABLAZE_COMPILEDSCHEMA_MT));
    if (ud != nullptr) {
        ud->~CompiledSchema();
    }
    return 0;
}

/**
 * @brief Return information about the compiled schema configuration.
 *
 * Implements `CompiledSchema:info() -> table`
 *
 * Returns a table containing:
 * - mode: "Fast" or "Exhaustive"
 * - dialect: The dialect used or "auto" if auto-detected
 * - max_array_length: Maximum array length for Lua table conversion
 * - max_depth: Maximum JSON nesting depth
 * - max_recursion_depth: Maximum recursion depth for conversion
 * - luablaze_version: Version of luablaze
 * - blaze_version: Version of the Blaze library
 *
 * @param L Lua state
 * @return 1 (info table on stack)
 */
static int compiled_schema_info(lua_State *L) {
    auto *compiled = check_compiled_schema(L, 1);

    // Ensure we have enough stack space (7 key-value pairs + table)
    if (!lua_checkstack(L, 15)) {
        return luaL_error(L, "Cannot grow Lua stack for info table");
    }

    lua_createtable(L, 0, 7);

    lua_pushstring(L, compiled->mode_name);
    lua_setfield(L, -2, "mode");

    lua_pushstring(L, compiled->dialect_name.c_str());
    lua_setfield(L, -2, "dialect");

    lua_pushinteger(L, static_cast<lua_Integer>(compiled->max_array_length));
    lua_setfield(L, -2, "max_array_length");

    lua_pushinteger(L, static_cast<lua_Integer>(compiled->max_depth));
    lua_setfield(L, -2, "max_depth");

    lua_pushinteger(L, static_cast<lua_Integer>(compiled->max_recursion_depth));
    lua_setfield(L, -2, "max_recursion_depth");

    lua_pushstring(L, LUABLAZE_VERSION);
    lua_setfield(L, -2, "luablaze_version");

    lua_pushstring(L, BLAZE_VERSION);
    lua_setfield(L, -2, "blaze_version");

    return 1;
}

/**
 * @brief Validate a Lua table against the compiled schema (simple boolean result).
 *
 * Implements `CompiledSchema:validate(instance_table) -> boolean`
 *
 * Converts the Lua table at stack index 2 to a JSON value, then validates it
 * against the compiled schema template.
 *
 * @param L Lua state
 * @return 1 (boolean result on stack)
 * @throws Lua error on conversion or validation failure
 */
static int compiled_schema_validate(lua_State *L) {
    auto *compiled = check_compiled_schema(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);

    try {
        std::unordered_set<const void *> seen;
        seen.reserve(32);
        sourcemeta::core::JSON instance{nullptr};
        std::string error;
        if (!lua_value_to_json(L, 2, seen, compiled->max_array_length, compiled->max_recursion_depth, 0, instance,
                               error)) {
            throw std::runtime_error(error);
        }
        const bool result = compiled->evaluator.validate(compiled->schema_template, instance);
        lua_pushboolean(L, result);
        return 1;
    } catch (const std::exception &e) {
        return luaL_error(L, "%s", e.what());
    } catch (...) {
        return luaL_error(L, "unknown error");
    }
}

/**
 * @brief Validate a JSON string against the compiled schema (simple boolean result).
 *
 * Implements `CompiledSchema:validate_json(instance_json_string) -> boolean`
 *
 * Parses the JSON string at stack index 2, then validates it against the compiled
 * schema template.
 *
 * @param L Lua state
 * @return 1 (boolean result on stack)
 * @throws Lua error on parse or validation failure
 */
static int compiled_schema_validate_json(lua_State *L) {
    auto *compiled = check_compiled_schema(L, 1);
    std::size_t instance_len{0};
    const char *instance_str = luaL_checklstring(L, 2, &instance_len);

    try {
        const auto instance =
            parse_json_with_depth_limit(std::string_view{instance_str, instance_len}, compiled->max_depth);
        const bool result = compiled->evaluator.validate(compiled->schema_template, instance);
        lua_pushboolean(L, result);
        return 1;
    } catch (const std::exception &e) {
        return luaL_error(L, "%s", e.what());
    } catch (...) {
        return luaL_error(L, "unknown error");
    }
}

/**
 * @brief Validate a Lua table against the compiled schema with detailed report.
 *
 * Implements `CompiledSchema:validate_detailed(instance_table) -> boolean, report_table`
 *
 * Converts the Lua table at stack index 2 to a JSON value, validates it against
 * the compiled schema template, and returns both the validation result and a detailed
 * report in JSON Schema "basic" output format.
 *
 * @param L Lua state
 * @return 2 (boolean result and report table on stack)
 * @throws Lua error on conversion or validation failure
 */
static int compiled_schema_validate_detailed(lua_State *L) {
    auto *compiled = check_compiled_schema(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);

    try {
        std::unordered_set<const void *> seen;
        seen.reserve(32);
        sourcemeta::core::JSON instance{nullptr};
        std::string error;
        if (!lua_value_to_json(L, 2, seen, compiled->max_array_length, compiled->max_recursion_depth, 0, instance,
                               error)) {
            throw std::runtime_error(error);
        }
        const auto result{sourcemeta::blaze::standard(compiled->evaluator, compiled->schema_template, instance,
                                                      sourcemeta::blaze::StandardOutput::Basic)};

        // Extract the "valid" field from the result
        const bool is_valid =
            result.defines("valid") && result.at("valid").is_boolean() ? result.at("valid").to_boolean() : false;

        lua_pushboolean(L, is_valid);

        // Convert the full result to a Lua table for detailed reporting
        if (!json_to_lua_value(L, result, compiled->max_recursion_depth, 0, error)) {
            throw std::runtime_error(error);
        }

        return 2; // Return (boolean, table)
    } catch (const std::exception &e) {
        return luaL_error(L, "%s", e.what());
    } catch (...) {
        return luaL_error(L, "unknown error");
    }
}

/**
 * @brief Validate a JSON string against the compiled schema with detailed report.
 *
 * Implements `CompiledSchema:validate_json_detailed(instance_json_string) -> boolean, report_table`
 *
 * Parses the JSON string at stack index 2, validates it against the compiled schema
 * template, and returns both the validation result and a detailed report in JSON Schema
 * "basic" output format.
 *
 * @param L Lua state
 * @return 2 (boolean result and report table on stack)
 * @throws Lua error on parse or validation failure
 */
static int compiled_schema_validate_json_detailed(lua_State *L) {
    auto *compiled = check_compiled_schema(L, 1);
    std::size_t instance_len{0};
    const char *instance_str = luaL_checklstring(L, 2, &instance_len);

    try {
        const auto instance =
            parse_json_with_depth_limit(std::string_view{instance_str, instance_len}, compiled->max_depth);
        const auto result{sourcemeta::blaze::standard(compiled->evaluator, compiled->schema_template, instance,
                                                      sourcemeta::blaze::StandardOutput::Basic)};

        // Extract the "valid" field from the result
        const bool is_valid =
            result.defines("valid") && result.at("valid").is_boolean() ? result.at("valid").to_boolean() : false;

        lua_pushboolean(L, is_valid);

        // Convert the full result to a Lua table for detailed reporting
        std::string error;
        if (!json_to_lua_value(L, result, compiled->max_recursion_depth, 0, error)) {
            throw std::runtime_error(error);
        }

        return 2; // Return (boolean, table)
    } catch (const std::exception &e) {
        return luaL_error(L, "%s", e.what());
    } catch (...) {
        return luaL_error(L, "unknown error");
    }
}

/**
 * @brief Alias for validate() - evaluates a Lua table against the compiled schema.
 *
 * Implements `CompiledSchema:evaluate(instance_table) -> boolean`
 *
 * For this binding, `evaluate` is an alias of `validate`.
 *
 * @param L Lua state
 * @return 1 (boolean result on stack)
 * @see compiled_schema_validate
 */
static int compiled_schema_evaluate(lua_State *L) {
    return compiled_schema_validate(L);
}

/**
 * @brief Compile a JSON Schema string into a reusable compiled schema object.
 *
 * Implements `luablaze.new(schema_json[, options]) -> CompiledSchema`
 *
 * Compiles a JSON Schema string into a Blaze template. Options can control:
 * - `dialect`: JSON Schema dialect (e.g., "draft7", "draft2020-12", or a $schema URI)
 * - `mode`: Compilation mode ("Fast" or "Exhaustive")
 * - `max_array_length`: Maximum array length for Lua table to JSON conversion (default: 100000, 0 = unlimited)
 * - `max_depth`: Maximum nesting depth for JSON parsing (default: 128, 0 = unlimited)
 *
 * @param L Lua state (expects schema_json_string at index 1, optional options table at index 2)
 * @return 1 (CompiledSchema userdata on stack)
 * @throws Lua error on parse or compilation failure
 *
 * @code{.lua}
 * local schema = luablaze.new([[{"type": "string"}]])
 * local schema2 = luablaze.new(schema_json, { dialect = "draft7", mode = "Exhaustive" })
 * @endcode
 */
static int luablaze_new(lua_State *L) {
    std::size_t schema_len{0};
    const char *schema_str = luaL_checklstring(L, 1, &schema_len);

    if (schema_len == 0) {
        return luaL_error(L, "schema cannot be empty");
    }

    std::optional<std::string> default_dialect{std::nullopt};
    sourcemeta::blaze::Mode mode{sourcemeta::blaze::Mode::FastValidation};
    std::size_t max_array_length{LUABLAZE_DEFAULT_MAX_ARRAY_LENGTH};
    std::size_t max_depth{LUABLAZE_DEFAULT_MAX_DEPTH};
    std::size_t max_recursion_depth{LUABLAZE_DEFAULT_MAX_RECURSION_DEPTH};

    try {
        // Supported call patterns:
        //   new(schema)
        //   new(schema, { dialect = "draft7", mode = "Exhaustive" })
        //
        // NOTE: Positional dialect arguments are not supported.
        if (!lua_isnoneornil(L, 2)) {
            if (lua_type(L, 2) != LUA_TTABLE) {
                throw std::runtime_error("options_table must be a table");
            }
            std::string options_error;
            if (!parse_options_table(L, 2, mode, default_dialect, max_array_length, max_depth, max_recursion_depth,
                                     options_error)) {
                throw std::runtime_error(options_error);
            }
        }

        if (!lua_isnoneornil(L, 3)) {
            throw std::runtime_error("luablaze.new expects (schema_json) or (schema_json, options_table)");
        }

        // Capture mode and dialect names for introspection
        const char *mode_name_ptr          = (mode == sourcemeta::blaze::Mode::FastValidation) ? "Fast" : "Exhaustive";
        const std::string dialect_name_str = default_dialect.value_or("auto");

        const auto schema = parse_json_with_depth_limit(std::string_view{schema_str, schema_len}, max_depth);
        const auto schema_template{
            sourcemeta::blaze::compile(schema, sourcemeta::core::schema_walker, sourcemeta::core::schema_resolver,
                                       sourcemeta::blaze::default_schema_compiler, mode, default_dialect)};

        auto *ud = static_cast<CompiledSchema *>(lua_newuserdata(L, sizeof(CompiledSchema)));
        new (ud) CompiledSchema{schema_template,     sourcemeta::blaze::Evaluator{},
                                max_array_length,    max_depth,
                                max_recursion_depth, mode_name_ptr,
                                dialect_name_str};
        luaL_getmetatable(L, LUABLAZE_COMPILEDSCHEMA_MT);
        lua_setmetatable(L, -2);
        return 1;
    } catch (const std::exception &e) {
        return luaL_error(L, "%s", e.what());
    } catch (...) {
        return luaL_error(L, "unknown error");
    }
}

// Implements `luablaze.validate(compiled_schema, instance_table)`.
//
// Functional form that delegates to the method implementation.
static int luablaze_validate(lua_State *L) {
    (void)check_compiled_schema(L, 1);
    return compiled_schema_validate(L);
}

static int luablaze_validate_json(lua_State *L) {
    (void)check_compiled_schema(L, 1);
    return compiled_schema_validate_json(L);
}

static int luablaze_validate_detailed(lua_State *L) {
    (void)check_compiled_schema(L, 1);
    return compiled_schema_validate_detailed(L);
}

static int luablaze_validate_json_detailed(lua_State *L) {
    (void)check_compiled_schema(L, 1);
    return compiled_schema_validate_json_detailed(L);
}

// Module function table for `require("luablaze")`.
static const struct luaL_Reg luablaze_functions[] = {
    {"new", luablaze_new},
    {"validate", luablaze_validate},
    {"validate_json", luablaze_validate_json},
    {"validate_detailed", luablaze_validate_detailed},
    {"validate_json_detailed", luablaze_validate_json_detailed},
    {NULL, NULL},
};

// Module entrypoint for `require("luablaze")`.
//
// Registers the `CompiledSchema` metatable (methods + __gc), then returns the
// module table containing `new` and `validate`.
LUABLAZE_EXPORT int luaopen_luablaze(lua_State *L) {
    luaL_newmetatable(L, LUABLAZE_COMPILEDSCHEMA_MT);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    static const luaL_Reg compiled_schema_methods[] = {
        {"validate", compiled_schema_validate},
        {"validate_json", compiled_schema_validate_json},
        {"validate_detailed", compiled_schema_validate_detailed},
        {"validate_json_detailed", compiled_schema_validate_json_detailed},
        {"evaluate", compiled_schema_evaluate},
        {"info", compiled_schema_info},
        {"__gc", compiled_schema_gc},
        {NULL, NULL},
    };
    luaL_setfuncs(L, compiled_schema_methods, 0);
    lua_pop(L, 1);

    luaL_newlib(L, luablaze_functions);

    // Add version information to the module table
    lua_pushstring(L, LUABLAZE_VERSION);
    lua_setfield(L, -2, "_VERSION");

    lua_pushstring(L, LUABLAZE_NAME);
    lua_setfield(L, -2, "_NAME");

    lua_pushstring(L, BLAZE_VERSION);
    lua_setfield(L, -2, "_BLAZE_VERSION");

    return 1;
}
