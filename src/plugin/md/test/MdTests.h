// md-plugintest's test areas: each runs when its MD_<NAME>_TEST variable is set (value: the variable's), returns the exit code
#pragma once
#include "MdProcessor.h"

namespace mnm::plugin::md::test {

struct TestEnv {
    MdProcessor& proc;   // prepared at rate / block, the OS (argv[1]) loaded
    double rate;
    int block;
    int argc;
    char** argv;
};

int mdSeqTest(TestEnv& env, const char* value);
int mdRecTest(TestEnv& env, const char* value);
int mdSongedTest(TestEnv& env, const char* value);
int mdGroupTest(TestEnv& env, const char* value);
int mdGridTest(TestEnv& env, const char* value);
int mdEditTest(TestEnv& env, const char* value);
int mdMixerTest(TestEnv& env, const char* value);
int mdArrowTest(TestEnv& env, const char* value);
int mdFourTest(TestEnv& env, const char* value);
int mdMidiTest(TestEnv& env, const char* value);
int mdCtrTest(TestEnv& env, const char* value);
int mdOscheckTest(TestEnv& env, const char* value);
int mdRateTest(TestEnv& env, const char* value);
int mdParityTest(TestEnv& env, const char* value);
int mdXtraTest(TestEnv& env, const char* value);

} // namespace mnm::plugin::md::test
