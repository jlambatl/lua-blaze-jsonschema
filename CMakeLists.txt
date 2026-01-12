cmake_minimum_required(VERSION 3.10)

project(luablaze)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)

option(LUABLAZE_INIT_SUBMODULES "Initialize git submodules during CMake configure" ON)
option(LUABLAZE_REQUIRE_TEST_SUITE "Require the JSON-Schema-Test-Suite submodule to be present" ${BUILD_TESTING})
option(LUABLAZE_BUILD_DOCS "Build documentation using Doxygen" OFF)

if(LUABLAZE_INIT_SUBMODULES)
  find_package(Git QUIET)
  if(GIT_FOUND)
    set(LUABLAZE_REQUIRED_SUBMODULES deps/blaze)
    if(BUILD_TESTING)
      list(APPEND LUABLAZE_REQUIRED_SUBMODULES deps/json-schema-test-suite)
    endif()
    foreach(LUABLAZE_SUBMODULE ${LUABLAZE_REQUIRED_SUBMODULES})
      execute_process(
        COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive --depth 1 ${LUABLAZE_SUBMODULE}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        RESULT_VARIABLE LUABLAZE_GIT_SUBMOD_RESULT
      )
      if(NOT LUABLAZE_GIT_SUBMOD_RESULT EQUAL 0)
        message(FATAL_ERROR "git submodule update --init --recursive ${LUABLAZE_SUBMODULE} failed with exit code ${LUABLAZE_GIT_SUBMOD_RESULT}")
      endif()
    endforeach()
  endif()
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/blaze/CMakeLists.txt")
  message(FATAL_ERROR "Blaze submodule not present at deps/blaze. Run: git submodule update --init --recursive")
endif()

if(BUILD_TESTING AND LUABLAZE_REQUIRE_TEST_SUITE AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/json-schema-test-suite/tests/draft7")
  message(FATAL_ERROR "JSON-Schema-Test-Suite submodule not present at deps/json-schema-test-suite. Run: git submodule update --init --recursive deps/json-schema-test-suite")
endif()

# Prevent Blaze from installing - we only want to link against it
set(BLAZE_INSTALL OFF CACHE BOOL "Disable Blaze installation" FORCE)
set(INSTALL_BLAZE OFF CACHE BOOL "Disable Blaze installation" FORCE)
set(CMAKE_SKIP_INSTALL_ALL_DEPENDENCY TRUE)

add_subdirectory(deps/blaze EXCLUDE_FROM_ALL)

# Extract Blaze version from its CMakeLists.txt
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/deps/blaze/CMakeLists.txt" BLAZE_CMAKE_CONTENT)
string(REGEX MATCH "project\\([^)]*VERSION[ \t]+([0-9]+\\.[0-9]+\\.[0-9]+)" _ "${BLAZE_CMAKE_CONTENT}")
if(CMAKE_MATCH_1)
  set(BLAZE_VERSION "${CMAKE_MATCH_1}")
  message(STATUS "Detected Blaze version: ${BLAZE_VERSION}")
else()
  set(BLAZE_VERSION "unknown")
  message(WARNING "Could not detect Blaze version from CMakeLists.txt")
endif()

# Disable installation from the deps directory
set_directory_properties(PROPERTIES EXCLUDE_FROM_ALL TRUE)

# LuaRocks compatibility - prioritize LuaRocks variables
message(STATUS "[DEBUG] LUA_INCDIR from LuaRocks: '${LUA_INCDIR}'")
message(STATUS "[DEBUG] LUA_LIBDIR from LuaRocks: '${LUA_LIBDIR}'")
message(STATUS "[DEBUG] LUA_LIBNAME from LuaRocks: '${LUA_LIBNAME}'")

if(DEFINED LUA_INCDIR)
  set(LUA_INCLUDE_DIR ${LUA_INCDIR})
  message(STATUS "[DEBUG] Set LUA_INCLUDE_DIR to: '${LUA_INCLUDE_DIR}'")
endif()

# Handle library detection
if(DEFINED LUA_LIBDIR AND DEFINED LUA_LIBNAME)
  find_library(LUA_LIBRARY 
    NAMES ${LUA_LIBNAME} lua
    PATHS ${LUA_LIBDIR}
    NO_DEFAULT_PATH
  )
  set(LUA_LIBRARIES ${LUA_LIBRARY})
  message(STATUS "[DEBUG] Found LUA_LIBRARY via LUA_LIBDIR: '${LUA_LIBRARY}'")
elseif(DEFINED LUA_LIBDIR)
  # LuaRocks provided LIBDIR but not LIBNAME, search for common Lua library names
  find_library(LUA_LIBRARY 
    NAMES lua5.4 lua54 lua5.3 lua53 lua5.2 lua52 lua5.1 lua51 lua
    PATHS ${LUA_LIBDIR}
    NO_DEFAULT_PATH
  )
  if(LUA_LIBRARY)
    set(LUA_LIBRARIES ${LUA_LIBRARY})
    message(STATUS "[DEBUG] Found LUA_LIBRARY in LUA_LIBDIR: '${LUA_LIBRARY}'")
  endif()
elseif(DEFINED LUA_LIBRARY)
  set(LUA_LIBRARIES ${LUA_LIBRARY})
  message(STATUS "[DEBUG] Using provided LUA_LIBRARY: '${LUA_LIBRARY}'")
endif()

# Only use find_package if we're not being called by LuaRocks
if(NOT DEFINED LUA_INCDIR AND (NOT DEFINED LUA_INCLUDE_DIR OR LUA_INCLUDE_DIR STREQUAL ""))
  message(STATUS "[DEBUG] Running find_package(Lua) as fallback")
  find_package(Lua 5.3 REQUIRED)
  if(DEFINED LUA_VERSION_STRING AND LUA_VERSION_STRING VERSION_LESS "5.3")
    message(FATAL_ERROR "Lua >= 5.3 is required, but found ${LUA_VERSION_STRING}")
  endif()
endif()

# Final debug output before validation
message(STATUS "[DEBUG] Final LUA_INCLUDE_DIR: '${LUA_INCLUDE_DIR}'")
message(STATUS "[DEBUG] Final LUA_LIBRARIES: '${LUA_LIBRARIES}'")

# Simplified validation - LuaRocks should provide valid paths
if(NOT LUA_INCLUDE_DIR OR LUA_INCLUDE_DIR STREQUAL "")
  message(FATAL_ERROR "LUA_INCLUDE_DIR is not set. This should be provided by LuaRocks.")
endif()

if(NOT (EXISTS "${LUA_INCLUDE_DIR}/lua.hpp" OR EXISTS "${LUA_INCLUDE_DIR}/lua.h"))
  message(FATAL_ERROR "Lua headers not found in ${LUA_INCLUDE_DIR}.")
endif()

add_library(blaze MODULE
  src/luablaze.cpp
  src/luablaze.h
)

target_include_directories(blaze PRIVATE "${LUA_INCLUDE_DIR}")
target_link_libraries(blaze PRIVATE ${LUA_LIBRARIES})

# Pass Blaze version to the C++ code as a compile definition
target_compile_definitions(blaze PRIVATE BLAZE_LIBRARY_VERSION="${BLAZE_VERSION}")

set_target_properties(blaze PROPERTIES
  PREFIX ""
  OUTPUT_NAME "luablaze"
)

# Platform-specific settings
if(WIN32)
  # Windows: Lua C modules use .dll extension
  set_target_properties(blaze PROPERTIES SUFFIX ".dll")
  # MSVC-specific flags
  if(MSVC)
    target_compile_options(blaze PRIVATE /utf-8)
    # MSVC optimizations for Release builds
    target_compile_options(blaze PRIVATE
      $<$<CONFIG:Release>:/O2> # Full optimization
      $<$<CONFIG:Release>:/Ob2> # Inline any suitable function
      $<$<CONFIG:Release>:/Oi> # Enable intrinsic functions
      $<$<CONFIG:Release>:/Ot> # Favor fast code
      $<$<CONFIG:Release>:/GL> # Whole program optimization
      $<$<CONFIG:Release>:/DNDEBUG> # Disable assertions
    )
    target_link_options(blaze PRIVATE
      $<$<CONFIG:Release>:/LTCG> # Link-time code generation
    )
  endif()
elseif(APPLE)
  # macOS: Lua C modules use .so extension (not .dylib)
  set_target_properties(blaze PROPERTIES SUFFIX ".so")
else()
  # Linux: .so is the default, but be explicit
  set_target_properties(blaze PROPERTIES SUFFIX ".so")
endif()

# GCC/Clang optimizations (Linux, macOS, MinGW)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(blaze PRIVATE
    $<$<CONFIG:Release>:-O2> # Aggressive optimization
    $<$<CONFIG:Release>:-DNDEBUG> # Disable assertions
    $<$<CONFIG:Release>:-flto=auto> # Link-time optimization
    $<$<CONFIG:Release>:-fomit-frame-pointer> # Free up a register
  )
  target_link_options(blaze PRIVATE
    $<$<CONFIG:Release>:-flto=auto> # LTO at link time
  )
