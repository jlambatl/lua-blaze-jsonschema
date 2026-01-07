# lua-blaze-jsonschema

Lua bindings for the [sourcemeta/blaze](https://github.com/sourcemeta/blaze) JSON Schema compiler/evaluator.

This project builds a Lua C module named `luablaze` that lets you:

- Compile a JSON Schema (provided as a JSON string) into a reusable compiled object.
- Validate JSON instances (provided as JSON strings) against that compiled schema.

### API

#### `luablaze.new(schema_json_string[, dialect_or_options[, options]]) -> CompiledSchema`

Compiles the given JSON Schema (as a string) and returns a `CompiledSchema` userdata.

`luablaze.new` supports these call patterns:

```lua
luablaze.new(schema_json)
luablaze.new(schema_json, { dialect = "draft7" })
luablaze.new(schema_json, { mode = "Exhaustive" })
luablaze.new(schema_json, { dialect = "draft7", mode = "Exhaustive" })
luablaze.new(schema_json, { max_array_length = 100000 })
luablaze.new(schema_json, { max_depth = 128 })
```

- `dialect` may be a JSON-Schema-Test-Suite folder name like `draft7`,
  `draft2019-09`, `draft2020-12`, or a full `$schema` URI.
- `mode` can be `"Fast"` (default) or `"Exhaustive"`.
- `max_array_length` limits the maximum array length produced when converting Lua tables to JSON values. Default: `100000`. Use `0` for unlimited.
- `max_depth` limits maximum nesting depth when parsing schema/instance JSON strings (for `new`, `validate_json`, and `validate_output_json`). Default: `128`. Use `0` for unlimited.

#### `CompiledSchema:validate(instance_json_string) -> boolean`

Validates a Lua table (decoded JSON-like structure) against the compiled schema.

Table conversion rules:

- **Supported Lua value types**
  - **nil**: JSON null
  - **boolean**: JSON boolean
  - **number**: JSON number (integers are preserved as integers when Lua marks them as integers). Non-finite numbers (`NaN`, `Inf`, `-Inf`) raise an error.
  - **string**: JSON string
  - **table**: JSON array or object (see below)
  - Any other Lua type will raise an error.
- **Array vs object detection for Lua tables**
  - A table is treated as a JSON **array** only if **all keys are positive integers**.
  - Arrays may be **sparse**: any missing indices in `1..max_index` are converted to explicit JSON **null** entries.
  - If a table has any non-integer key or non-positive integer key, it is treated as a JSON **object**.
  - **Empty tables** are treated as JSON **objects**.
- **Object keys**
  - When a table is treated as an object, **all keys must be strings**. Non-string keys will raise an error.
- **Cycles**
  - Cycles in tables are detected and will raise an error.

If conversion fails (for example, due to cycles, invalid key types, unsupported Lua value types, or non-finite numbers), the module raises a Lua error.

#### `CompiledSchema:validate_json(instance_json_string) -> boolean`

Parses and validates the JSON instance string against the compiled schema.

#### `CompiledSchema:validate_output(instance_json_string) -> string`

Validates a Lua table (decoded JSON-like structure) against the compiled schema, returning
a JSON string containing the validation output in the JSON Schema "basic" output format.

#### `CompiledSchema:validate_output_json(instance_json_string) -> string`

Parses and validates the JSON instance string against the compiled schema, returning
a JSON string containing the validation output in the JSON Schema "basic" output format.

#### `CompiledSchema:evaluate(instance_json_string) -> boolean`

Alias for `CompiledSchema:validate`.

#### `luablaze.validate(compiled_schema, instance_json_string) -> boolean`

Functional form of validation.

## Usage

```lua
local luablaze = require("luablaze")

local schema = luablaze.new([[
  {
    "type": "object",
    "properties": { "name": { "type": "string" } },
    "required": ["name"],
    "additionalProperties": false
  }
]])

print(schema:validate({ name = "Ada" })) -- true
print(schema:validate({ name = 123 })) -- false
print(schema:validate_json([[{"name":"Ada"}]]) ) -- true
print(luablaze.validate(schema, { name = "Ada" })) -- true
```

Notes:

- `luablaze.new` always takes a **schema JSON string** (parsed using `sourcemeta::core::parse_json`).
- `CompiledSchema:validate`/`validate_output` take **Lua tables** (converted to a JSON value internally).
- `CompiledSchema:validate_json`/`validate_output_json` take **instance JSON strings** (parsed using `sourcemeta::core::parse_json`).
- On parse/compile errors, the module raises a Lua error with the underlying C++ exception message.

## Building

### Requirements

- CMake >= 3.23
- A C++20 compiler
- Lua >= 5.2 (headers + library)
- Git (only needed if you want CMake to auto-init submodules)
- LuaRocks (optional, for `luarocks make`)

### Submodules

This repository uses git submodules for dependencies under `deps/`.

If you cloned the repository without submodules, initialize them with:

```sh
git submodule update --init --recursive
```

In particular, `deps/blaze` will not be present unless you initialize
submodules.

### Build with LuaRocks

From a git checkout:

```sh
luarocks make
```

If you have multiple rockspecs in the directory, run:

```sh
luarocks make luablaze-scm-1.rockspec
```

Notes:

- The build uses CMake under the hood.
- Submodules are initialized during the build (equivalent to `git submodule update --init --recursive`).

You can verify the install with:

```sh
lua -e 'local luablaze=require("luablaze"); print("ok")'
```

### Build steps

```sh
cmake -S . -B build
cmake --build build
```

By default, configuring will run:

```sh
git submodule update --init --recursive
```

You can disable that behavior with:

```sh
cmake -S . -B build -DLUABLAZE_INIT_SUBMODULES=OFF
```

### Using the built module

The build produces a Lua module named `luablaze` (typically `luablaze.so` on Linux/macOS).

To load it from Lua, ensure Lua can find it, for example:

```sh
export LUA_CPATH="$(pwd)/build/?.so;;"
lua -e 'local luablaze=require("luablaze"); print("ok")'
```

## License

This repository's code is licensed under the MIT License (see `LICENSE`).

It links against third-party dependencies (notably `deps/blaze`) that have
their own licenses. See `THIRD_PARTY_LICENSES.md` for details.
