cmake_minimum_required(VERSION 3.5)

project(psp_m17)

#sources list for the libm17
FILE(GLOB libm17 libm17/*.c libm17/decode/*.c libm17/encode/*.c libm17/math/*.c libm17/payload/*.c libm17/phy/*.c)

add_executable(${PROJECT_NAME} main.c ${libm17})

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
)

#create an EBOOT.PBP file
create_pbp_file(
    TARGET ${PROJECT_NAME}
    ICON_PATH NULL
    BACKGROUND_PATH NULL
    PREVIEW_PATH NULL
    TITLE ${PROJECT_NAME}
    VERSION 01.00
)