endif()

# Symbol visibility for shared library exports
set_target_properties(blaze PROPERTIES
  C_VISIBILITY_PRESET hidden
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
)

target_include_directories(blaze PRIVATE ${LUA_INCLUDE_DIR})

target_link_libraries(blaze PRIVATE
  ${LUA_LIBRARIES}
  sourcemeta::blaze::compiler
  sourcemeta::blaze::output
)

# Install target for LuaRocks
if(DEFINED INSTALL_CMOD)
  install(TARGETS blaze
    LIBRARY DESTINATION ${INSTALL_CMOD}
  )
endif()

# Debug: Print LuaRocks variables
message(STATUS "LUA_INCDIR: ${LUA_INCDIR}")
message(STATUS "LUA_LIBDIR: ${LUA_LIBDIR}") 
message(STATUS "LUA_LIBNAME: ${LUA_LIBNAME}")
message(STATUS "LUA_INCLUDE_DIR: ${LUA_INCLUDE_DIR}")
message(STATUS "LUA_LIBRARIES: ${LUA_LIBRARIES}")
message(STATUS "INSTALL_CMOD: ${INSTALL_CMOD}")

# Documentation generation with Doxygen
if(LUABLAZE_BUILD_DOCS)
  find_package(Doxygen)
  if(DOXYGEN_FOUND)
    set(DOXYGEN_IN ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile)
    set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
    
    # Copy Doxyfile to build directory
    configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)
    
    # Add custom target for documentation generation
    add_custom_target(docs
      COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Generating API documentation with Doxygen"
      VERBATIM
    )
    
    message(STATUS "Doxygen found - 'docs' target available (run: make docs)")
  else()
    message(WARNING "Doxygen not found - documentation cannot be built")
  endif()
endif()
