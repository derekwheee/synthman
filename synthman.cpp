#include "daisysp.h"
#include "daisy_pod.h"
#include "moogladder.h"
#include "synthvoice.h"

using namespace daisysp;
using namespace daisy;

#define NUM_NOTES 127
#define NUM_OSCILLATORS 3
#define POLYSYNTH_VOICES 8
#define MAX_DELAY static_cast<size_t>(48000 * 2.5f)

static DaisyPod pod;
static MoogLadder filter;
static Parameter pitchParam, osc2Detune, cutoffParam, resonanceParam, lfoParam;
static ReverbSc DSY_SDRAM_BSS reverb;
static DelayLine<float, MAX_DELAY> DSY_SDRAM_BSS delayLeft;
static DelayLine<float, MAX_DELAY> DSY_SDRAM_BSS delayRight;

SynthVoice voices[POLYSYNTH_VOICES];

enum ControlMode
{
	VCO,
	FILTER,
	ENVELOPE,
	VCA,
	__COUNT
};

int numWaveforms = static_cast<Waveform>(__WF_COUNT);
int numProfiles = static_cast<Profile>(__WF_COUNT);

ControlMode mode;
int wave[NUM_OSCILLATORS];

float sample_rate;
float vibrato;
float oscFreq;
float lfoFreq;
float lfoAmp;
float attack;
float release;
float cutoff;
float resonance;
float reverbMix;
float oldKnob1, oldKnob2, knob1, knob2;
bool isGateHigh;

// Delay
float currentDelay;
float delayFeedback;
float delayTarget;

float modeColorMap[4][3] = {
	{1.0, 0.5, 0},
	{1.0, 0, 0},
	{0, 1.0, 0},
	{1.0, 0, 1.0}};

void getReverbSample(float in1, float in2, float &out1, float &out2);
void getDelaySample(float in1, float in2, float &out1, float &out2);

void ConditionalParameter(float oldVal,
						  float newVal,
						  float &param,
						  float update);

void Controls();

void NextSamples(float &signal)
{
	float voiceSum = 0.0f;

	for (int i = 0; i < POLYSYNTH_VOICES; i++)
	{
		voiceSum += voices[i].getSample();
	}

	signal = voiceSum / POLYSYNTH_VOICES;
	signal = filter.Process(signal);
}

static void AudioCallback(AudioHandle::InterleavingInputBuffer input,
						  AudioHandle::InterleavingOutputBuffer output,
						  size_t size)
{
	Controls();

	for (size_t i = 0; i < size; i += 2)
	{
		float signal;
		float out1, out2;
		NextSamples(signal);
		getReverbSample(signal, signal, out1, out2);
		getDelaySample(signal, signal, out1, out2);

		// left output
		output[i] = out1;

		// right output
		output[i + 1] = out2;
	}
}

void handleNoteOn(int note, int millis)
{
	bool foundVoice = false;

	for (int i = 0; i < POLYSYNTH_VOICES; i++)
	{
		if (voices[i].note == -1)
		{
			voices[i].setFrequency(mtof(note));
			voices[i].note = note;
			voices[i].lastNoteMs = millis;
			voices[i].trigger();
			foundVoice = true;
			break;
		}
	}

	if (foundVoice)
	{
		return;
	}

	int stalestVoiceIndex = POLYSYNTH_VOICES - 1;
	float stalestVoice = -1;

	for (int i = 0; i < POLYSYNTH_VOICES; i++)
	{
		if (stalestVoice == -1 || voices[i].lastNoteMs < stalestVoice)
		{
			stalestVoiceIndex = i;
			stalestVoice = voices[i].lastNoteMs;
		}
	}

	voices[stalestVoiceIndex].setFrequency(mtof(note));
	voices[stalestVoiceIndex].note = note;
	voices[stalestVoiceIndex].lastNoteMs = millis;
	voices[stalestVoiceIndex].trigger();
}

void handleNoteOff(int note)
{
	for (int i = 0; i < POLYSYNTH_VOICES; i++)
	{
		if (voices[i].note == note)
		{
			voices[i].note = -1;
			voices[i].release();
			break;
		}
	}
}

void updateEnvelopeParams(int segment, float value)
{
	for (int i = 0; i < POLYSYNTH_VOICES; i++)
	{
		switch (segment)
		{
		case ADSR_SEG_ATTACK:
		case ADSR_SEG_DECAY:
		case ADSR_SEG_RELEASE:
			voices[i].envelope.SetTime(segment, value);
			break;
		default:
			voices[i].envelope.SetSustainLevel(value);
			break;
		}
	}
}

