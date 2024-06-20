#ifndef SYNTHVOICE_H
#define SYNTHVOICE_H
#include "daisysp.h"
#include "reverbsc.h"

using namespace daisysp;

enum Waveform
{
    SINE,
    TRIANGLE,
    SAW,
    SQUARE,
    // POLYBLEP_TRI,
    // POLYBLEP_SAW,
    // POLYBLEP_SQUARE,
    __WF_COUNT
};

enum Profile
{
    DEFAULT,
    NUMBER_2,
    BUZZSAW,
    __P_COUNT
};

class SynthVoice
{
public:
    SynthVoice();
    ~SynthVoice();

    Oscillator oscillator[2];
    Oscillator lfo;
    Adsr envelope;
    Profile profile;
    int note;
    float detune;
    int lastNoteMs;

    void initialize(float sampleRate);
    void setProfile(Profile profile);
    void setFrequency();
    void setFrequency(float frequency);
    void trigger();
    void release();
    float getSample();

private:
    float frequency_;
};

#endif // SYNTHVOICE_H