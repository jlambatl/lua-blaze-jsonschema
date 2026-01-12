-- Tests for the configurable max_recursion_depth option
local luablaze = require("luablaze")

describe("Configurable max_recursion_depth", function()
    local simple_schema = [[{"$schema":"http://json-schema.org/draft-07/schema#","type": "object"}]]

    -- Helper to create a deeply nested table
    local function create_nested_table(depth)
        local t = { value = "leaf" }
        for _ = 1, depth - 1 do
            t = { nested = t }
        end
        return t
    end

    describe("default limit (100)", function()
        it("accepts tables within default limit", function()
            local schema = luablaze.new(simple_schema)
            local t = create_nested_table(50)
            assert.is_true(schema:validate(t))
        end)

        it("rejects tables exceeding default limit", function()
            local schema = luablaze.new(simple_schema)
            local t = create_nested_table(101)
            local ok, err = pcall(function() schema:validate(t) end)
            assert.is_false(ok)
            assert.is_truthy(err:match("Maximum recursion depth exceeded"))
        end)
    end)

    describe("custom lower limit", function()
        it("accepts tables within custom limit", function()
            local schema = luablaze.new(simple_schema, { max_recursion_depth = 50 })
            local t = create_nested_table(40)
            assert.is_true(schema:validate(t))
        end)

        it("rejects tables exceeding custom limit", function()
            local schema = luablaze.new(simple_schema, { max_recursion_depth = 50 })
            local t = create_nested_table(60)
            local ok, err = pcall(function() schema:validate(t) end)
            assert.is_false(ok)
            assert.is_truthy(err:match("Maximum recursion depth exceeded"))
        end)
    end)

    describe("unlimited (max_recursion_depth = 0)", function()
        it("accepts deeply nested tables when unlimited", function()
            -- Note: we can't actually test infinitely deep, but we can test
            -- deeper than the default limit (100) without crashing
            local schema = luablaze.new(simple_schema, { max_recursion_depth = 0 })
            -- Create a moderately deep table that would exceed normal limits
            -- but not crash Lua itself (keep under ~200 to be safe)
            local t = create_nested_table(150)
            assert.is_true(schema:validate(t))
        end)
    end)

    describe("option validation", function()
        it("accepts integer max_recursion_depth", function()
            assert.has_no.errors(function()
                luablaze.new(simple_schema, { max_recursion_depth = 100 })
            end)
        end)

        it("rejects negative max_recursion_depth", function()
            assert.has_error(function()
                luablaze.new(simple_schema, { max_recursion_depth = -1 })
            end)
        end)

        it("rejects non-integer max_recursion_depth", function()
            assert.has_error(function()
                luablaze.new(simple_schema, { max_recursion_depth = "100" })
            end)
        end)
    end)

    describe("combined with other limits", function()
        it("respects both max_recursion_depth and max_array_length", function()
            local array_schema = [[{"$schema":"http://json-schema.org/draft-07/schema#","type": "array"}]]
            local schema = luablaze.new(array_schema, {
                max_recursion_depth = 100,
                max_array_length = 10
            })

            -- Create small nested arrays
            local t = { { { 1, 2, 3 } } }
            assert.is_true(schema:validate(t))
        end)

        it("info() reflects the custom max_recursion_depth", function()
            local schema = luablaze.new(simple_schema, { max_recursion_depth = 250 })
            local info = schema:info()
            assert.equals(250, info.max_recursion_depth)
        end)
    end)
end)
