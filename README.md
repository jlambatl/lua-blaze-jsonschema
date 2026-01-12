# lua-blaze-jsonschema

Lua bindings for the [sourcemeta/blaze](https://github.com/sourcemeta/blaze) JSON Schema compiler/evaluator.

This project builds a Lua C module named `luablaze` that lets you:

- Compile a JSON Schema (provided as a JSON string) into a reusable compiled object.
- Validate JSON instances (provided as JSON strings) against that compiled schema.

## Performaance

As of the time of writing this, lua-schema `luarocks install lua-schema` was the most performant and compliant 
lua implementation of json-schema validation available. This project was created to provide a faster alternative.

* On average, luablaze appears to be roughly 4.5x to 5x faster across standard validation scenarios
* In some cases, luablaze can be up to 10x faster
* Worst case scenarios show luablaze to be roughly 2x faster

There are probably schemas and tests that lua-schema performs better on, if you find one please let me know!

It's also worth noting, lua-schema is awesome and I'm not trying to take anything away from it. This project 
was created to provide a faster alternative for certain use cases. The tradeoff is that lua-schema is a pure lua 
implementation while luablaze is a C/C++ module that uses the sourcemeta/blaze library.

### API

#### Module Constants

- `luablaze._VERSION` (string) - Module version, e.g. `"1.0.0"`
- `luablaze._NAME` (string) - Module name, always `"luablaze"`
- `luablaze._BLAZE_VERSION` (string) - Sourcemeta Blaze library version, e.g. `"0.0.1"`

#### `luablaze.new(schema_json_string[, options]) -> CompiledSchema`

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
- `max_depth` limits maximum nesting depth when parsing schema/instance JSON strings (for `new`, `validate_json`, and `validate_json_detailed`). Default: `128`. Use `0` for unlimited.

#### `CompiledSchema:validate(instance_table) -> boolean`

Validates a Lua table (decoded JSON-like structure) against the compiled schema. Returns `true` if valid, `false` otherwise.

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

Parses and validates the JSON instance string against the compiled schema. Returns `true` if valid, `false` otherwise.

#### `CompiledSchema:validate_detailed(instance_table) -> boolean, table`

Validates a Lua table (decoded JSON-like structure) against the compiled schema with detailed reporting.

Returns two values:

- **boolean**: `true` if validation passed, `false` if it failed
- **table**: The complete validation output in the JSON Schema "basic" output format (as a Lua table)

Example usage:

```lua
local ok, report = schema:validate_detailed({ name = "Ada" })
if not ok then
  print("Validation failed!")
  -- report contains detailed error information as a Lua table
else
  print("Validation passed!")
end
```

#### `CompiledSchema:validate_json_detailed(instance_json_string) -> boolean, table`

Parses and validates the JSON instance string against the compiled schema with detailed reporting.

Returns two values:

- **boolean**: `true` if validation passed, `false` if it failed
- **table**: The complete validation output in the JSON Schema "basic" output format (as a Lua table)

#### `CompiledSchema:evaluate(instance_table) -> boolean`

Alias for `CompiledSchema:validate`. Provided for compatibility.

#### Module-level Functions

The following module-level functions are also available as functional forms:

- `luablaze.validate(compiled_schema, instance_table) -> boolean`
- `luablaze.validate_json(compiled_schema, instance_json_string) -> boolean`
- `luablaze.validate_detailed(compiled_schema, instance_table) -> boolean, table`
- `luablaze.validate_json_detailed(compiled_schema, instance_json_string) -> boolean, table`

## Usage

```lua
local luablaze = require("luablaze")

-- Check module version
print(luablaze._VERSION)        -- "1.0.0"
print(luablaze._NAME)           -- "luablaze"
print(luablaze._BLAZE_VERSION)  -- "0.0.1" (Sourcemeta Blaze library version)

local schema = luablaze.new([[
  {
    "type": "object",
    "properties": { "name": { "type": "string" } },
    "required": ["name"],
    "additionalProperties": false
  }
]])

-- Simple boolean validation
print(schema:validate({ name = "Ada" })) -- true
print(schema:validate({ name = 123 })) -- false
print(schema:validate_json([[{"name":"Ada"}]])) -- true
print(luablaze.validate(schema, { name = "Ada" })) -- true

-- Validation with detailed output
local ok, report = schema:validate_detailed({ name = 123 })
if not ok then
  print("Validation failed!")
  -- report is a Lua table with detailed error information
  print("Valid:", report.valid)
  print("Errors:", report.errors)
end
```

Notes:

- `luablaze.new` always takes a **schema JSON string** (parsed using `sourcemeta::core::parse_json`).
- `CompiledSchema:validate`/`validate_detailed` take **Lua tables** (converted to a JSON value internally).
- `CompiledSchema:validate_json`/`validate_json_detailed` take **instance JSON strings** (parsed using `sourcemeta::core::parse_json`).
- On parse/compile errors, the module raises a Lua error with the underlying C++ exception message.

## Building

### Requirements

- CMake >= 3.23
- A C++20 compiler
- Lua >= 5.3 (headers + library)
- Git (only needed if you want CMake to auto-init submodules)
- LuaRocks (optional, for `luarocks make`)

### Submodules

This repository uses git submodules for dependencies under `deps/`.

Currently tracked submodules:

- `deps/blaze` &mdash; required for every build.
- `deps/json-schema-test-suite` &mdash; only needed for running tests/dev validation.

If you cloned the repository without submodules, initialize them with:

```sh
git submodule update --init --recursive
```

You can also pull just what you need, e.g.:

```sh
git submodule update --init deps/blaze                  # build only
git submodule update --init deps/blaze deps/json-schema-test-suite  # build + tests
```

The top-level CMake has an option `LUABLAZE_INIT_SUBMODULES` (default ON) that
mirrors the commands above during configure. When `BUILD_TESTING` is enabled,
only then will it update the JSON test suite submodule.

### Build with LuaRocks

From a git checkout:

```sh
luarocks make
```

### Generate Documentation

The project uses [Doxygen](https://www.doxygen.nl/) to generate API documentation from source code comments.

#### Requirements

- Doxygen (install with `brew install doxygen` on macOS, `apt-get install doxygen` on Ubuntu)
- Graphviz (optional, for diagrams - `brew install graphviz` or `apt-get install graphviz`)

#### Generate Docs

```sh
# Option 1: Using CMake
mkdir -p build && cd build
cmake .. -DLUABLAZE_BUILD_DOCS=ON
make docs

# Option 2: Using Doxygen directly
doxygen Doxyfile
```

Documentation will be generated in the `docs/html/` directory. Open `docs/html/index.html` in your browser to view.

#### Automating Documentation Updates

To ensure documentation stays up-to-date with code changes, you can set up a pre-commit hook:

**1. Create `.git/hooks/pre-commit`:**

```bash
#!/bin/bash
# Pre-commit hook to generate documentation

echo "Generating documentation..."
if ! command -v doxygen &> /dev/null; then
    echo "Warning: Doxygen not installed. Skipping documentation generation."
    exit 0
fi

# Generate docs
doxygen Doxyfile > /dev/null 2>&1

if [ $? -eq 0 ]; then
    echo "✓ Documentation generated successfully"
else
    echo "✗ Documentation generation failed"
    exit 1
fi

# Optionally stage the generated docs (uncomment if you want to commit docs)
# git add docs/

exit 0
```

**2. Make it executable:**

```sh
chmod +x .git/hooks/pre-commit
```

**What this does:**
- Runs automatically before each commit
- Generates Doxygen documentation from source comments
- Fails the commit if documentation generation fails
- Optionally stages generated docs (uncomment `git add docs/` line)

**Note:** The `docs/` directory is git-ignored by default. If you want to commit generated documentation, remove `docs/` from [.gitignore](.gitignore) and uncomment the `git add docs/` line in the hook.

#### Clean Docs

```sh
rm -rf docs/
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

### Testing

Tests are driven by CTest and rely on the JSON Schema Test Suite data.

1. Ensure submodules are ready for testing:
  ```sh
  git submodule update --init deps/blaze deps/json-schema-test-suite
  ```
2. Configure with tests enabled:
  ```sh
  cmake -S . -B build -DBUILD_TESTING=ON
  ```
  - `LUABLAZE_REQUIRE_TEST_SUITE` defaults to `BUILD_TESTING`, ensuring the
    configure step fails fast if the test suite data is missing.
3. Build and run the tests:
  ```sh
  cmake --build build
  ctest --test-dir build
  ```

For CI or production builds where tests are unnecessary, configure with
`-DBUILD_TESTING=OFF` (the default) and skip pulling the JSON test suite.

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
