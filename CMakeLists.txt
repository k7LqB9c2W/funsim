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
find_package(SDL2_ttf CONFIG QUIET)
find_package(nlohmann_json CONFIG REQUIRED)

if (NOT SDL2_ttf_FOUND)
  set(SDL2_TTF_ROOT "${CMAKE_SOURCE_DIR}/third_party/SDL2_ttf")
  find_path(SDL2_TTF_INCLUDE_DIR SDL_ttf.h
            PATHS "${SDL2_TTF_ROOT}/include" "${SDL2_TTF_ROOT}/include/SDL2"
            NO_DEFAULT_PATH)
  find_library(SDL2_TTF_LIBRARY SDL2_ttf
               PATHS "${SDL2_TTF_ROOT}/lib/x64" "${SDL2_TTF_ROOT}/lib"
               NO_DEFAULT_PATH)
  if (SDL2_TTF_INCLUDE_DIR AND SDL2_TTF_LIBRARY)
    add_library(SDL2_ttf::SDL2_ttf UNKNOWN IMPORTED)
    set_target_properties(SDL2_ttf::SDL2_ttf PROPERTIES
      IMPORTED_LOCATION "${SDL2_TTF_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${SDL2_TTF_INCLUDE_DIR}"
    )
    set(SDL2_ttf_FOUND TRUE)
    find_file(SDL2_TTF_DLL SDL2_ttf.dll
              PATHS "${SDL2_TTF_ROOT}/lib/x64" "${SDL2_TTF_ROOT}/lib"
              NO_DEFAULT_PATH)
  endif()
endif()

if (NOT SDL2_ttf_FOUND)
  message(FATAL_ERROR "SDL2_ttf not found. Install via vcpkg or place it under third_party/SDL2_ttf.")
endif()

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
  src/settlements.cpp
  src/factions.cpp
  src/ui.cpp
  src/util.cpp
  ${IMGUI_SOURCES}
)

target_include_directories(funsim PRIVATE src ${IMGUI_DIR} ${IMGUI_DIR}/backends)

target_link_libraries(funsim PRIVATE SDL2::SDL2 SDL2::SDL2main SDL2_image::SDL2_image
                                   SDL2_ttf::SDL2_ttf nlohmann_json::nlohmann_json)

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

if (SDL2_TTF_DLL)
  add_custom_command(TARGET funsim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${SDL2_TTF_DLL}
    $<TARGET_FILE_DIR:funsim>
  )
endif()
