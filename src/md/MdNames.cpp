#include "MdNames.h"

namespace mnm::mdnames {

namespace {
struct Named { int id; const char* name; };
constexpr Named kMachines[] = {
    {0, "GND---"}, {1, "GND-SN"}, {2, "GND-NS"}, {3, "GND-IM"},
    {16, "TRX-BD"}, {17, "TRX-SD"}, {18, "TRX-XT"}, {19, "TRX-CP"}, {20, "TRX-RS"}, {21, "TRX-CB"}, {22, "TRX-CH"},
    {23, "TRX-OH"}, {24, "TRX-CY"}, {25, "TRX-MA"}, {26, "TRX-CL"}, {27, "TRX-XC"}, {28, "TRX-B2"},
    {32, "EFM-BD"}, {33, "EFM-SD"}, {34, "EFM-XT"}, {35, "EFM-CP"}, {36, "EFM-RS"}, {37, "EFM-CB"}, {38, "EFM-HH"}, {39, "EFM-CY"},
    {48, "E12-BD"}, {49, "E12-SD"}, {50, "E12-HT"}, {51, "E12-LT"}, {52, "E12-CP"}, {53, "E12-RS"}, {54, "E12-CB"},
    {55, "E12-CH"}, {56, "E12-OH"}, {57, "E12-RC"}, {58, "E12-CC"}, {59, "E12-BR"}, {60, "E12-TA"}, {61, "E12-TR"},
    {62, "E12-SH"}, {63, "E12-BC"},
    {64, "P-I-BD"}, {65, "P-I-SD"}, {66, "P-I-MT"}, {67, "P-I-ML"}, {68, "P-I-MA"}, {69, "P-I-RS"}, {70, "P-I-RC"},
    {71, "P-I-CC"}, {72, "P-I-HH"},
    {80, "INP-GA"}, {81, "INP-GB"}, {82, "INP-FA"}, {83, "INP-FB"}, {84, "INP-EA"}, {85, "INP-EB"},
    {160, "RAM-R1"}, {161, "RAM-R2"}, {162, "RAM-P1"}, {163, "RAM-P2"}, {165, "RAM-R3"}, {166, "RAM-R4"}, {167, "RAM-P3"}, {168, "RAM-P4"},
};
} // namespace

bool isRom(int id) { return (id >= 128 && id <= 159) || (id >= 176 && id <= 191); }
bool isRam(int id) { return id >= 160 && id <= 168 && id != 164; }

std::string machineName(int id)
{
    for (const auto& m : kMachines) if (m.id == id) return m.name;
    if (isRom(id)) {
        const int n = id < 160 ? id - 127 : id - 143;   // ROM-01..32, ROM-33..48
        return std::string("ROM-") + (n < 10 ? "0" : "") + std::to_string(n);
    }
    return "ID " + std::to_string(id);
}

const char* const kTrackParamNames[16] = {"AMD", "AMF", "EQF", "EQG", "FLTF", "FLTW", "FLTQ", "SRR", "DIST", "VOL", "PAN", "DEL", "REV", "LFOS", "LFOD", "LFOM"};
const char* const kMasterFxNames[4] = {"REVERB", "DELAY", "EQ", "DYNAMIX"};
const char* const kMasterFxParams[4][8] = {
    {"DVOL", "PRED", "DEC", "DAMP", "HP", "LP", "GATE", "LEV"},
    {"TIME", "MOD", "MFRQ", "FB", "FLTF", "FLTW", "MONO", "LEV"},
    {"LF", "LG", "HF", "HG", "PF", "PG", "PQ", "GAIN"},
    {"ATCK", "REL", "TRHD", "RTIO", "KNEE", "HP", "OUTG", "MIX"},
};
const char* const kLfoParamNames[24] = {"SYN1", "SYN2", "SYN3", "SYN4", "SYN5", "SYN6", "SYN7", "SYN8",
                                        "AMD", "AMF", "EQF", "EQG", "FLTF", "FLTW", "FLTQ", "SRR",
                                        "DIST", "VOL", "PAN", "DEL", "REV", "LFOS", "LFOD", "LFOM"};
const char* const kLfoTypes[3] = {"FREE", "TRIG", "HOLD"};

} // namespace mnm::mdnames
