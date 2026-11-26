// DirectSound (the Xbox's) for the Windows build, as a small software mixer:
// every Xbox buffer is memory the dash locks and writes, and a thread mixes
// the playing ones (resampled, with their volume) into 48 kHz stereo for
// waveOut. Play cursors, looping and position notifications follow the
// mixer, as the Xbox's follow its voice processor. Xbox ADPCM is decoded to
// PCM as it's written. 3D sound, effects, mix bins and streams aren't.
#include <xtl.h>
#include <mmsystem.h>

#include <math.h>
#include <string.h>
#include <vector>

#include <dsound.h>

#define MIX_RATE 48000
#define MIX_FRAMES 960   // 20 ms a block
#define MIX_BLOCKS 4

struct Notify
{
	DWORD offset;
	HANDLE event;
};

// An Xbox buffer. The dash only sees the pointer.
struct UxSoundBuffer
{
	DWORD magic;
	LONG refs;
	WAVEFORMATEX format;
	bool adpcm;                  // Xbox ADPCM: pcm holds the decoded samples
	std::vector<BYTE> data;      // what the dash locks and writes
	std::vector<short> pcm;      // ADPCM decoded (16-bit, interleaved)
	double frame;                // the play position in frames
	bool playing, looping, paused;
	float gain;
	std::vector<Notify> notifies;
};
#define BUFFER_MAGIC 0x55534E44u

struct UxDirectSound
{
	LONG refs;
};

static CRITICAL_SECTION s_lock;
// (Objects the dash's global constructors can reach are made on first use:
// the order of those constructors between files is the compiler's.)
static std::vector<UxSoundBuffer *> &s_buffers_()
{
	static std::vector<UxSoundBuffer *> value;
	return value;
}
#define s_buffers s_buffers_()
static HWAVEOUT s_waveOut;
static HANDLE s_mixThread, s_mixEvent;
static UxDirectSound s_device;
static DWORD s_peak[2], s_rms[2];

static void Lock()
{
	static LONG s_ready;
	if (InterlockedCompareExchange(&s_ready, 1, 0) == 0)
	{
		InitializeCriticalSection(&s_lock);
		InterlockedExchange(&s_ready, 2);
	}
	while (s_ready != 2)
		Sleep(0);
	EnterCriticalSection(&s_lock);
}

static void Unlock()
{
	LeaveCriticalSection(&s_lock);
}

static UxSoundBuffer *Get(LPDIRECTSOUNDBUFFER buffer)
{
	UxSoundBuffer *b = (UxSoundBuffer *)buffer;
	return b != NULL && b->magic == BUFFER_MAGIC ? b : NULL;
}

// ---- Xbox ADPCM (IMA ADPCM, 36-byte blocks per channel) ----

