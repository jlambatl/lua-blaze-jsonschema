# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Initial Lua C module `luablaze` providing JSON Schema compilation and validation
- `luablaze.new(schema_json[, dialect_or_options[, options]])` for compiling schemas
- `CompiledSchema:validate(instance_json)` for boolean validation
- `CompiledSchema:validate_output(instance_json)` for JSON Schema "basic" output format
- `CompiledSchema:evaluate(instance_json)` as alias for validate
- `luablaze.validate(compiled_schema, instance_json)` functional form
- Support for multiple JSON Schema dialects (draft-04 through draft2020-12)
- Support for "Fast" and "Exhaustive" validation modes
- LuaRocks rockspec for easy installation
- Comprehensive test suite using JSON-Schema-Test-Suite
- Performance benchmarks comparing luablaze with lua-schema
- CI workflows for Linux, macOS, and Windows

### Dependencies
- Sourcemeta Blaze JSON Schema compiler/evaluator (AGPL or commercial license)
- Lua >= 5.2
- CMake >= 3.23
- C++20 compiler
