#include "MenuItem.h"

#include <cstring>
#include <map>

#include "../common/FileUtil.h"
#include "../common/StrUtil.h"
#include "../render/D3D.h"
#include "../render/Texture.h"
#include "../xml/XmlDocument.h"

// Icons by lowercased path, shared by all menu items. Each entry holds one
// reference.
static std::map<std::string, IDirect3DTexture8 *> s_iconCache;   // 0x0027794C

// 0x000220C2
// Quirk: the icon is only kept when the item has text, and m_pszText isn't
// set at all without text (it was left uninitialized).
CMenuItem::CMenuItem(const char *text, const char *iconPath, BOOL isList)
{
	m_pElement = NULL;
	m_pTexture = NULL;
	m_pszText = NULL;
	m_strIcon.clear();
	m_unknown24 = TRUE;
	m_bIsList = FALSE;
	if (text != NULL && strlen(text) != 0)
	{
		size_t len = strlen(text);
		m_pszText = new wchar_t[len + 1];
		memset(m_pszText, 0, len * sizeof(wchar_t));
		StrUtf8ToWide(text, m_pszText, (unsigned)len);
		// The original strdup()s the path to lowercase it and never frees it.
		m_strIcon = iconPath == NULL ? std::string("") : StrToLower(iconPath);
	}
	// (Fix: an item without text gets an empty one. The menu reads the text
	// unconditionally, so a text-less item, such as an <Item> with no value
	// or a scanned XBE with no title, crashed it.)
	if (m_pszText == NULL)
	{
		m_pszText = new wchar_t[1];
		if (m_pszText != NULL)
			m_pszText[0] = 0;
	}
	m_bIsList = isList;
}

// 0x00022183
CMenuItem::~CMenuItem()
{
	delete[] m_pszText;
	m_pszText = NULL;
	if (m_pTexture != NULL)
	{
		m_pTexture->Release();
		m_pTexture = NULL;
	}
	m_pElement = NULL;
	m_strIcon.clear();
}

// Reads an .xbx (an XPR with a single texture) straight into a new texture.
// Returns FALSE if the file isn't one; *texture may still have been set.
static BOOL LoadXbxIcon(HANDLE file, IDirect3DTexture8 **texture)
{
	// XPR header followed by the texture's resource header.
	struct
	{
		XPR_HEADER xpr;
		D3DResource texture;   // D3DTexture: Common, Data, Lock, Format, Size
		DWORD format;
		DWORD size;
		DWORD endMarker;       // 0xFFFFFFFF
	} header;
	memset(&header, 0, sizeof(header));
	DWORD read;
	BOOL ok = ReadFile(file, &header, 0x24, &read, NULL);
	ok &= read == 0x24 && (header.xpr.dwMagic == XPR0_MAGIC || header.xpr.dwMagic == XPR1_MAGIC) &&
	      header.xpr.dwHeaderSize == 0x800 && header.endMarker == 0xFFFFFFFF &&
	      header.texture.Common < 0x828480 && D3DResource_GetType(&header.texture) == D3DRTYPE_TEXTURE;
	if (!ok)
		return FALSE;

	D3DSURFACE_DESC desc;
	Get2DSurfaceDesc(&header.texture, 0, &desc);
	if (desc.Height == 0)
	{
		desc.Height = 128;
		desc.Width = 128;
		desc.Format = D3DFMT_DXT1;
	}
	*texture = NULL;
	g_pd3dDevice->CreateTexture(desc.Width, desc.Height, 1, 0, desc.Format, 0, texture);
	if (*texture == NULL)
		return TRUE;   // handled, but no texture

	D3DLOCKED_RECT locked;
	locked.pBits = NULL;
	(*texture)->LockRect(0, &locked, NULL, D3DLOCK_READONLY);
	SetFilePointer(file, 0x800, NULL, FILE_BEGIN);
	DWORD dataSize = header.xpr.dwTotalSize - header.xpr.dwHeaderSize;
	// (Fix: the file's size is checked against the texture before the read.)
	if (header.xpr.dwTotalSize < header.xpr.dwHeaderSize || !XprTextureDataFits(*texture, locked, &dataSize) ||
	    !ReadFile(file, locked.pBits, dataSize, &read, NULL) || read != dataSize)
		*texture = NULL;   // sic: the texture is leaked
	return TRUE;
}

// An icon path the XBE scan made: <drive>:\udata\<title id>\titleimage.xbx,
// or z:\media\<title id>.xbx when running from DVD. (Lowercase.)
static BOOL IsTitleImageCachePath(const std::string &path)
{
	const char *p = path.c_str();
	const char *hex;
	if (path.size() == 32 && p[1] == ':' && strncmp(p + 2, "\\udata\\", 7) == 0 &&
	    strcmp(p + 17, "\\titleimage.xbx") == 0)
		hex = p + 9;
	else if (path.size() == 21 && strncmp(p, "z:\\media\\", 9) == 0 && strcmp(p + 17, ".xbx") == 0)
		hex = p + 9;
	else
		return FALSE;
	for (int i = 0; i < 8; i++)
	{
		if (!((hex[i] >= '0' && hex[i] <= '9') || (hex[i] >= 'a' && hex[i] <= 'f')))
			return FALSE;
	}
	return TRUE;
}