static const int kStepTable[89] = {
	7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
	31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
	130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
	544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
	2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
	9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const int kIndexTable[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

static short DecodeNibble(int nibble, int &predictor, int &index)
{
	int step = kStepTable[index];
	int diff = step >> 3;
	if (nibble & 1)
		diff += step >> 2;
	if (nibble & 2)
		diff += step >> 1;
	if (nibble & 4)
		diff += step;
	predictor += (nibble & 8) ? -diff : diff;
	predictor = predictor < -32768 ? -32768 : predictor > 32767 ? 32767 : predictor;
	index += kIndexTable[nibble];
	index = index < 0 ? 0 : index > 88 ? 88 : index;
	return (short)predictor;
}

#define ADPCM_SAMPLES 65   // a block: the header's sample and 64 more

static void DecodeAdpcm(UxSoundBuffer *b)
{
	int channels = b->format.nChannels ? b->format.nChannels : 1;
	size_t blockBytes = 36 * channels;
	size_t blocks = b->data.size() / blockBytes;
	b->pcm.assign(blocks * ADPCM_SAMPLES * channels, 0);
	for (size_t k = 0; k < blocks; k++)
	{
		const BYTE *block = &b->data[k * blockBytes];
		short *out = &b->pcm[k * ADPCM_SAMPLES * channels];
		for (int c = 0; c < channels; c++)
		{
			const BYTE *h = block + 4 * c;
			int predictor = (short)(h[0] | (h[1] << 8));
			int index = h[2] > 88 ? 88 : h[2];
			out[c] = (short)predictor;
			// The data: 4 bytes (8 samples) per channel in turn.
			for (int group = 0; group < 8; group++)
			{
				const BYTE *p = block + 4 * channels + (group * channels + c) * 4;
				for (int i = 0; i < 8; i++)
				{
					int nibble = (p[i / 2] >> ((i & 1) * 4)) & 15;
					int sample = 1 + group * 8 + i;
					out[sample * channels + c] = DecodeNibble(nibble, predictor, index);
				}
			}
		}
	}
}

// ---- the mixer ----

static DWORD FramesOf(const UxSoundBuffer *b)
{
	if (b->adpcm)
		return (DWORD)(b->pcm.size() / (b->format.nChannels ? b->format.nChannels : 1));
	return b->format.nBlockAlign ? (DWORD)(b->data.size() / b->format.nBlockAlign) : 0;
}

// A frame's byte offset in the buffer, as the dash counts.
static DWORD ByteOf(const UxSoundBuffer *b, DWORD frame)
{
	if (b->adpcm)
		return frame / ADPCM_SAMPLES * 36 * b->format.nChannels;
	return frame * b->format.nBlockAlign;
}

static DWORD FrameOf(const UxSoundBuffer *b, DWORD byte)
{
	if (b->adpcm)
		return byte / (36 * b->format.nChannels) * ADPCM_SAMPLES;
	return b->format.nBlockAlign ? byte / b->format.nBlockAlign : 0;
}

static void Sample(const UxSoundBuffer *b, DWORD frame, float &left, float &right)
{
	int channels = b->format.nChannels;
	if (b->adpcm)
	{
		const short *s = &b->pcm[frame * channels];
		left = s[0];
		right = channels > 1 ? s[1] : left;
		return;
	}
	const BYTE *p = &b->data[frame * b->format.nBlockAlign];
	if (b->format.wBitsPerSample == 8)
	{
		left = (float)((int)p[0] - 128) * 256.0f;
		right = channels > 1 ? (float)((int)p[1] - 128) * 256.0f : left;
	}
	else
	{
		left = (short)(p[0] | (p[1] << 8));
		right = channels > 1 ? (short)(p[2] | (p[3] << 8)) : left;
	}
}

static void FireNotifies(UxSoundBuffer *b, DWORD from, DWORD to, bool wrapped)
{
	for (size_t i = 0; i < b->notifies.size(); i++)
	{
		DWORD o = b->notifies[i].offset;
		if (o == DSBPN_OFFSETSTOP)
			continue;
		bool crossed = wrapped ? (o >= from || o < to) : (o >= from && o < to);
		if (crossed && b->notifies[i].event != NULL)
			SetEvent(b->notifies[i].event);
	}
}

static void FireStop(UxSoundBuffer *b)
{
	for (size_t i = 0; i < b->notifies.size(); i++)
		if (b->notifies[i].offset == DSBPN_OFFSETSTOP && b->notifies[i].event != NULL)
			SetEvent(b->notifies[i].event);
}

static void Mix(short *out)
{
	static float mix[MIX_FRAMES * 2];
	memset(mix, 0, sizeof(mix));
	Lock();
	for (size_t v = 0; v < s_buffers.size(); v++)
	{
		UxSoundBuffer *b = s_buffers[v];
		DWORD frames = FramesOf(b);
		if (!b->playing || b->paused || frames == 0 || b->format.nSamplesPerSec == 0)
			continue;
		double step = (double)b->format.nSamplesPerSec / MIX_RATE;
		DWORD startByte = ByteOf(b, (DWORD)b->frame);
		bool wrapped = false;
		for (int i = 0; i < MIX_FRAMES; i++)
		{
			DWORD f = (DWORD)b->frame;
			if (f >= frames)
			{
				if (!b->looping)
				{
					b->playing = false;
					b->frame = 0;
					break;
				}
				b->frame -= frames;
				wrapped = true;
				f = (DWORD)b->frame;
				if (f >= frames)
					f = 0;
			}
			float l0, r0, l1, r1;
			Sample(b, f, l0, r0);
			DWORD next = f + 1 < frames ? f + 1 : (b->looping ? 0 : f);
			Sample(b, next, l1, r1);
			float t = (float)(b->frame - f);
			mix[i * 2] += (l0 + (l1 - l0) * t) * b->gain;
			mix[i * 2 + 1] += (r0 + (r1 - r0) * t) * b->gain;
			b->frame += step;
		}
		DWORD endByte = b->playing ? ByteOf(b, (DWORD)b->frame % frames) : ByteOf(b, frames);
		FireNotifies(b, startByte, endByte, wrapped);
		if (!b->playing)
			FireStop(b);
	}
	Unlock();
	double sumL = 0, sumR = 0;
	DWORD peakL = 0, peakR = 0;
	for (int i = 0; i < MIX_FRAMES * 2; i++)
	{
		float s = mix[i];
		s = s < -32768.0f ? -32768.0f : s > 32767.0f ? 32767.0f : s;
		out[i] = (short)s;
		DWORD a = (DWORD)fabsf(s);
		if (i & 1)
		{
			sumR += (double)s * s;
			if (a > peakR)
				peakR = a;
		}
		else
		{
			sumL += (double)s * s;
			if (a > peakL)
				peakL = a;
		}
	}
	// (24-bit full scale, as the Xbox reports.)
	s_peak[0] = peakL << 8;
	s_peak[1] = peakR << 8;
	s_rms[0] = (DWORD)sqrt(sumL / MIX_FRAMES) << 8;
	s_rms[1] = (DWORD)sqrt(sumR / MIX_FRAMES) << 8;
}

static DWORD WINAPI MixThread(void *)
{
	static short blocks[MIX_BLOCKS][MIX_FRAMES * 2];
	WAVEHDR headers[MIX_BLOCKS];
	memset(headers, 0, sizeof(headers));
	for (int i = 0; i < MIX_BLOCKS; i++)
	{
		headers[i].lpData = (LPSTR)blocks[i];
		headers[i].dwBufferLength = sizeof(blocks[i]);
		waveOutPrepareHeader(s_waveOut, &headers[i], sizeof(WAVEHDR));
		Mix(blocks[i]);
		waveOutWrite(s_waveOut, &headers[i], sizeof(WAVEHDR));
	}
	for (;;)
	{
		WaitForSingleObject(s_mixEvent, 50);
		for (int i = 0; i < MIX_BLOCKS; i++)
		{
			if (!(headers[i].dwFlags & WHDR_DONE))
				continue;
			Mix(blocks[i]);
			headers[i].dwFlags &= ~WHDR_DONE;
			waveOutWrite(s_waveOut, &headers[i], sizeof(WAVEHDR));
		}
	}
	return 0;
}

static void StartMixer()
{
	if (s_mixThread != NULL)
		return;
	WAVEFORMATEX format;
	memset(&format, 0, sizeof(format));
	format.wFormatTag = WAVE_FORMAT_PCM;
	format.nChannels = 2;
	format.nSamplesPerSec = MIX_RATE;
	format.wBitsPerSample = 16;
	format.nBlockAlign = 4;
	format.nAvgBytesPerSec = MIX_RATE * 4;
	s_mixEvent = CreateEventA(NULL, FALSE, FALSE, NULL);
	if (waveOutOpen(&s_waveOut, WAVE_MAPPER, &format, (DWORD_PTR)s_mixEvent, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR)
	{
		s_waveOut = NULL;
		return;   // no sound device: everything still "plays", silently
	}
	s_mixThread = CreateThread(NULL, 0, MixThread, NULL, 0, NULL);
	SetThreadPriority(s_mixThread, THREAD_PRIORITY_TIME_CRITICAL);
}

// Without an output the buffers still need their cursors to move (the music
// player waits for its notifications): a timer thread "plays" them.
static DWORD WINAPI SilentThread(void *)
{
	static short block[MIX_FRAMES * 2];
	for (;;)
	{
		Sleep(MIX_FRAMES * 1000 / MIX_RATE);
		Mix(block);
	}
	return 0;
}

static void EnsureMixer()
{
	Lock();
	static bool s_started;
	if (!s_started)
	{
		s_started = true;
		StartMixer();
		if (s_waveOut == NULL)
			CloseHandle(CreateThread(NULL, 0, SilentThread, NULL, 0, NULL));
	}
	Unlock();
}

static void SetFormat(UxSoundBuffer *b, const WAVEFORMATEX *format)
{
	if (format == NULL)
	{
		memset(&b->format, 0, sizeof(b->format));
		return;
	}
	b->format = *format;
	b->format.cbSize = 0;
	b->adpcm = format->wFormatTag == WAVE_FORMAT_XBOX_ADPCM;
	if (!b->adpcm && b->format.nBlockAlign == 0)
		b->format.nBlockAlign = (WORD)(b->format.nChannels * b->format.wBitsPerSample / 8);
	if (b->adpcm)
		DecodeAdpcm(b);
}

extern "C" {

// (For where the SDK's headers don't have KSDATAFORMAT_SUBTYPE_PCM: dsound.h.)
const GUID UX_KSDATAFORMAT_SUBTYPE_PCM = {0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};

HRESULT WINAPI DirectSoundCreate(LPGUID, LPDIRECTSOUND *ds, LPUNKNOWN)
{
	EnsureMixer();
	InterlockedIncrement(&s_device.refs);
	*ds = (LPDIRECTSOUND)&s_device;
	return DS_OK;
}

ULONG WINAPI IDirectSound_AddRef(LPDIRECTSOUND)
{
	return InterlockedIncrement(&s_device.refs);
}

HRESULT WINAPI IDirectSound_GetTime(LPDIRECTSOUND, REFERENCE_TIME *time)
{
	// (100 ns units, as the Xbox's.)
	LARGE_INTEGER now, frequency;
	QueryPerformanceCounter(&now);
	QueryPerformanceFrequency(&frequency);
	if (time != NULL)
		*time = (REFERENCE_TIME)(now.QuadPart * 10000000.0 / frequency.QuadPart);
	return DS_OK;
}

ULONG WINAPI IDirectSound_Release(LPDIRECTSOUND)
{
	LONG refs = InterlockedDecrement(&s_device.refs);
	return refs > 0 ? refs : 0;
}

HRESULT WINAPI IDirectSound_SetMixBinHeadroom(LPDIRECTSOUND, DWORD, DWORD)
{
	return DS_OK;
}

HRESULT WINAPI IDirectSound_GetOutputLevels(LPDIRECTSOUND, DSOUTPUTLEVELS *levels, BOOL)
{
	memset(levels, 0, sizeof(*levels));
	levels->dwAnalogLeftTotalPeak = s_peak[0];
	levels->dwAnalogRightTotalPeak = s_peak[1];
	levels->dwAnalogLeftTotalRMS = s_rms[0];
	levels->dwAnalogRightTotalRMS = s_rms[1];
	levels->dwDigitalFrontLeftPeak = s_peak[0];
	levels->dwDigitalFrontRightPeak = s_peak[1];
	levels->dwDigitalFrontLeftRMS = s_rms[0];
	levels->dwDigitalFrontRightRMS = s_rms[1];
	levels->dwDigitalFrontCenterPeak = (s_peak[0] + s_peak[1]) / 2;
	levels->dwDigitalFrontCenterRMS = (s_rms[0] + s_rms[1]) / 2;
	return DS_OK;
}

HRESULT WINAPI DirectSoundCreateBuffer(LPCDSBUFFERDESC desc, LPDIRECTSOUNDBUFFER *buffer)
{
	*buffer = NULL;
	if (desc == NULL)
		return DSERR_INVALIDCALL;
	EnsureMixer();
	UxSoundBuffer *b = new UxSoundBuffer;
	b->magic = BUFFER_MAGIC;
	b->refs = 1;
	b->frame = 0;
	b->playing = b->looping = b->paused = false;
	b->gain = 1.0f;
	b->data.assign(desc->dwBufferBytes, 0);
	SetFormat(b, desc->lpwfxFormat);
	Lock();
	s_buffers.push_back(b);
	Unlock();
	*buffer = (LPDIRECTSOUNDBUFFER)b;
	return DS_OK;
}

HRESULT WINAPI IDirectSound_CreateSoundBuffer(LPDIRECTSOUND, LPCDSBUFFERDESC desc, LPDIRECTSOUNDBUFFER *buffer,
                                              LPUNKNOWN)
{
	return DirectSoundCreateBuffer(desc, buffer);
}

void WINAPI DirectSoundDoWork(void)
{
}

void WINAPI DirectSoundUseLightHRTF(void)
{
}

void WINAPI DirectSoundOverrideSpeakerConfig(DWORD)
{
}

HRESULT WINAPI XAudioDownloadEffectsImage(LPCSTR, LPCDSEFFECTIMAGELOC, DWORD, LPDSEFFECTIMAGEDESC *desc)
{
	if (desc != NULL)
		*desc = NULL;
	return DS_OK;
}

ULONG WINAPI IDirectSoundBuffer_Release(LPDIRECTSOUNDBUFFER buffer)
{
	UxSoundBuffer *b = Get(buffer);
	if (b == NULL)
		return 0;
	Lock();
	LONG refs = --b->refs;
	if (refs == 0)
	{
		for (size_t i = 0; i < s_buffers.size(); i++)
			if (s_buffers[i] == b)
			{
				s_buffers.erase(s_buffers.begin() + i);
				break;
			}
		b->magic = 0;
		delete b;
	}
	Unlock();
	return refs;
}

HRESULT WINAPI IDirectSoundBuffer_SetBufferData(LPDIRECTSOUNDBUFFER buffer, LPVOID data, DWORD bytes)
{
	UxSoundBuffer *b = Get(buffer);
	if (b == NULL)
		return DSERR_INVALIDCALL;
	Lock();
	if (data != NULL)
		b->data.assign((const BYTE *)data, (const BYTE *)data + bytes);
	else
		b->data.assign(bytes, 0);
	if (b->adpcm)
		DecodeAdpcm(b);
	b->frame = 0;
	Unlock();
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_Lock(LPDIRECTSOUNDBUFFER buffer, DWORD offset, DWORD bytes, LPVOID *ptr1,
                                       LPDWORD bytes1, LPVOID *ptr2, LPDWORD bytes2, DWORD flags)
{
	UxSoundBuffer *b = Get(buffer);
	if (b == NULL || b->data.empty())
		return DSERR_INVALIDCALL;
	DWORD size = (DWORD)b->data.size();
	if (flags & DSBLOCK_ENTIREBUFFER)
	{
		offset = 0;
		bytes = size;
	}
	else if (flags & DSBLOCK_FROMWRITECURSOR)
	{
		offset = ByteOf(b, (DWORD)b->frame);
	}
	offset %= size;
	if (bytes > size)
		bytes = size;
	DWORD first = size - offset < bytes ? size - offset : bytes;
	*ptr1 = &b->data[offset];
	*bytes1 = first;
	if (ptr2 != NULL)
		*ptr2 = bytes > first ? &b->data[0] : NULL;
	if (bytes2 != NULL)
		*bytes2 = bytes - first;
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_Unlock(LPDIRECTSOUNDBUFFER buffer, LPVOID, DWORD, LPVOID, DWORD)
{
	UxSoundBuffer *b = Get(buffer);
	if (b == NULL)
		return DSERR_INVALIDCALL;
	if (b->adpcm)
	{
		Lock();
		DecodeAdpcm(b);
		Unlock();
	}
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_Play(LPDIRECTSOUNDBUFFER buffer, DWORD, DWORD, DWORD flags)
{
	UxSoundBuffer *b = Get(buffer);
	if (b == NULL)
		return DSERR_INVALIDCALL;
	Lock();
	b->looping = (flags & DSBPLAY_LOOPING) != 0;
	b->playing = true;
	b->paused = false;
	Unlock();
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_Stop(LPDIRECTSOUNDBUFFER buffer)
{
	UxSoundBuffer *b = Get(buffer);
	if (b == NULL)
		return DSERR_INVALIDCALL;
	Lock();
	bool was = b->playing;
	b->playing = false;
	b->paused = false;
	if (was)
		FireStop(b);
	Unlock();
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_StopEx(LPDIRECTSOUNDBUFFER buffer, REFERENCE_TIME, DWORD)
{
	return IDirectSoundBuffer_Stop(buffer);
}

HRESULT WINAPI IDirectSoundBuffer_Pause(LPDIRECTSOUNDBUFFER buffer, DWORD pause)
{
	UxSoundBuffer *b = Get(buffer);
	if (b == NULL)
		return DSERR_INVALIDCALL;
	Lock();
	b->paused = pause != 0;
	Unlock();
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_GetStatus(LPDIRECTSOUNDBUFFER buffer, LPDWORD status)
{
	UxSoundBuffer *b = Get(buffer);
	*status = 0;
	if (b == NULL)
		return DSERR_INVALIDCALL;
	Lock();
	if (b->playing)
		*status = DSBSTATUS_PLAYING | (b->looping ? DSBSTATUS_LOOPING : 0) | (b->paused ? DSBSTATUS_PAUSED : 0);
	Unlock();
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_GetCurrentPosition(LPDIRECTSOUNDBUFFER buffer, LPDWORD play, LPDWORD write)
{
	UxSoundBuffer *b = Get(buffer);
	if (b == NULL)
		return DSERR_INVALIDCALL;
	Lock();
	DWORD frames = FramesOf(b);
	DWORD frame = frames != 0 ? (DWORD)b->frame % frames : 0;
	DWORD byte = ByteOf(b, frame);
	Unlock();
	if (play != NULL)
		*play = byte;
	if (write != NULL)
	{
		// A block ahead, as a voice processor reads ahead.
		DWORD ahead = byte + (DWORD)((ULONGLONG)b->format.nAvgBytesPerSec * MIX_FRAMES / MIX_RATE);
		*write = b->data.empty() ? 0 : ahead % (DWORD)b->data.size();
	}
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_SetCurrentPosition(LPDIRECTSOUNDBUFFER buffer, DWORD position)
{
	UxSoundBuffer *b = Get(buffer);
	if (b == NULL)
		return DSERR_INVALIDCALL;
	Lock();
	b->frame = FrameOf(b, position);
	Unlock();
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_SetFormat(LPDIRECTSOUNDBUFFER buffer, LPCWAVEFORMATEX format)
{
	UxSoundBuffer *b = Get(buffer);
	if (b == NULL)
		return DSERR_INVALIDCALL;
	Lock();
	SetFormat(b, format);
	b->frame = 0;
	Unlock();
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_SetVolume(LPDIRECTSOUNDBUFFER buffer, LONG volume)
{
	UxSoundBuffer *b = Get(buffer);
	if (b == NULL)
		return DSERR_INVALIDCALL;
	if (volume > 0)
		volume = 0;
	b->gain = volume <= -10000 ? 0.0f : powf(10.0f, (float)volume / 2000.0f);
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_SetHeadroom(LPDIRECTSOUNDBUFFER, DWORD)
{
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_SetMixBins(LPDIRECTSOUNDBUFFER, LPCDSMIXBINS)
{
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_GetVoiceProperties(LPDIRECTSOUNDBUFFER, LPDSVOICEPROPS props)
{
	if (props != NULL)
		memset(props, 0, sizeof(*props));
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_SetNotificationPositions(LPDIRECTSOUNDBUFFER buffer, DWORD count,
                                                           LPCDSBPOSITIONNOTIFY notifies)
{
	UxSoundBuffer *b = Get(buffer);
	if (b == NULL)
		return DSERR_INVALIDCALL;
	Lock();
	b->notifies.clear();
	for (DWORD i = 0; i < count; i++)
	{
		Notify n;
		n.offset = notifies[i].dwOffset;
		n.event = notifies[i].hEventNotify;
		b->notifies.push_back(n);
	}
	Unlock();
	return DS_OK;
}

}   // extern "C"
