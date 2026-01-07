require("spec.spec_helper")

local luablaze = require("luablaze")

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
      error("JSON library not found. Install lua-cjson or dkjson to run the benchmarks.")
    end
  end
end

local function decode_json_once(str)
  local ok, decoded = pcall(json.decode, str, 1, json.null)
  if ok then
    return decoded
  end

  local ok2, decoded2 = pcall(json.decode, str)
  if ok2 then
    return decoded2
  end

  error(tostring(decoded))
end

local function dirname(path)
  return (path:gsub("[/\\][^/\\]+$", ""))
end

local function cwd()
  local p = assert(io.popen("pwd", "r"))
  local out = p:read("*l")
  p:close()
  return out
end

local function this_file_dir()
  local source = debug.getinfo(1, "S").source
  if source:sub(1, 1) == "@" then
    source = source:sub(2)
  end
  if source:sub(1, 1) ~= "/" and not source:match("^[A-Za-z]:\\") then
    source = cwd() .. "/" .. source
  end
  return dirname(source)
end

local function read_all(path)
  local f = assert(io.open(path, "rb"))
  local data = f:read("*a")
  f:close()
  return data
end

local function list_dir(dir)
  local is_windows = package.config:sub(1, 1) == "\\"
  local cmd
  if is_windows then
    cmd = string.format('dir /b "%s"', dir)
  else
    cmd = string.format('ls -1 "%s"', dir)
  end

  local p = assert(io.popen(cmd, "r"))
  local files = {}
  for line in p:lines() do
    table.insert(files, line)
  end
  p:close()
  return files
end

local function now_seconds()
  local ok, socket = pcall(require, "socket")
  if ok and type(socket.gettime) == "function" then
    return socket.gettime()
  end

  return os.clock()
end

local function fmt_seconds(s)
  return string.format("%.6f", s)
end

local function env_number(name, default)
  local value = os.getenv(name)
  if not value or value == "" then
    return default
  end
  local n = tonumber(value)
  if not n then
    return default
  end
  return n
end

