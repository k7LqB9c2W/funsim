cmake_minimum_required(VERSION 3.20)

if (NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if (DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
  else()
    find_program(VCPKG_EXECUTABLE vcpkg)
    if (VCPKG_EXECUTABLE)
      get_filename_component(VCPKG_ROOT_DIR "${VCPKG_EXECUTABLE}" DIRECTORY)
      set(_vcpkg_toolchain "${VCPKG_ROOT_DIR}/scripts/buildsystems/vcpkg.cmake")
      if (EXISTS "${_vcpkg_toolchain}")
        set(CMAKE_TOOLCHAIN_FILE "${_vcpkg_toolchain}"
            CACHE STRING "Vcpkg toolchain file")
      endif()
    endif()
  endif()
endif()

project(funsim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_image CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
set(IMGUI_SOURCES
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
  ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer2.cpp
)

add_executable(funsim
  src/main.cpp
  src/app.cpp
  src/world.cpp
  src/humans.cpp
  src/tools.cpp
  src/render.cpp
  src/ui.cpp
  src/util.cpp
  ${IMGUI_SOURCES}
)

target_include_directories(funsim PRIVATE src ${IMGUI_DIR} ${IMGUI_DIR}/backends)

target_link_libraries(funsim PRIVATE SDL2::SDL2 SDL2::SDL2main SDL2_image::SDL2_image
                                   nlohmann_json::nlohmann_json)

if (MSVC)
  target_compile_options(funsim PRIVATE /W4)
else()
  target_compile_options(funsim PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_custom_command(TARGET funsim POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
  ${CMAKE_SOURCE_DIR}/assets
  $<TARGET_FILE_DIR:funsim>/assets
)
