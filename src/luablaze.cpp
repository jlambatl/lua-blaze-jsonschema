#include "luablaze.h"

#include <sourcemeta/core/json.h>
#include <sourcemeta/core/jsonschema.h>

#include <sourcemeta/blaze/compiler.h>
#include <sourcemeta/blaze/evaluator.h>
#include <sourcemeta/blaze/output_standard.h>

#include <cmath>
#include <new>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>

// Lua module: `luablaze`
//
// This file implements a Lua C module that binds the Sourcemeta Blaze JSON
// Schema compiler/evaluator.
//
// The binding model is:
// - `luablaze.new(schema_json[, dialect_or_options[, options]]) -> CompiledSchema`
// - `CompiledSchema:validate(instance_json) -> boolean`
// - `CompiledSchema:evaluate(instance_json) -> boolean` (alias)
// - `luablaze.validate(compiled_schema, instance_json) -> boolean`
//
// The schema is passed as a JSON string and parsed with
// `sourcemeta::core::parse_json`. Instances can be provided either as Lua tables
// (converted to a JSON value) or as JSON strings, depending on the method.
// Compilation produces a Blaze `Template` which is stored in a Lua userdata and
// later evaluated.

static constexpr const char *LUABLAZE_COMPILEDSCHEMA_MT = "luablaze.CompiledSchema";