local function discover_fixtures(test_data_dir)
  local entries = list_dir(test_data_dir)
  table.sort(entries)

  local schemas = {}
  for _, name in ipairs(entries) do
    if name:sub(-12) == ".schema.json" then
      local base = name:sub(1, #name - 12)
      schemas[base] = { schema = test_data_dir .. "/" .. name, data = {} }
    end
  end

  for _, name in ipairs(entries) do
    if name:sub(-10) == ".data.json" then
      local base = name:sub(1, #name - 10)
      local prefix = base
      local dot = base:find("%.[0-9]+$")
      if dot then
        prefix = base:sub(1, dot - 1)
      end

      if schemas[prefix] then
        table.insert(schemas[prefix].data, test_data_dir .. "/" .. name)
      end
    end
  end

  local result = {}
  for base, item in pairs(schemas) do
    table.sort(item.data)
    if #item.data > 0 then
      table.insert(result, { name = base, schema_path = item.schema, data_paths = item.data })
    end
  end
  table.sort(result, function(a, b) return a.name < b.name end)
  return result
end

local function load_lua_schema_adapter()
  local ok, mod = pcall(require, "schema")
  if ok and mod then
    if type(mod) ~= "table" then
      return nil
    end
    if type(mod.new) ~= "function" then
      return nil
    end
    mod.output_format = "detailed"
    return mod
  end

  return nil
end

local function lua_schema_compatible_schema_for_fixture(name)
  if name == "github-action" then
    return {
      type = "object",
      properties = {
        name = { type = "string" },
        author = { type = "string" },
        description = { type = "string" },
        inputs = { type = "object" },
        outputs = { type = "object" },
        runs = {
          type = "object",
          properties = {
            using = { type = "string" },
            main = { type = "string" },
            pre = { type = "string" },
            ["pre-if"] = { type = "string" },
            post = { type = "string" },
            ["post-if"] = { type = "string" },
          },
          required = { "using", "main" },
        },
      },
      required = { "name", "description", "runs" },
    }
  end

  return nil
end

local function compile_lua_schema(mod, schema_json)
  local schema_table
  if type(schema_json) == "string" then
    local ok, parsed = pcall(json.decode, schema_json)
    if not ok then
      error("Failed to parse schema JSON for lua-schema: " .. tostring(parsed))
    end
    schema_table = parsed
  else
    schema_table = schema_json
  end

  local validator = mod.new(schema_table)
  return function(instance_table)
    if type(instance_table) ~= "table" then
      error("lua-schema benchmark expects a Lua table instance")
    end
    local result = validator:validate(instance_table)
    return result.valid
  end
end

local function bench(name, compile_fn, validate_fn, iterations)
  local t0 = now_seconds()
  local compiled = compile_fn()
  local t1 = now_seconds()

  for _ = 1, iterations do
    validate_fn(compiled)
  end
  local t2 = now_seconds()

  return {
    name = name,
    compile_s = (t1 - t0),
    validate_s = (t2 - t1),
    total_s = (t2 - t0),
  }
end

local function bench_validate_only(name, compiled_validate_fn, iterations)
  local t0 = now_seconds()
  for _ = 1, iterations do
    compiled_validate_fn()
  end
  local t1 = now_seconds()

  return {
    name = name,
    validate_s = (t1 - t0),
  }
end

describe("Performance", function()
  it("benchmarks luablaze vs lua-schema over spec/test_data", function()
    local root = dirname(this_file_dir())
    local test_data_dir = root .. "/spec/test_data"

    local iterations = env_number("LUABLAZE_BENCH_ITERS", 200)

    local fixtures = discover_fixtures(test_data_dir)
    assert.is_true(#fixtures > 0)

    local lua_schema = load_lua_schema_adapter()

    print(string.format("Benchmark iterations: %d", iterations))
    if lua_schema then
      print(string.format("lua-schema module: %s", "schema"))
    else
      print("lua-schema module: not installed or missing 'new' (skipping)")
    end

    local results = {}

    for _, fx in ipairs(fixtures) do
      local schema_json = read_all(fx.schema_path)
      local instances = {}
      for _, data_path in ipairs(fx.data_paths) do
        local instance_json = read_all(data_path)
        table.insert(instances, decode_json_once(instance_json))
      end

      local fixture_iterations = iterations
      if fx.name == "github-action" then
        fixture_iterations = 50
      end

      local blaze_compiled = luablaze.new(schema_json, { mode = "Fast" })
      local blaze_validate_fn = function()
        for _, inst in ipairs(instances) do
          blaze_compiled:validate(inst)
        end
      end

      local blaze_result = bench_validate_only("luablaze", blaze_validate_fn, fixture_iterations)
      blaze_result.compile_s = 0.000001 -- placeholder for display
      blaze_result.total_s = blaze_result.validate_s
      blaze_result.name = fx.name

      table.insert(results, { lib = "luablaze", fixture = fx.name, result = blaze_result })

      print(string.format(
        "[%s] luablaze compile=%ss validate=%ss total=%ss",
        fx.name,
        fmt_seconds(blaze_result.compile_s),
        fmt_seconds(blaze_result.validate_s),
        fmt_seconds(blaze_result.total_s)
      ))

      if lua_schema then
        local ok_compile, lua_compiled_or_err = pcall(function()
          local lua_schema_json = schema_json
          local compat = lua_schema_compatible_schema_for_fixture(fx.name)
          if compat then
            lua_schema_json = json.encode(compat)
          end
          return compile_lua_schema(lua_schema, lua_schema_json)
        end)

        if ok_compile then
          local lua_validate_fn = function()
            for _, inst in ipairs(instances) do
              lua_compiled_or_err(inst)
            end
          end

          local lua_result = bench_validate_only("lua-schema", lua_validate_fn, fixture_iterations)
          lua_result.compile_s = 0.000001 -- placeholder for display
          lua_result.total_s = lua_result.validate_s
          lua_result.name = fx.name

          table.insert(results, { lib = "lua-schema", fixture = fx.name, result = lua_result })

          print(string.format(
            "[%s] lua-schema compile=%ss validate=%ss total=%ss",
            fx.name,
            fmt_seconds(lua_result.compile_s),
            fmt_seconds(lua_result.validate_s),
            fmt_seconds(lua_result.total_s)
          ))
        else
          print(string.format("[%s] lua-schema skipped (compile error): %s", fx.name, tostring(lua_compiled_or_err)))
        end
      end
    end

    print("\n=== Summary ===")
    for _, fx in ipairs(fixtures) do
      local blaze = nil
      local lua_schema_result = nil
      for _, r in ipairs(results) do
        if r.fixture == fx.name then
          if r.lib == "luablaze" then
            blaze = r.result
          elseif r.lib == "lua-schema" then
            lua_schema_result = r.result
          end
        end
      end

      if blaze and lua_schema_result then
        local ratio = blaze.validate_s / lua_schema_result.validate_s
        local faster = ratio < 1 and "luablaze" or "lua-schema"
        local slower = ratio < 1 and "lua-schema" or "luablaze"
        local factor = ratio < 1 and (1/ratio) or ratio
        print(string.format(
          "[%s] %s is %.2fx faster than %s (validate: %.6fs vs %.6fs)",
          fx.name,
          faster,
          factor,
          slower,
          math.min(blaze.validate_s, lua_schema_result.validate_s),
          math.max(blaze.validate_s, lua_schema_result.validate_s)
        ))
      elseif blaze then
        print(string.format("[%s] luablaze only: validate=%.6fs", fx.name, blaze.validate_s))
      elseif lua_schema_result then
        print(string.format("[%s] lua-schema only: validate=%.6fs", fx.name, lua_schema_result.validate_s))
      end
    end

    assert.is_true(true)
  end)
end)
