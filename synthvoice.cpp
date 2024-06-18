#include "synthvoice.h"
#include "daisysp.h"

using namespace daisysp;

SynthVoice::SynthVoice() {}
SynthVoice::~SynthVoice() {}

void SynthVoice::initialize(float sampleRate)
{
    note = -1;
    detune = 1.0f;

    oscillator[0].Init(sampleRate);
    oscillator[0].SetFreq(440);
    oscillator[0].SetAmp(1);

    oscillator[1].Init(sampleRate);
    oscillator[1].SetFreq(440 * detune);
    oscillator[1].SetAmp(1);

    envelope.Init(sampleRate);

    setProfile(DEFAULT);
}

void SynthVoice::setProfile(Profile nextProfile)
{
    switch (nextProfile)
    {
    case BUZZSAW:
        oscillator[0].SetWaveform(oscillator[0].WAVE_SAW);
        oscillator[1].SetWaveform(oscillator[1].WAVE_SQUARE);
        detune = 2.0f;
        break;
    case DEFAULT:
    default:
        oscillator[0].SetWaveform(oscillator[0].WAVE_SIN);
        oscillator[1].SetWaveform(oscillator[1].WAVE_TRI);
        break;
    }
}

void SynthVoice::setFrequency(float frequency, float vibrato)
{
    oscillator[0].SetFreq(frequency + vibrato);
    oscillator[1].SetFreq((frequency * detune) + vibrato);
}

void SynthVoice::trigger()
{
    envelope.Retrigger(false);
}

void SynthVoice::release()
{
    note = -1;
}

float SynthVoice::getSample()
{
    float level = envelope.Process(note > -1);
    return ((oscillator[0].Process() + oscillator[1].Process()) / 2) * level;
}