#include "synthvoice.h"
#include "daisysp.h"

using namespace daisysp;

SynthVoice::SynthVoice() {}
SynthVoice::~SynthVoice() {}

void SynthVoice::initialize(float sampleRate)
{
    note = -1;
    detune = 1.0f;
    frequency_ = 440.0f;

    oscillator[0].Init(sampleRate);
    oscillator[0].SetFreq(frequency_);
    oscillator[0].SetAmp(1);

    oscillator[1].Init(sampleRate);
    oscillator[1].SetFreq(frequency_ * detune);
    oscillator[1].SetAmp(1);

    lfo.Init(sampleRate);
    lfo.SetWaveform(SINE);
    lfo.SetFreq(0.1);
    lfo.SetAmp(0);

    envelope.Init(sampleRate);

    setProfile(DEFAULT);
}

void SynthVoice::setProfile(Profile nextProfile)
{
    switch (nextProfile)
    {
    case NUMBER_2:
        oscillator[0].SetWaveform(oscillator[0].WAVE_TRI);
        oscillator[1].SetWaveform(oscillator[1].WAVE_SQUARE);
        detune = 1.3333f;
        break;
    case BUZZSAW:
        oscillator[0].SetWaveform(oscillator[0].WAVE_SAW);
        oscillator[1].SetWaveform(oscillator[1].WAVE_SQUARE);
        detune = 0.5f;
        break;
    case DEFAULT:
    default:
        oscillator[0].SetWaveform(oscillator[0].WAVE_SIN);
        oscillator[1].SetWaveform(oscillator[1].WAVE_TRI);
        detune = 2.0f;
        break;
    }
}

void SynthVoice::setFrequency()
{
    setFrequency(frequency_);
}

void SynthVoice::setFrequency(float frequency)
{
    frequency_ = frequency;
    oscillator[0].SetFreq(frequency);
    oscillator[1].SetFreq((frequency * detune));
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
    // TODO: This LFO isn't working right :(
    // float vibrato = lfo.Process();
    float level = envelope.Process(note > -1);

    // setFrequency(frequency_ + vibrato);

    float osc1 = oscillator[0].Process();
    float osc2 = oscillator[1].Process();

    return ((osc1 + osc2) / 2) * level;
}