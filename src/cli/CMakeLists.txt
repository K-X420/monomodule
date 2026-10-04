add_executable(mnm-render mnm_render.cpp)
target_link_libraries(mnm-render PRIVATE mnmcore)
add_executable(md-probe md_probe.cpp)
target_link_libraries(md-probe PRIVATE mnmcore)
