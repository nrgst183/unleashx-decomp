// What the Windows build doesn't do (yet): XACT (the WMA playlists), the WMA
// decoder, XMV video and XFONT's TrueType fonts. Everything that creates one
// fails, so the dash goes its other ways; the rest is never reached.
#include <xtl.h>

#include <string.h>
#include <dsound.h>
#include <xact.h>
#include <xfont.h>
#include <xmv.h>

extern "C" {

// ---- XACT ----

HRESULT WINAPI XACTEngineCreate(PCXACT_RUNTIME_PARAMETERS, PXACTENGINE *engine)
{
	if (engine != NULL)
		*engine = NULL;
	return E_NOTIMPL;
}

void WINAPI XACTEngineDoWork(void)
{
}

void WINAPI XACTEngineSetFileIOCallbacks(PCXACT_FILEIO_CALLBACKS)
{
}

HRESULT WINAPI IXACTEngine_CreateSoundBank(PXACTENGINE, PVOID, DWORD, PXACTSOUNDBANK *bank)
{
	*bank = NULL;
	return E_NOTIMPL;
}

HRESULT WINAPI IXACTEngine_CreateSoundSource(PXACTENGINE, DWORD, PXACTSOUNDSOURCE *source)
{
	*source = NULL;
	return E_NOTIMPL;
}

HRESULT WINAPI IXACTEngine_GetNotification(PXACTENGINE, PCXACT_NOTIFICATION_DESCRIPTION, PXACT_NOTIFICATION)
{
	return E_FAIL;
}

HRESULT WINAPI IXACTEngine_GlobalPause(PXACTENGINE, WORD, BOOL)
{
	return S_OK;
}

HRESULT WINAPI IXACTEngine_RegisterNotification(PXACTENGINE, PCXACT_NOTIFICATION_DESCRIPTION)
{
	return S_OK;
}

ULONG WINAPI IXACTEngine_Release(PXACTENGINE)
{
	return 0;
}

HRESULT WINAPI IXACTEngine_SetMasterVolume(PXACTENGINE, WORD, LONG)
{
	return S_OK;
}

HRESULT WINAPI IXACTSoundBank_CreateWmaPlayList(PXACTSOUNDBANK, DWORD, DWORD, PXACTWMAPLAYLIST *list)
{
	*list = NULL;
	return E_NOTIMPL;
}

HRESULT WINAPI IXACTSoundBank_GetSoundCueIndexFromFriendlyName(PXACTSOUNDBANK, PCSTR, PDWORD)
{
	return E_FAIL;
}

HRESULT WINAPI IXACTSoundBank_PlayEx(PXACTSOUNDBANK, PCXACT_PREPARE_SOUNDCUE, PXACTSOUNDCUE *cue)
{
	if (cue != NULL)
		*cue = NULL;
	return E_FAIL;
}

HRESULT WINAPI IXACTSoundBank_Stop(PXACTSOUNDBANK, DWORD, DWORD, PXACTSOUNDCUE)
{
	return S_OK;
}

ULONG WINAPI IXACTSoundSource_Release(PXACTSOUNDSOURCE)
{
	return 0;
}

HRESULT WINAPI IXACTSoundSource_SetMixBinVolumes(PXACTSOUNDSOURCE, LPCDSMIXBINS)
{
	return S_OK;
}

HRESULT WINAPI IXACTSoundSource_SetMixBins(PXACTSOUNDSOURCE, LPCDSMIXBINS)
{
	return S_OK;
}

HRESULT WINAPI IXACTSoundSource_StopSoundCues(PXACTSOUNDSOURCE)
{
	return S_OK;
}

HRESULT WINAPI IXACTWmaPlayList_Add(PXACTWMAPLAYLIST, PCXACT_WMA_PLAYLIST_ADD, PXACTWMASONG *song)
{
	if (song != NULL)
		*song = NULL;
	return E_FAIL;
}

HRESULT WINAPI IXACTWmaPlayList_GetCurrentSongInfo(PXACTWMAPLAYLIST, PDWORD length, PWCHAR name, DWORD size,
                                                   PXACTWMASONG *song)
{
	if (length != NULL)
		*length = 0;
	if (name != NULL && size > 0)
		name[0] = 0;
	if (song != NULL)
		*song = NULL;
	return E_FAIL;
}

HRESULT WINAPI IXACTWmaPlayList_GetCurrentSongInfoEx(PXACTWMAPLAYLIST, PXACT_WMASONG_DESCRIPTION, PXACTWMASONG *song)
{
	if (song != NULL)
		*song = NULL;
	return E_FAIL;
}

HRESULT WINAPI IXACTWmaPlayList_GetProperties(PXACTWMAPLAYLIST, PXACT_WMA_PLAYLIST_PROPERTIES)
{
	return E_FAIL;
}

HRESULT WINAPI IXACTWmaPlayList_Next(PXACTWMAPLAYLIST)
{
	return E_FAIL;
}

HRESULT WINAPI IXACTWmaPlayList_SetCurrent(PXACTWMAPLAYLIST, PXACTWMASONG)
{
	return E_FAIL;
}

HRESULT WINAPI IXACTWmaPlayList_Previous(PXACTWMAPLAYLIST)
{
	return E_FAIL;
}

ULONG WINAPI IXACTWmaPlayList_Release(PXACTWMAPLAYLIST)
{
	return 0;
}

// ---- WMA ----

HRESULT WINAPI WmaCreateDecoder(LPCSTR, HANDLE, BOOL, DWORD, DWORD, DWORD, LPWAVEFORMATEX, XFileMediaObject **object)
{
	if (object != NULL)
		*object = NULL;
	return E_NOTIMPL;
}

// ---- XMV video ----

HRESULT WINAPI XMVDecoder_CreateDecoderForFile(DWORD, LPCSTR, XMVDecoder **decoder)
{
	if (decoder != NULL)
		*decoder = NULL;
	return E_NOTIMPL;
}

void WINAPI XMVDecoder_CloseDecoder(XMVDecoder *)
{
}

void WINAPI XMVDecoder_DisableAudioStream(XMVDecoder *, DWORD)
{
}

HRESULT WINAPI XMVDecoder_EnableAudioStream(XMVDecoder *, DWORD, DWORD, DSMIXBINS *, IDirectSoundStream **stream)
{
	if (stream != NULL)
		*stream = NULL;
	return E_NOTIMPL;
}

void WINAPI XMVDecoder_GetAudioDescriptor(XMVDecoder *, DWORD, XMVAUDIO_DESC *desc)
{
	if (desc != NULL)
		memset(desc, 0, sizeof(*desc));
}

HRESULT WINAPI XMVDecoder_GetNextFrame(XMVDecoder *, IDirect3DSurface8 *, XMVRESULT *result, DWORD *)
{
	if (result != NULL)
		*result = XMV_ENDOFFILE;
	return E_FAIL;
}

void WINAPI XMVDecoder_GetVideoDescriptor(XMVDecoder *, XMVVIDEO_DESC *desc)
{
	if (desc != NULL)
		memset(desc, 0, sizeof(*desc));
}

HRESULT WINAPI XMVDecoder_Reset(XMVDecoder *)
{
	return E_FAIL;
}

void WINAPI XMVDecoder_TerminateLoop(XMVDecoder *)
{
}

// The streams only come from the XMV decoder.
HRESULT WINAPI DirectSoundCreateStream(LPCDSSTREAMDESC, LPDIRECTSOUNDSTREAM *stream)
{
	if (stream != NULL)
		*stream = NULL;
	return E_NOTIMPL;
}

HRESULT WINAPI IDirectSoundStream_GetVoiceProperties(LPDIRECTSOUNDSTREAM, LPDSVOICEPROPS props)
{
	if (props != NULL)
		memset(props, 0, sizeof(*props));
	return S_OK;
}

HRESULT WINAPI IDirectSoundStream_SetFormat(LPDIRECTSOUNDSTREAM, LPCWAVEFORMATEX)
{
	return S_OK;
}

HRESULT WINAPI IDirectSoundStream_SetMixBins(LPDIRECTSOUNDSTREAM, LPCDSMIXBINS)
{
	return S_OK;
}

HRESULT WINAPI IDirectSoundStream_FlushEx(LPDIRECTSOUNDSTREAM, REFERENCE_TIME, DWORD)
{
	return S_OK;
}

HRESULT WINAPI IDirectSoundStream_Pause(LPDIRECTSOUNDSTREAM, DWORD)
{
	return S_OK;
}

HRESULT WINAPI IDirectSoundStream_SetHeadroom(LPDIRECTSOUNDSTREAM, DWORD)
{
	return S_OK;
}

HRESULT WINAPI IDirectSoundStream_SetVolume(LPDIRECTSOUNDSTREAM, LONG)
{
	return S_OK;
}

// ---- XFONT (TrueType) ----

HRESULT WINAPI XFONT_OpenTrueTypeFont(LPCWSTR, unsigned, XFONT **font)
{
	if (font != NULL)
		*font = NULL;
	return E_NOTIMPL;
}

void WINAPI XFONT_GetFontMetrics(XFONT *, unsigned *height, unsigned *descent)
{
	if (height != NULL)
		*height = 0;
	if (descent != NULL)
		*descent = 0;
}

unsigned WINAPI XFONT_GetIntercharacterSpacing(XFONT *)
{
	return 0;
}

HRESULT WINAPI XFONT_GetTextExtent(XFONT *, LPCWSTR, unsigned, unsigned *width)
{
	if (width != NULL)
		*width = 0;
	return E_FAIL;
}

unsigned WINAPI XFONT_GetTextHeight(XFONT *)
{
	return 0;
}

void WINAPI XFONT_SetClippingRectangle(XFONT *, D3DRECT *)
{
}

void WINAPI XFONT_SetTextAlignment(XFONT *, unsigned)
{
}

HRESULT WINAPI XFONT_SetTextAntialiasLevel(XFONT *, unsigned)
{
	return S_OK;
}

void WINAPI XFONT_SetTextColor(XFONT *, D3DCOLOR)
{
}

HRESULT WINAPI XFONT_SetTextHeight(XFONT *, unsigned)
{
	return S_OK;
}

HRESULT WINAPI XFONT_TextOut(XFONT *, IDirect3DSurface8 *, LPCWSTR, unsigned, long, long)
{
	return E_FAIL;
}

}   // extern "C"
