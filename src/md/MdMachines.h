// Machine families the OS treats specially (MainOS 0x20C930 and around): the MIDI machines MID-01..16 (MIDI out on
// channel 1..16) and the controller machines, which make no sound: CTR-AL turns a parameter on every track, CTR-8P
// eight assigned ones, CTR-RE / GB / EQ / DX the master delay / reverb / EQ / dynamix.
#pragma once

namespace mnm::md {

inline bool isMidMachine(int id) { return id >= 96 && id <= 111; }
inline bool isCtrMachine(int id) { return id == 112 || id == 113 || (id >= 120 && id <= 123); }
constexpr int kCtrAll = 112, kCtr8p = 113;
// CTR-RE (Rhythm Echo) = the delay, CTR-GB (Gate Box) = the reverb, CTR-EQ, CTR-DX: index into the master effects
// (0 reverb, 1 delay, 2 EQ, 3 dynamix); -1 for any other machine
inline int ctrMasterFx(int id) { return id == 120 ? 1 : id == 121 ? 0 : id == 122 ? 2 : id == 123 ? 3 : -1; }
inline bool isRamRecorder(int id) { return id == 160 || id == 161 || id == 165 || id == 166; }

} // namespace mnm::md
