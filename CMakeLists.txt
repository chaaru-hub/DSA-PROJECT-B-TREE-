cmake_minimum_required(VERSION 3.16)
project(btree_visualizer CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(BUILD_CONSOLE "Build the terminal animation" ON)
option(BUILD_GUI "Build the graphical visualizer (downloads raylib)" ON)

if(BUILD_CONSOLE AND NOT EMSCRIPTEN)
  add_executable(btree_animation btree_animation.cpp)
  add_executable(btree_tests tests/test_btree.cpp)
  enable_testing()
  add_test(NAME btree_tests COMMAND btree_tests)
endif()

if(BUILD_GUI)
  include(FetchContent)
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(raylib)

  add_executable(btree_gui gui/btree_gui.cpp)
  target_link_libraries(btree_gui PRIVATE raylib)

  if(EMSCRIPTEN)
    # Single self-contained page: fonts embedded, plain JavaScript output.
    set_target_properties(btree_gui PROPERTIES SUFFIX ".html")
    target_link_options(btree_gui PRIVATE
      -sUSE_GLFW=3 -sALLOW_MEMORY_GROWTH=1 -sSINGLE_FILE=1
      --embed-file ${CMAKE_SOURCE_DIR}/gui/assets@assets
      --shell-file ${CMAKE_SOURCE_DIR}/gui/web/shell.html)
  else()
    add_custom_command(TARGET btree_gui POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              ${CMAKE_SOURCE_DIR}/gui/assets $<TARGET_FILE_DIR:btree_gui>/assets)
    if(APPLE)
      target_link_libraries(btree_gui PRIVATE "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
    endif()
  endif()
endif()
