add_executable(eden-ps4 main.cpp platform.cpp ui_canvas.cpp ui_launcher.cpp ui_metadata.cpp ui_sound.cpp
    ${PS4_RUNTIME_SOURCES})
target_link_libraries(eden-ps4 PRIVATE common core input_common frontend_common video_core
    SceSysmodule SceSystemService SceUserService SceVideoOut SceAudioOut ScePad SceNet pthread z)
ps4_link_mesa(eden-ps4)
target_compile_definitions(eden-ps4 PRIVATE PS4_RUNTIME_DATA_DIR="/data/EdenPS4")
target_include_directories(eden-ps4 PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
# HOME menu text (Nunito, SIL Open Font License) and icons, baked into 8-bit masks.
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ps4_ui_assets.h"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/ps4/generate-ui-assets.py"
        "${DOLPHIN_PS4_ROOT}/fonts/Nunito-Regular.ttf"
        "${CMAKE_CURRENT_BINARY_DIR}/ps4_ui_assets.h"
    DEPENDS "${CMAKE_SOURCE_DIR}/ps4/generate-ui-assets.py"
        "${DOLPHIN_PS4_ROOT}/fonts/Nunito-Regular.ttf"
    VERBATIM)
target_sources(eden-ps4 PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/ps4_ui_assets.h")
ps4_add_eboot(eden-ps4)
