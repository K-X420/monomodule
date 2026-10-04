// Machinedrum names the library shows without an OS file: machines by firmware id, the kit's effect and routing
// parameters, pattern slots. The synthesis knob labels come from the OS file (MdFirmware) when there is one.
#pragma once
#include <string>

namespace mnm::mdnames {

std::string machineName(int id);   // "TRX-BD", "ROM-01", "GND---"; "ID 99" for an id this table does not name
bool isRom(int id);
bool isRam(int id);
// The 24 parameters of a kit track: 0-7 synthesis (labelled by the machine), then these 16
extern const char* const kTrackParamNames[16];   // AMD AMF EQF EQG FLTF FLTW FLTQ SRR DIST VOL PAN DEL REV LFOS LFOD LFOM
extern const char* const kMasterFxNames[4];      // REVERB DELAY EQ DYNAMIX
extern const char* const kMasterFxParams[4][8];
extern const char* const kLfoParamNames[24];     // the LFO's destination parameters
extern const char* const kLfoTypes[3];           // FREE TRIG HOLD

} // namespace mnm::mdnames
