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
 
 local root = dirname(this_file_dir())
 
 local function add_cpath(path)
   if not path or path == "" then
     return
   end
   package.cpath = path .. "/?.so;" .. package.cpath
 end
 
 add_cpath(root .. "/build")
 add_cpath(root .. "/build/Debug")
 add_cpath(root .. "/build/Release")
 add_cpath(root .. "/lua_install")
 add_cpath(root .. "/lua_install/lib")