// 0x0002220D
void CMenuItem::LoadIcon()
{
	if (m_pTexture != NULL || m_strIcon.size() == 0)
		return;
	// (The list loader's thread loads icons too; s_iconCache is shared.)
	CD3DLock d3dLock;

	// (Fix: an XBE's automatic icon is its title image, copied by the scan
	// to E:\UDATA\<title id>\TitleImage.xbx and loaded from there. Every XBE
	// with the same title id (homebrew often shares one, or has none) got the
	// first one's picture, and one whose image couldn't be copied got none.
	// It's now read from the XBE itself, cached by the XBE's path.)
	if (m_pElement != NULL && IsTitleImageCachePath(StrToLower(m_strIcon)))
	{
		std::string xbe = m_pElement->GetAttribute("Action");
		if (xbe.size() > 4 && StrCmpNoCase(StrRight(xbe, 4), ".xbe") == 0)
		{
			if (StrFind(xbe, ":") == -1)
				ResolvePath(xbe.c_str(), xbe);
			std::string key = StrToLower(xbe);
			std::map<std::string, IDirect3DTexture8 *>::iterator it = s_iconCache.find(key);
			if (it != s_iconCache.end() && it->second != NULL)
			{
				m_pTexture = it->second;
				m_pTexture->AddRef();
				return;
			}
			if (FileExists(xbe.c_str()) && LoadXbeIcon(xbe.c_str(), &m_pTexture) && m_pTexture != NULL)
			{
				s_iconCache[key] = m_pTexture;
				m_pTexture->AddRef();
				return;
			}
			// No usable title image in the XBE: whatever is in the shared
			// file is another XBE's, so no icon.
			m_pTexture = NULL;
			m_strIcon.clear();
			return;
		}
	}

	if (StrFind(m_strIcon, ":") == -1)
		ResolvePath(m_strIcon.c_str(), m_strIcon);
	if (!FileExists(m_strIcon.c_str()))
	{
		m_strIcon.clear();
		return;
	}

	std::string key = StrToLower(m_strIcon);
	std::map<std::string, IDirect3DTexture8 *>::iterator it = s_iconCache.find(key);
	if (it != s_iconCache.end())
	{
		m_pTexture = s_iconCache[key];
		if (m_pTexture != NULL)
		{
			m_pTexture->AddRef();
			return;
		}
	}

	if (StrCmpNoCase(StrRight(key, 4), ".xbx") == 0)
	{
		HANDLE file = CreateFileA(m_strIcon.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
		if (file != INVALID_HANDLE_VALUE)
		{
			BOOL handled = LoadXbxIcon(file, &m_pTexture);
			if (handled)
			{
				if (m_pTexture == NULL)
					m_strIcon.clear();
				CloseHandle(file);
			}
			else
			{
				// Not a single-texture XPR: let D3DX try, then the packed
				// resource loader.
				CloseHandle(file);
				if (g_pd3dDevice != NULL &&
				    D3DXCreateTextureFromFileExA(g_pd3dDevice, key.c_str(), (UINT)-1, (UINT)-1, 1, 0,
				                                 (D3DFORMAT)-1, 1, 1, 1, 0, NULL, NULL, &m_pTexture) == S_OK)
				{
					Sleep(1);
				}
				else if (!LoadXprFileTexture(m_strIcon.c_str(), &m_pTexture))
				{
					m_pTexture = NULL;
					m_strIcon.clear();
				}
			}
		}
	}
	else
	{
		BOOL loaded;
		if (StrCmpNoCase(StrRight(key, 4), ".xbe") == 0)
			loaded = LoadXbeIcon(key.c_str(), &m_pTexture);
		else
			loaded = LoadTextureFromFileMax(m_strIcon.c_str(), g_pd3dDevice, &m_pTexture, 128, 128);
		if (!loaded)
		{
			m_pTexture = NULL;
			m_strIcon.clear();
		}
	}

	if (m_pTexture != NULL)
	{
		s_iconCache[key] = m_pTexture;
		m_pTexture->AddRef();
	}
	else
	{
		m_strIcon.clear();
	}
}

void PruneIconCache()
{
	std::map<std::string, IDirect3DTexture8 *>::iterator it = s_iconCache.begin();
	while (it != s_iconCache.end())
	{
		IDirect3DTexture8 *texture = it->second;
		// Only the cache's reference left (and this one): unused.
		ULONG count = texture != NULL ? texture->AddRef() : 0;
		if (texture != NULL)
			texture->Release();
		if (texture == NULL || count <= 2)
		{
			if (texture != NULL)
				texture->Release();
			s_iconCache.erase(it++);
		}
		else
		{
			++it;
		}
	}
}

void FreeIconCache()
{
	for (std::map<std::string, IDirect3DTexture8 *>::iterator it = s_iconCache.begin(); it != s_iconCache.end(); ++it)
	{
		if (it->second != NULL)
			it->second->Release();
	}
	s_iconCache.clear();
}
