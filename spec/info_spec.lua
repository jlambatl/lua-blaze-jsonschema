-- Tests for the compiled_schema:info() method
local luablaze = require("luablaze")

describe("CompiledSchema:info()", function()
    local simple_schema = [[{"$schema":"http://json-schema.org/draft-07/schema#","type": "string"}]]

    describe("default options", function()
        local schema

        before_each(function()
            schema = luablaze.new(simple_schema)
        end)

        it("returns a table", function()
            local info = schema:info()
            assert.is_table(info)
        end)

        it("contains mode field with default value", function()
            local info = schema:info()
            assert.equals("Fast", info.mode)
        end)

        it("contains dialect field", function()
            local info = schema:info()
            assert.equals("auto", info.dialect)
        end)

        it("contains max_array_length with default value", function()
            local info = schema:info()
            assert.equals(100000, info.max_array_length)
        end)

        it("contains max_depth with default value", function()
            local info = schema:info()
            assert.equals(128, info.max_depth)
        end)

        it("contains max_recursion_depth with default value", function()
            local info = schema:info()
            assert.equals(100, info.max_recursion_depth)
        end)

        it("contains luablaze_version", function()
            local info = schema:info()
            assert.is_string(info.luablaze_version)
            assert.equals(luablaze._VERSION, info.luablaze_version)
        end)

        it("contains blaze_version", function()
            local info = schema:info()
            assert.is_string(info.blaze_version)
            assert.equals(luablaze._BLAZE_VERSION, info.blaze_version)
        end)
    end)

    describe("custom options", function()
        it("reflects custom mode", function()
            local schema = luablaze.new(simple_schema, { mode = "Exhaustive" })
            local info = schema:info()
            assert.equals("Exhaustive", info.mode)
        end)

        it("reflects custom max_array_length", function()
            local schema = luablaze.new(simple_schema, { max_array_length = 5000 })
            local info = schema:info()
            assert.equals(5000, info.max_array_length)
        end)

        it("reflects custom max_depth", function()
            local schema = luablaze.new(simple_schema, { max_depth = 50 })
            local info = schema:info()
            assert.equals(50, info.max_depth)
        end)

        it("reflects custom max_recursion_depth", function()
            local schema = luablaze.new(simple_schema, { max_recursion_depth = 500 })
            local info = schema:info()
            assert.equals(500, info.max_recursion_depth)
        end)

        it("reflects custom dialect", function()
            local schema = luablaze.new(simple_schema, { dialect = "draft7" })
            local info = schema:info()
            -- The dialect is stored as the resolved URI
            assert.is_string(info.dialect)
            assert.truthy(info.dialect:match("draft%-07"))
        end)

        it("reflects all custom options together", function()
            local schema = luablaze.new(simple_schema, {
                mode = "Exhaustive",
                max_array_length = 1000,
                max_depth = 64,
                max_recursion_depth = 200
            })
            local info = schema:info()
            assert.equals("Exhaustive", info.mode)
            assert.equals(1000, info.max_array_length)
            assert.equals(64, info.max_depth)
            assert.equals(200, info.max_recursion_depth)
        end)
    end)

    describe("edge cases", function()
        it("handles zero max_recursion_depth (unlimited)", function()
            local schema = luablaze.new(simple_schema, { max_recursion_depth = 0 })
            local info = schema:info()
            assert.equals(0, info.max_recursion_depth)
        end)

        it("handles zero max_array_length (unlimited)", function()
            local schema = luablaze.new(simple_schema, { max_array_length = 0 })
            local info = schema:info()
            assert.equals(0, info.max_array_length)
        end)

        it("handles zero max_depth (unlimited)", function()
            local schema = luablaze.new(simple_schema, { max_depth = 0 })
            local info = schema:info()
            assert.equals(0, info.max_depth)
        end)
    end)
end)
