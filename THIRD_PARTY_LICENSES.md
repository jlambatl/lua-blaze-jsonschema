# Third-Party Licenses

This repository includes (or references) third-party software.

## sourcemeta/blaze

- Project: https://github.com/sourcemeta/blaze
- License: GNU Affero General Public License (AGPL)
- License text: https://github.com/sourcemeta/blaze/blob/master/LICENSE

This project builds a Lua C module that links against Blaze. If you distribute
binaries produced from this repository, Blaze's AGPL license may impose
additional requirements (including providing corresponding source and preserving
license notices).

Alternatively, you can use this software under a commercial license, as set out
in <https://www.sourcemeta.com/licensing/>.

## JSON-Schema-Test-Suite

- Project: https://github.com/json-schema-org/JSON-Schema-Test-Suite
- License: MIT
- License text: https://github.com/json-schema-org/JSON-Schema-Test-Suite/blob/main/LICENSE

This project may use the JSON-Schema-Test-Suite as a git submodule at
`deps/json-schema-test-suite` for running conformance tests. It will not be
present unless you initialize git submodules.

## spec/test_data Samples

The sample JSON Schema and data files in `spec/test_data/` are derived from
examples published at https://json-schema.org/ and are used for benchmarking
and testing purposes.

- Source: https://json-schema.org/
- License: The JSON Schema specification and examples are published under
  permissive terms. See https://json-schema.org/ for details.

## Notes about git submodules

Some dependencies are tracked as git submodules under `deps/` and are not
present unless initialized.
