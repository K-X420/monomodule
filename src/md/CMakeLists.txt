# Machinedrum engine: the user's MD OS file (MainOS + DSP2 voices), ColdFire control handlers run under Musashi.
add_library(mdcore STATIC MdFirmware.cpp MdControl.cpp MdVoiceEngine.cpp MdMixEngine.cpp MdKit.cpp MdDump.cpp MdCatalog.cpp MdProject.cpp MdNames.cpp MdEngine.cpp MdPreview.cpp MdSequencer.cpp)
target_include_directories(mdcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mdcore PUBLIC mnmcore PRIVATE musashi)

add_executable(md-render md_render.cpp)
target_link_libraries(md-render PRIVATE mdcore)