// Convert a user-facing dialect identifier into a JSON Schema metaschema URI.
//
// Blaze/Core determine dialect via the schema's top-level `$schema`, but Blaze
// also supports a `default_dialect` parameter for schemas that omit `$schema`.
//
// For the JSON-Schema-Test-Suite, the dialect is represented by the folder name
// under `tests/` (e.g. `draft7`, `draft2019-09`, `draft2020-12`). This helper
// maps those names to the appropriate metaschema URI.
//
// If the string already looks like a URI (contains "://"), it's treated as a
// dialect URI and passed through unchanged.
static auto dialect_uri_from_name(const std::string &name) -> std::optional<std::string> {
    if (name.find("://") != std::string::npos) {
        return name;
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

static auto parse_mode_string(const std::string &value) -> std::optional<sourcemeta::blaze::Mode> {
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
                                std::size_t &max_depth, std::string &error) {
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
        const auto mode_name = std::string{mode_str, mode_len};
        const auto parsed    = parse_mode_string(mode_name);
        if (!parsed.has_value()) {
            lua_pop(L, 1);
            error = "Unknown mode '" + mode_name + "'";
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
        const auto dialect_name = std::string{dialect_str, dialect_len};
        default_dialect         = dialect_uri_from_name(dialect_name);
        if (!default_dialect.has_value()) {
            lua_pop(L, 1);
            error = "Unknown dialect '" + dialect_name + "'";
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

    return true;
}

// Userdata payload for a compiled schema.
//
// We store the Blaze template by value directly inside the userdata.
struct CompiledSchema {
    sourcemeta::blaze::Template schema_template;
    std::size_t max_array_length;
    std::size_t max_depth;
};

static bool lua_value_to_json(lua_State *L, int index, std::unordered_set<const void *> &seen,
                              const std::size_t max_array_length, sourcemeta::core::JSON &out, std::string &error);

static bool lua_table_to_json(lua_State *L, int index, std::unordered_set<const void *> &seen,
                              const std::size_t max_array_length, sourcemeta::core::JSON &out, std::string &error) {
    const int abs_index = lua_absindex(L, index);
    const void *ptr     = lua_topointer(L, abs_index);
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
                if (!lua_value_to_json(L, -1, seen, max_array_length, element, error)) {
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
            lua_pop(L, 2);
            error = "Object table keys must be strings";
            if (ptr != nullptr) {
                seen.erase(ptr);
            }
            return false;
        }
        std::size_t key_len{0};
        const char *key_str = lua_tolstring(L, -2, &key_len);
        const auto key      = std::string{key_str, key_len};
        sourcemeta::core::JSON value{nullptr};
        if (!lua_value_to_json(L, -1, seen, max_array_length, value, error)) {
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
                              const std::size_t max_array_length, sourcemeta::core::JSON &out, std::string &error) {
    const int abs_index = lua_absindex(L, index);
    const int t         = lua_type(L, abs_index);
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
            return lua_table_to_json(L, abs_index, seen, max_array_length, out, error);
        default:
            error = std::string{"Unsupported Lua type for JSON conversion: "} + lua_typename(L, t);
            return false;
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

// Implements `CompiledSchema:validate(instance_table)`.
static int compiled_schema_validate(lua_State *L) {
    auto *compiled = check_compiled_schema(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);

    try {
        std::unordered_set<const void *> seen;
        sourcemeta::core::JSON instance{nullptr};
        std::string error;
        if (!lua_value_to_json(L, 2, seen, compiled->max_array_length, instance, error)) {
            throw std::runtime_error(error);
        }
        sourcemeta::blaze::Evaluator evaluator;
        const bool result = evaluator.validate(compiled->schema_template, instance);
        lua_pushboolean(L, result);
        return 1;
    } catch (const std::exception &e) {
        return luaL_error(L, "%s", e.what());
    } catch (...) {
        return luaL_error(L, "unknown error");
    }
}

static int compiled_schema_validate_json(lua_State *L) {
    auto *compiled = check_compiled_schema(L, 1);
    std::size_t instance_len{0};
    const char *instance_str = luaL_checklstring(L, 2, &instance_len);

    try {
        const auto instance =
            parse_json_with_depth_limit(std::string_view{instance_str, instance_len}, compiled->max_depth);
        sourcemeta::blaze::Evaluator evaluator;
        const bool result = evaluator.validate(compiled->schema_template, instance);
        lua_pushboolean(L, result);
        return 1;
    } catch (const std::exception &e) {
        return luaL_error(L, "%s", e.what());
    } catch (...) {
        return luaL_error(L, "unknown error");
    }
}

static int compiled_schema_validate_output(lua_State *L) {
    auto *compiled = check_compiled_schema(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);

    try {
        std::unordered_set<const void *> seen;
        sourcemeta::core::JSON instance{nullptr};
        std::string error;
        if (!lua_value_to_json(L, 2, seen, compiled->max_array_length, instance, error)) {
            throw std::runtime_error(error);
        }
        sourcemeta::blaze::Evaluator evaluator;
        const auto result{sourcemeta::blaze::standard(evaluator, compiled->schema_template, instance,
                                                      sourcemeta::blaze::StandardOutput::Basic)};
        std::ostringstream out;
        sourcemeta::core::stringify(result, out);
        const auto serialized = out.str();
        lua_pushlstring(L, serialized.c_str(), serialized.size());
        return 1;
    } catch (const std::exception &e) {
        return luaL_error(L, "%s", e.what());
    } catch (...) {
        return luaL_error(L, "unknown error");
    }
}

static int compiled_schema_validate_output_json(lua_State *L) {
    auto *compiled = check_compiled_schema(L, 1);
    std::size_t instance_len{0};
    const char *instance_str = luaL_checklstring(L, 2, &instance_len);

    try {
        const auto instance =
            parse_json_with_depth_limit(std::string_view{instance_str, instance_len}, compiled->max_depth);
        sourcemeta::blaze::Evaluator evaluator;
        const auto result{sourcemeta::blaze::standard(evaluator, compiled->schema_template, instance,
                                                      sourcemeta::blaze::StandardOutput::Basic)};
        std::ostringstream out;
        sourcemeta::core::stringify(result, out);
        const auto serialized = out.str();
        lua_pushlstring(L, serialized.c_str(), serialized.size());
        return 1;
    } catch (const std::exception &e) {
        return luaL_error(L, "%s", e.what());
    } catch (...) {
        return luaL_error(L, "unknown error");
    }
}

// Implements `CompiledSchema:evaluate(instance_json)`.
//
// For this binding, `evaluate` is an alias of `validate`.
static int compiled_schema_evaluate(lua_State *L) {
    return compiled_schema_validate(L);
}

// Implements `luablaze.new(schema_json[, dialect_or_options[, options]])`.
//
// Compiles a JSON Schema string into a Blaze template. If `dialect` is provided
// (either as the second argument string, or as `options.dialect`), it is passed
// as Blaze's `default_dialect` so that schemas without `$schema` can still be
// compiled under the correct rules.
//
// The compilation `mode` can be controlled using `options.mode`:
// - "Fast" (default): attempt to short-circuit to a boolean result
// - "Exhaustive": perform exhaustive evaluation, including annotations
static int luablaze_new(lua_State *L) {
    std::size_t schema_len{0};
    const char *schema_str = luaL_checklstring(L, 1, &schema_len);

    std::optional<std::string> default_dialect{std::nullopt};
    sourcemeta::blaze::Mode mode{sourcemeta::blaze::Mode::FastValidation};
    std::size_t max_array_length{100000};
    std::size_t max_depth{128};

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
            if (!parse_options_table(L, 2, mode, default_dialect, max_array_length, max_depth, options_error)) {
                throw std::runtime_error(options_error);
            }
        }

        if (!lua_isnoneornil(L, 3)) {
            throw std::runtime_error("luablaze.new expects (schema_json) or (schema_json, options_table)");
        }

        const auto schema = parse_json_with_depth_limit(std::string_view{schema_str, schema_len}, max_depth);
        const auto schema_template{
            sourcemeta::blaze::compile(schema, sourcemeta::core::schema_walker, sourcemeta::core::schema_resolver,
                                       sourcemeta::blaze::default_schema_compiler, mode, default_dialect)};

        auto *ud = static_cast<CompiledSchema *>(lua_newuserdata(L, sizeof(CompiledSchema)));
        new (ud) CompiledSchema{schema_template, max_array_length, max_depth};
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

static int luablaze_validate_output(lua_State *L) {
    (void)check_compiled_schema(L, 1);
    return compiled_schema_validate_output(L);
}

static int luablaze_validate_output_json(lua_State *L) {
    (void)check_compiled_schema(L, 1);
    return compiled_schema_validate_output_json(L);
}

// Module function table for `require("luablaze")`.
static const struct luaL_Reg luablaze_functions[] = {
    {"new", luablaze_new},
    {"validate", luablaze_validate},
    {"validate_json", luablaze_validate_json},
    {"validate_output", luablaze_validate_output},
    {"validate_output_json", luablaze_validate_output_json},
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
        {"validate_output", compiled_schema_validate_output},
        {"validate_output_json", compiled_schema_validate_output_json},
        {"evaluate", compiled_schema_evaluate},
        {"__gc", compiled_schema_gc},
        {NULL, NULL},
    };
    luaL_setfuncs(L, compiled_schema_methods, 0);
    lua_pop(L, 1);

    luaL_newlib(L, luablaze_functions);
    return 1;
}
