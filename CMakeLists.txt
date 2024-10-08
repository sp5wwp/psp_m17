cmake_minimum_required(VERSION 3.5)

project(psp_m17)
set(title "PSP M17 Reflector Client")
set(version "01.00")

#sources list for the libm17
FILE(GLOB libm17
    libm17/*.c libm17/decode/*.c libm17/encode/*.c libm17/math/*.c libm17/payload/*.c libm17/phy/*.c
)

#sources for codec2
FILE(GLOB codec2
    codec2/src/codec2.c codec2/src/codec2_fft.c codec2/src/quantise.c
    codec2/src/filter.c codec2/src/interp.c codec2/src/kiss_fft.c codec2/src/kiss_fftr.c
    codec2/src/lpc.c codec2/src/lsp.c codec2/src/mbest.c codec2/src/newamp1.c
    codec2/src/nlp.c codec2/src/pack.c codec2/src/phase.c codec2/src/phi0.c
    codec2/src/postfilter.c codec2/src/phase.c codec2/src/sine.c
    #codebooks
    codec2/build/src/codebook.c
    codec2/build/src/codebooknewamp1.c
    codec2/build/src/codebooknewamp1_energy.c
    codec2/build/src/codebookge.c
    codec2/build/src/codebookjmv.c
    codec2/build/src/codebookd.c
)

add_executable(${PROJECT_NAME} main.c)

target_sources(${PROJECT_NAME} PRIVATE
    ${libm17}
    ${codec2}
)

#some fancy psp libs
target_link_libraries(${PROJECT_NAME} PRIVATE
    pspdebug
    pspdisplay
    pspge
    pspnet_apctl
    pspnet
)

#add includes
target_include_directories(${PROJECT_NAME} PRIVATE
    libm17
    codec2/build
    codec2/src
)

#create an EBOOT.PBP file
create_pbp_file(
    TARGET ${PROJECT_NAME}
    ICON_PATH "${PROJECT_SOURCE_DIR}/images/icon.png"
    BACKGROUND_PATH "${PROJECT_SOURCE_DIR}/images/bkg.png"
    PREVIEW_PATH NULL
    TITLE ${title}
    VERSION ${version}
)

