#ifndef SYNTHVOICE_H
#define SYNTHVOICE_H
#include "daisysp.h"

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

enum Profile {
    DEFAULT,
    BUZZSAW
};

class SynthVoice
{
public:
    SynthVoice();
    ~SynthVoice();

    Oscillator oscillator[2];
    Adsr envelope;
    Profile profile;
    int note;
    float detune;
    int lastNoteMs;

    void initialize(float sampleRate);
    void setProfile(Profile profile);
    void setFrequency(float frequency, float vibrato);
    void trigger();
    void release();
    float getSample();
};

#endif // SYNTHVOICE_H