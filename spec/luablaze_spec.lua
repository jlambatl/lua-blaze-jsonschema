require("spec.spec_helper")

local luablaze = require("luablaze")

describe("Module metadata", function()
  it("has _VERSION constant", function()
    assert.is_not_nil(luablaze._VERSION)
    assert.is_string(luablaze._VERSION)
    assert.is_true(#luablaze._VERSION > 0)
  end)

  it("has _NAME constant", function()
    assert.is_not_nil(luablaze._NAME)
    assert.are.equal("luablaze", luablaze._NAME)
  end)

  it("has _BLAZE_VERSION constant", function()
    assert.is_not_nil(luablaze._BLAZE_VERSION)
    assert.is_string(luablaze._BLAZE_VERSION)
    assert.is_true(#luablaze._BLAZE_VERSION > 0)
  end)
end)

local json
do
  local ok, mod = pcall(require, "dkjson")
  if ok then
    json = {
      null = mod.null,
      decode = function(str, pos, nullval)
        local obj, _, err = mod.decode(str, pos, nullval)
        if err then
          error(err)
        end
        return obj
      end,
      encode = function(obj)
        return mod.encode(obj)
      end,
    }
  else
    ok, mod = pcall(require, "cjson")
    if ok then
      if type(mod.encode_number_precision) == "function" then
        mod.encode_number_precision(17)
      end

      json = {
        null = mod.null,
        decode = function(str)
          return mod.decode(str)
        end,
        encode = function(obj)
          return mod.encode(obj)
        end,
      }
    else
      error("JSON library not found. Install lua-cjson or dkjson to run the test suite.")
    end
  end
end

local function dirname(path)
  return (path:gsub("[/\\][^/\\]+$", ""))
end

local function this_file_dir()
  local source = debug.getinfo(1, "S").source
  if source:sub(1, 1) == "@" then
    source = source:sub(2)
  end
  return dirname(source)
end

local function read_all(path)
  local f = assert(io.open(path, "rb"))
  local data = f:read("*a")
  f:close()
  return data
end

local function encode_number_plain(n)
  if n == math.huge then
    return "1e9999"
  elseif n == -math.huge then
    return "-1e9999"
  elseif n ~= n then
    return "null"
  end

  if math.type and math.type(n) == "integer" then
    return tostring(n)
  end

  if n == math.floor(n) and math.abs(n) >= 1e15 then
    return string.format("%.0f", n)
  end

  return string.format("%.15g", n)
end

local function dir_exists(path)
  local is_windows = package.config:sub(1, 1) == "\\"
  local cmd
  if is_windows then
    cmd = string.format('if exist "%s\\." (echo 1) else (echo 0)', path)
  else
    cmd = string.format('test -d "%s" && echo 1 || echo 0', path)
  end

  local p = assert(io.popen(cmd, "r"))
  local out = p:read("*l")
  p:close()
  return out == "1"
end

local function list_json_files(dir)
  local is_windows = package.config:sub(1, 1) == "\\"
  local cmd
  if is_windows then
    cmd = string.format('dir /b "%s\\*.json"', dir)
  else
    cmd = string.format('find "%s" -maxdepth 1 -type f -name "*.json" -print', dir)
  end

  local p = assert(io.popen(cmd, "r"))
  local files = {}
  for line in p:lines() do
    if is_windows then
      table.insert(files, dir .. "\\" .. line)
    else
      table.insert(files, line)
    end
  end
  p:close()
  table.sort(files)
  return files
end

local function list_dialect_dirs(tests_dir)
  local is_windows = package.config:sub(1, 1) == "\\"
  local cmd
  if is_windows then
    cmd = string.format('dir /b /ad "%s"', tests_dir)
  else
    cmd = string.format('find "%s" -maxdepth 1 -mindepth 1 -type d -print', tests_dir)
  end

  local p = assert(io.popen(cmd, "r"))
  local dirs = {}
  for line in p:lines() do
    if is_windows then
      table.insert(dirs, line)
    else
      local name = (line:gsub("^.*/", ""))
      table.insert(dirs, name)
    end
  end
  p:close()
  table.sort(dirs)
  return dirs
end

describe("JSON-Schema-Test-Suite", function()
  it("runs the JSON-Schema-Test-Suite (excluding optional/remotes)", function()
    local root = os.getenv("GITHUB_WORKSPACE")
    if not root or root == "" then
      local p = assert(io.popen("pwd", "r"))
      root = p:read("*l")
      p:close()
    end
    local suite_root = root .. "/deps/json-schema-test-suite"
    local tests_dir = suite_root .. "/tests"

    assert.is_true(
      dir_exists(tests_dir),
      "JSON-Schema-Test-Suite submodule is missing. Ensure " ..
      tests_dir .. " exists (e.g. run: git submodule update --init --recursive)"
    )

    local dialects = list_dialect_dirs(tests_dir)
    assert.is_true(#dialects > 0)

    for _, dialect in ipairs(dialects) do
      if dialect == "v1" then
        goto continue_dialect
      end
      local dialect_dir = tests_dir .. "/" .. dialect
      if dir_exists(dialect_dir) then
        local files = list_json_files(dialect_dir)
        for _, file in ipairs(files) do
          local lower = file:lower()
          if not lower:find("optional", 1, true) and not lower:find("remotes", 1, true) then
            local content = read_all(file)
            local groups = json.decode(content, 1, json.null)

            for _, group in ipairs(groups) do
              local schema_str = json.encode(group.schema)
              local ok, compiled_or_err = pcall(luablaze.new, schema_str, { dialect = dialect, mode = "Exhaustive" })
              if not ok then
                local message = tostring(compiled_or_err)
                if message:find("Could not resolve the reference to an external schema", 1, true) then
                  goto continue_group
                end
                if message:find("Could not resolve the metaschema of the schema", 1, true) then
                  goto continue_group
                end
                if message:find("Cannot compile unsupported vocabulary", 1, true) then
                  goto continue_group
                end
                if message:find("Could not determine how to perform bundling in this dialect", 1, true) then
                  goto continue_group
                end
                error(message)
              end
              local compiled = compiled_or_err

              for _, test in ipairs(group.tests) do
                local instance_str
                if type(test.data) == "number" then
                  instance_str = encode_number_plain(test.data)
                else
                  instance_str = json.encode(test.data)
                end
                local actual = compiled:validate_json(instance_str)
                if actual ~= test.valid then
                  print("Test Data: ", instance_str)
                  local ok, output_table = compiled:validate_json_detailed(instance_str)
                  local output_str = json.encode(output_table)
                  assert.are.equal(test.valid, actual, string.format(
                    "Mismatch in %s [%s]: %s / %s\nOutput: %s",
                    file, dialect, tostring(group.description), tostring(test.description), tostring(output_str)
                  ))
                end
              end

              ::continue_group::
            end
          end
        end
      end

      ::continue_dialect::
    end
  end)
end)

describe("Testing Malicious Lua data structures", function()
  local function deep_array_json(depth)
    return string.rep("[", depth) .. "0" .. string.rep("]", depth)
  end

  local function deep_object_json(depth)
    local s = "{}"
    for _ = 1, depth do
      s = "{\"a\":" .. s .. "}"
    end
    return s
  end

  it("rejects empty schema string", function()
    local ok, err = pcall(luablaze.new, "")
    assert.is_false(ok)
    assert.is_true(tostring(err):find("schema cannot be empty", 1, true) ~= nil)
  end)

  it("enforces max_depth when parsing schemas in luablaze.new", function()
    local schema = deep_object_json(5)
    local ok, err = pcall(luablaze.new, schema, { max_depth = 2 })
    assert.is_false(ok)
    assert.is_true(tostring(err):find("JSON maximum nesting depth exceeded", 1, true) ~= nil)
  end)

  it("enforces max_depth in validate_json", function()
    local schema = "{\"$schema\":\"http://json-schema.org/draft-07/schema#\",\"type\":\"array\"}"
    local compiled = luablaze.new(schema, { max_depth = 2 })
    local instance = deep_array_json(5)
    local ok, err = pcall(function()
      return compiled:validate_json(instance)
    end)
    assert.is_false(ok)
    assert.is_true(tostring(err):find("JSON maximum nesting depth exceeded", 1, true) ~= nil)
  end)

  it("enforces max_depth in validate_json_detailed", function()
    local schema = "{\"$schema\":\"http://json-schema.org/draft-07/schema#\",\"type\":\"array\"}"
    local compiled = luablaze.new(schema, { max_depth = 2 })
    local instance = deep_array_json(5)
    local ok, err = pcall(function()
      return compiled:validate_json_detailed(instance)
    end)
    assert.is_false(ok)
    assert.is_true(tostring(err):find("JSON maximum nesting depth exceeded", 1, true) ~= nil)
  end)

  it("rejects userdata values in table conversion", function()
    local schema = "{\"$schema\":\"http://json-schema.org/draft-07/schema#\",\"type\":\"object\"}"
    local compiled = luablaze.new(schema)
    local ok, err = pcall(function()
      return compiled:validate({ x = io.stdout })
    end)
    assert.is_false(ok)
    assert.is_true(tostring(err):find("Unsupported Lua type for JSON conversion", 1, true) ~= nil)
  end)

  it("rejects json.null sentinel values when they are userdata/lightuserdata", function()
    if type(json.null) ~= "userdata" and type(json.null) ~= "lightuserdata" then
      return
    end

    local schema = "{\"$schema\":\"http://json-schema.org/draft-07/schema#\",\"type\":\"object\"}"
    local compiled = luablaze.new(schema)
    local ok, err = pcall(function()
      return compiled:validate({ x = json.null })
    end)
    assert.is_false(ok)
    assert.is_true(tostring(err):find("Unsupported Lua type for JSON conversion", 1, true) ~= nil)
  end)

  it("enforces max_array_length on sparse arrays", function()
    local schema = "{\"$schema\":\"http://json-schema.org/draft-07/schema#\",\"type\":\"array\"}"
    local compiled = luablaze.new(schema, { max_array_length = 2 })
    local t = {}
    t[3] = 1
    local ok, err = pcall(function()
      return compiled:validate(t)
    end)
    assert.is_false(ok)
    assert.is_true(tostring(err):find("Array length exceeds max_array_length", 1, true) ~= nil)
  end)
end)

describe("Required fields", function()
  it("fails when required fields are missing", function()
    local schema =
    "{\"$schema\":\"http://json-schema.org/draft-07/schema#\",\"type\":\"object\",\"properties\":{\"s\":{\"type\":\"string\",\"minLength\":1},\"n\":{\"type\":\"number\"}},\"required\":[\"s\",\"n\"]}"
    local compiled = luablaze.new(schema)

    assert.is_false(compiled:validate_json("{}"))
    assert.is_false(compiled:validate_json("{\"s\":\"ok\"}"))
    assert.is_false(compiled:validate_json("{\"n\":1}"))
  end)

  it("fails when required string fields are null or empty", function()
    local schema =
    "{\"$schema\":\"http://json-schema.org/draft-07/schema#\",\"type\":\"object\",\"properties\":{\"s\":{\"type\":\"string\",\"minLength\":1},\"n\":{\"type\":\"number\"}},\"required\":[\"s\",\"n\"]}"
    local compiled = luablaze.new(schema)

    assert.is_false(compiled:validate_json("{\"s\":null,\"n\":1}"))
    assert.is_false(compiled:validate_json("{\"s\":\"\",\"n\":1}"))
    assert.is_true(compiled:validate_json("{\"s\":\"ok\",\"n\":1}"))
  end)

  it("fails when required number fields are null or wrong type", function()
    local schema =
    "{\"$schema\":\"http://json-schema.org/draft-07/schema#\",\"type\":\"object\",\"properties\":{\"s\":{\"type\":\"string\",\"minLength\":1},\"n\":{\"type\":\"number\"}},\"required\":[\"s\",\"n\"]}"
    local compiled = luablaze.new(schema)

    assert.is_false(compiled:validate_json("{\"s\":\"ok\",\"n\":null}"))
    assert.is_false(compiled:validate_json("{\"s\":\"ok\",\"n\":\"\"}"))
    assert.is_false(compiled:validate_json("{\"s\":\"ok\",\"n\":\"1\"}"))
    assert.is_true(compiled:validate_json("{\"s\":\"ok\",\"n\":0}"))
  end)
end)
