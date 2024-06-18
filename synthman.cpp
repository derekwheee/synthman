#include "daisysp.h"
#include "daisy_pod.h"
#include "moogladder.h"
#include "synthvoice.h"

using namespace daisysp;
using namespace daisy;

#define NUM_NOTES 127
#define NUM_OSCILLATORS 3
#define POLYSYNTH_VOICES 8

static DaisyPod pod;
// static Oscillator osc[NUM_OSCILLATORS];
static Oscillator lfo;
static MoogLadder filter;
static Parameter pitchParam, osc2Detune, cutoffParam, resonanceParam, lfoParam;

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

bool heldNotes[NUM_NOTES];
float detune[NUM_OSCILLATORS];

ControlMode mode;
int wave[NUM_OSCILLATORS];
float vibrato;
float oscFreq;
float lfoFreq;
float lfoAmp;
float attack;
float release;
float cutoff;
float resonance;
float oldKnob1, oldKnob2, knob1, knob2;
bool isGateHigh;

float modeColorMap[4][3] = {
	{1.0, 0.5, 0},
	{1.0, 0, 0},
	{0, 1.0, 0},
	{1.0, 0, 1.0}};

void ConditionalParameter(float oldVal,
						  float newVal,
						  float &param,
						  float update);

void Controls();

void NextSamples(float &signal)
{
	float voiceSum = 0.0f;
	vibrato = lfo.Process();

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
		NextSamples(signal);

		// left output
		output[i] = signal;

		// right output
		output[i + 1] = signal;
	}
}

void handleNoteOn(int note, int millis)
{
	bool foundVoice = false;

	for (int i = 0; i < POLYSYNTH_VOICES; i++)
	{
		if (voices[i].note == -1)
		{
			voices[i].setFrequency(mtof(note), vibrato);
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

	voices[stalestVoiceIndex].setFrequency(mtof(note), vibrato);
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
				voices[i].detune = p.value / 127.0f;
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
			lfo.SetFreq((float)p.value / 127.0f * 100.0f);
			break;
		case 109: // LFO Amplitude
			lfo.SetAmp(((float)p.value / 127.0f) * 100);
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
	std::fill(heldNotes, heldNotes + NUM_NOTES, false);
	std::fill(detune, detune + NUM_OSCILLATORS, 1);

	// Set global variables
	float sample_rate;
	mode = VCO;
	vibrato = 0.0f;
	oscFreq = 1000.0f;
	oldKnob1 = oldKnob2 = 0;
	knob1 = knob2 = 0;
	attack = .01f;
	release = .2f;
	cutoff = 10000;
	lfoAmp = 1.0f;
	lfoFreq = 0.1f;

	// Init everything
	pod.Init();
	pod.SetAudioBlockSize(4);
	sample_rate = pod.AudioSampleRate();
	filter.Init(sample_rate);
	lfo.Init(sample_rate);

	// Set filter parameters
	filter.SetFreq(10000);
	filter.SetRes(0.8);

	for (int i = 0; i < POLYSYNTH_VOICES; i++)
	{
		voices[i].initialize(sample_rate);
	}

	// Set parameters for lfo
	lfo.SetWaveform(SINE);
	lfo.SetFreq(0.1);
	lfo.SetAmp(1);

	// set parameter parameters
	cutoffParam.Init(pod.knob1, 100, 20000, cutoffParam.LOGARITHMIC);
	resonanceParam.Init(pod.knob2, 0, 1, resonanceParam.LINEAR);
	pitchParam.Init(pod.knob1, 50, 5000, pitchParam.LOGARITHMIC);
	osc2Detune.Init(pod.knob2, 0.01, 2, osc2Detune.LINEAR);
	lfoParam.Init(pod.knob1, 0.25, 1000, lfoParam.LOGARITHMIC);

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
	case VCA:
		ConditionalParameter(oldKnob1, knob1, lfoFreq, lfoParam.Process());
		ConditionalParameter(oldKnob2, knob2, lfoAmp, pod.knob2.Process());
		lfo.SetFreq(lfoFreq);
		lfo.SetAmp(lfoAmp * 100);
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
