# Musashi 68000-family emulator (MIT, https://github.com/kstenerud/Musashi, commit 313ebf1). Runs the Machinedrum
# OS's ColdFire control handlers (knob -> DSP word conversions); see src/md/MdControl.cpp.
# m68kops.c/.h are generated once with m68kmake (m68kmake <dir> m68k_in.c) and kept in the tree.
# m68kconf.h is edited: instruction hook -> md_m68k_instruction_hook(), PMMU off.
add_library(musashi STATIC m68kcpu.c m68kops.c m68kdasm.c softfloat/softfloat.c)
target_include_directories(musashi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(musashi PROPERTIES LINKER_LANGUAGE C)
if(MSVC)
  target_compile_options(musashi PRIVATE /W0)
else()
  target_compile_options(musashi PRIVATE -w)
endif()
