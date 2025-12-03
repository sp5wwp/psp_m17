cmake_minimum_required(VERSION 3.5)

project(psp_m17)
set(title "PSP M17 Reflector Client")
set(version "01.10")

# Collect sources for libm17
FILE(GLOB libm17_src
    libm17/*.c
    libm17/decode/*.c
    libm17/encode/*.c
    libm17/math/*.c
    libm17/payload/*.c
    libm17/phy/*.c
)

# Collect sources for codec2
FILE(GLOB codec2_src
    codec2/src/codec2.c codec2/src/codec2_fft.c codec2/src/quantise.c
    codec2/src/filter.c codec2/src/interp.c codec2/src/kiss_fft.c codec2/src/kiss_fftr.c
    codec2/src/lpc.c codec2/src/lsp.c codec2/src/mbest.c codec2/src/newamp1.c
    codec2/src/nlp.c codec2/src/pack.c codec2/src/phase.c codec2/src/phi0.c
    codec2/src/postfilter.c codec2/src/phase.c codec2/src/sine.c

    codec2/build/src/codebook.c
    codec2/build/src/codebooknewamp1.c
    codec2/build/src/codebooknewamp1_energy.c
    codec2/build/src/codebookge.c
    codec2/build/src/codebookjmv.c
    codec2/build/src/codebookd.c
)

# Build libm17 static library
add_library(libm17 STATIC ${libm17_src})

target_include_directories(libm17 PUBLIC
    ${PROJECT_SOURCE_DIR}/libm17
)

# Build codec2 static library
add_library(codec2 STATIC ${codec2_src})

target_include_directories(codec2 PUBLIC
    ${PROJECT_SOURCE_DIR}/codec2/build
    ${PROJECT_SOURCE_DIR}/codec2/src
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
    codec2
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