// Typical Switch case for Message Type.
void HandleMidiMessage(MidiEvent m)
{
	int millis = System::GetNow();

	switch (m.type)
	{
	case NoteOn:
	{
		NoteOnEvent p = m.AsNoteOn();
		handleNoteOn(p.note, millis);
	}
	break;
	case NoteOff:
	{
		NoteOnEvent p = m.AsNoteOn();
		handleNoteOff(p.note);
	}
	case ControlChange:
	{
		ControlChangeEvent p = m.AsControlChange();
		switch (p.control_number)
		{
		case 96: // set voice profile
			for (int i = 0; i < POLYSYNTH_VOICES; i++)
			{
				voices[i].setProfile(static_cast<Profile>(round((p.value / 127.0f) * numProfiles)));
			}
			break;
		case 105: // detune voices
			for (int i = 0; i < POLYSYNTH_VOICES; i++)
			{
				voices[i].detune = (p.value / 127.0f) * 4.0f;
				voices[i].setFrequency();
			}
			break;
		case 97: // Cutoff
			filter.SetFreq(mtof((float)p.value));
			break;
		case 106: // Resonance
			filter.SetRes(((float)p.value / 127.0f));
			break;
		case 98: // Attack
			updateEnvelopeParams(ADSR_SEG_ATTACK, ((float)p.value / 127.0f) * 2.0f);
			break;
		case 107: // Decay
			updateEnvelopeParams(ADSR_SEG_DECAY, ((float)p.value / 127.0f));
			break;
		case 99: // Sustain
			updateEnvelopeParams(-1, (float)p.value / 127.0f);
			break;
		case 108: // Release
			updateEnvelopeParams(ADSR_SEG_RELEASE, ((float)p.value / 127.0f));
			break;
		case 100: // LFO Frequency
			// TODO: Make this logarithmic
			for (int i = 0; i < POLYSYNTH_VOICES; i++)
			{
				voices[i].lfo.SetFreq(((float)p.value / 127.0f) * 1000.0f);
			}
			break;
		case 109: // LFO Amplitude
			for (int i = 0; i < POLYSYNTH_VOICES; i++)
			{
				voices[i].lfo.SetAmp((float)p.value / 127.0f);
			}
			break;
		case 101: // Reverb mix
			reverbMix = (float)p.value / 127.0f;
			break;
		case 110: // Reverb feedback
			reverb.SetFeedback((float)p.value / 127.0f);
			break;
		case 102: // Delay feedback
			delayFeedback = (float)p.value / 127.0f;
			break;
		case 111: // Delay time
			currentDelay = delayTarget = sample_rate * ((float)p.value / 127.0f);
			break;
		default:
			break;
		}
		break;
	}
	default:
		break;
	}
}

int main(void)
{
	// Set global variables
	mode = VCO;
	vibrato = 0.0f;
	oscFreq = 1000.0f;
	oldKnob1 = oldKnob2 = 0;
	knob1 = knob2 = 0;
	attack = .01f;
	release = .2f;
	cutoff = 10000;
	reverbMix = 0.5f;

	// Init everything
	pod.Init();
	pod.SetAudioBlockSize(4);
	sample_rate = pod.AudioSampleRate();
	filter.Init(sample_rate);

	// Set filter parameters
	filter.SetFreq(10000);
	filter.SetRes(0.8);

	reverb.Init(sample_rate);
	reverb.SetLpFreq(18000.0f);
	reverb.SetFeedback(0.85f);

	delayLeft.Init();
	delayRight.Init();
	currentDelay = delayTarget = sample_rate * 0.75f;
	delayFeedback = 0.5f;
	delayLeft.SetDelay(currentDelay);
	delayRight.SetDelay(currentDelay);

	for (int i = 0; i < POLYSYNTH_VOICES; i++)
	{
		voices[i].initialize(sample_rate);
	}

	// set parameter parameters
	cutoffParam.Init(pod.knob1, 100, 20000, cutoffParam.LOGARITHMIC);
	resonanceParam.Init(pod.knob2, 0, 1, resonanceParam.LINEAR);
	pitchParam.Init(pod.knob1, 50, 5000, pitchParam.LOGARITHMIC);
	osc2Detune.Init(pod.knob2, 0.01, 2, osc2Detune.LINEAR);

	// start callback
	pod.StartAdc();
	pod.StartAudio(AudioCallback);
	pod.midi.StartReceive();

	while (1)
	{
		pod.midi.Listen();
		// Handle MIDI Events
		while (pod.midi.HasEvents())
		{
			HandleMidiMessage(pod.midi.PopEvent());
		}
	}
}

