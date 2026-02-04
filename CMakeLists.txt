cmake_minimum_required(VERSION 3.5)

project(psp_m17)
set(title "PSP M17 Reflector Client")
set(version "01.20")

# Collect sources for libm17
file(GLOB_RECURSE libm17_src CONFIGURE_DEPENDS
    libm17/*.c
    libm17/*/*.c
)
list(FILTER libm17_src EXCLUDE REGEX "/libm17/unit_tests/")

# Collect sources for codec2
file(GLOB_RECURSE codec2_mod_src CONFIGURE_DEPENDS
    codec2-mod/src/*.c
)

# Build libm17 static library
add_library(libm17 STATIC ${libm17_src})

target_include_directories(libm17 PUBLIC
    ${PROJECT_SOURCE_DIR}/libm17
)

# Build codec2-mod static library
add_library(codec2-mod STATIC ${codec2_mod_src})

target_include_directories(codec2-mod PUBLIC
    ${PROJECT_SOURCE_DIR}/codec2-mod/src
    ${PROJECT_SOURCE_DIR}/codec2-mod/inc
)

# Build PSP application
add_executable(${PROJECT_NAME} main.c)

# Warnings only for our code (main.c)
target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall
    -Wextra
)

# Link our libraries and PSP libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    libm17
    codec2-mod
    pspdebug
    pspdisplay
    pspge
    pspnet_apctl
    pspnet
    pspaudiolib
    pspaudio
    psppower
)

# Create EBOOT.PBP
create_pbp_file(
    TARGET ${PROJECT_NAME}
    ICON_PATH "${PROJECT_SOURCE_DIR}/images/icon.png"
    BACKGROUND_PATH "${PROJECT_SOURCE_DIR}/images/bkg.png"
    PREVIEW_PATH NULL
    TITLE ${title}
    VERSION ${version}
)