// Updates values if knob had changed
void ConditionalParameter(float oldVal,
						  float newVal,
						  float &param,
						  float update)
{
	if (abs(oldVal - newVal) > 0.00005)
	{
		param = update;
	}
}

// Controls Helpers
void UpdateEncoder()
{
	// for (int i = 0; i < 2; i++)
	// {
	// 	wave[i] = updateWaveform(i, pod.encoder.Increment());
	// 	osc[0].SetWaveform(wave[i]);
	// }

	mode = static_cast<ControlMode>((mode + pod.encoder.RisingEdge()) % static_cast<ControlMode>(__COUNT));
}

void UpdateKnobs()
{
	knob1 = pod.knob1.Process();
	knob2 = pod.knob2.Process();

	switch (mode)
	{
	case VCO:
		// ConditionalParameter(oldKnob1, knob1, oscFreq, pitchParam.Process());
		// ConditionalParameter(oldKnob2, knob2, osc2DetuneAmt, osc2Detune.Process());
		break;
	case FILTER:
		// ConditionalParameter(oldKnob1, knob1, cutoff, cutoffParam.Process());
		// ConditionalParameter(oldKnob2, knob2, resonance, resonanceParam.Process());
		// filter.SetFreq(cutoff);
		// filter.SetRes(resonance);
		break;
	case ENVELOPE:
		// ConditionalParameter(oldKnob1, knob1, attack, pod.knob1.Process());
		// ConditionalParameter(oldKnob2, knob2, release, pod.knob2.Process());
		// envelope.SetTime(ADSR_SEG_ATTACK, attack);
		// envelope.SetTime(ADSR_SEG_RELEASE, release);
		break;
	// case LFO:
	// 	ConditionalParameter(oldKnob1, knob1, lfoFreq, lfoParam.Process());
	// 	ConditionalParameter(oldKnob2, knob2, lfoAmp, pod.knob2.Process());
	// 	lfo.SetFreq(lfoFreq);
	// 	lfo.SetAmp(lfoAmp * 100);
	// case VCA:
	// 	ConditionalParameter(oldKnob1, knob1, lfoFreq, lfoParam.Process());
	// 	ConditionalParameter(oldKnob2, knob2, lfoAmp, pod.knob2.Process());
	// 	lfo.SetFreq(lfoFreq);
	// 	lfo.SetAmp(lfoAmp * 100);
	default:
		break;
	}
}

void UpdateLeds()
{
	pod.led1.Set(modeColorMap[mode][0], modeColorMap[mode][1], modeColorMap[mode][2]);
	pod.led2.Set(0, 1, 1);

	oldKnob1 = knob1;
	oldKnob2 = knob2;

	pod.UpdateLeds();
}

void UpdateButtons()
{
	if (pod.button1.RisingEdge())
	{
	}

	if (pod.button2.RisingEdge())
	{
	}
}

void Controls()
{
	pod.ProcessAnalogControls();
	pod.ProcessDigitalControls();

	UpdateEncoder();

	UpdateKnobs();

	UpdateLeds();

	UpdateButtons();
}

void getReverbSample(float in1, float in2, float &out1, float &out2)
{

	reverb.Process(in1, in2, &out1, &out2);
	out1 = reverbMix * out1 + (1 - reverbMix) * in1;
	out2 = reverbMix * out2 + (1 - reverbMix) * in2;
}

void getDelaySample(float in1, float in2, float &out1, float &out2)
{
	fonepole(currentDelay, delayTarget, .00007f);
	delayRight.SetDelay(currentDelay);
	delayLeft.SetDelay(currentDelay);

	out1 = delayRight.Read();
	out2 = delayLeft.Read();

	delayRight.Write((delayFeedback * out1) + in1);
	out1 = (delayFeedback * out1) + in1;

	delayLeft.Write((delayFeedback * out2) + in2);
	out2 = (delayFeedback * out2) + in2;
}