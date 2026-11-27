#include "FileUtil.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../settings/AppSettings.h"
#include "../system/Drives.h"
#include "../system/FileIO.h"
#include "StrUtil.h"

#include "XboxKernel.h"

// 0x0009DE34
BOOL FileExists(const char *path)
{
	if (path == NULL || strlen(path) == 0)
		return FALSE;
	DWORD attributes = GetFileAttributesA(path);
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

// 0x0009DDFC
BOOL DirectoryExists(const char *path)
{
	if (path == NULL || strlen(path) == 0)
		return FALSE;
	DWORD attributes = GetFileAttributesA(path);
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

// 0x000A4AAD
BOOL IsDriveAvailable(char drive)
{
	std::string root;
	StrFormat(root, "%c:\\", toupper(drive));
	return GetFileAttributesA(root.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// 0x000A3745
std::string ReadTextFile(const char *path)
{
	std::string text;

	FILE *fp = fopen(path, "rb");
	if (fp == NULL)
		return std::string("");

	fseek(fp, 0, SEEK_END);
	long size = ftell(fp);
	if (size > 0)
	{
		fseek(fp, 0, SEEK_SET);
		char *buf = new char[size];
		if (buf == NULL)
		{
			fclose(fp);
			return std::string("");
		}
		memset(buf, 0, size);
		if (fread(buf, size, 1, fp) == 0)
		{
			delete[] buf;
			fclose(fp);
			return std::string("");
		}
		text.assign(buf, size);
		delete[] buf;
	}
	fclose(fp);
	return text;
}

// 0x000A446C
std::string ReadMetaFile(const char *path)
{
	if (path == NULL || strlen(path) == 0)
		return std::string("");
	FILE *fp = fopen(path, "rb");
	if (fp == NULL)
		return std::string("");
	fseek(fp, 0, SEEK_END);
	long size = ftell(fp);
	if (size < 2)
	{
		// (The original leaves the file open here.)
		fclose(fp);
		return std::string("");
	}
	fseek(fp, 0, SEEK_SET);
	unsigned char bom[2];
	if (fread(bom, 2, 1, fp) == 0)
	{
		fclose(fp);
		return std::string("");
	}
	if (!((bom[0] == 0xFF && bom[1] == 0xFE) || (bom[0] == 0xFE && bom[1] == 0xFF)))
	{
		fclose(fp);
		return ReadTextFile(path);
	}
	// UTF-16 after the mark. (The original asks for twice the size.)
	unsigned char *bytes = new unsigned char[size * 2];
	size_t count = fread(bytes, 1, (size_t)(size - 2), fp);
	fclose(fp);
	size_t chars = count / 2;
	std::wstring wide;
	for (size_t i = 0; i < chars; i++)
		wide += (wchar_t)(bytes[i * 2] | (bytes[i * 2 + 1] << 8));
	delete[] bytes;
	std::string text;
	for (size_t i = 0; i < wide.size() && wide[i] != 0; i++)
	{
		char narrow[8];
		int n = wctomb(narrow, wide[i]);
		if (n > 0)
			text.append(narrow, n);
	}
	if (chars < text.size())
		text = text.substr(0, chars);
	return text;
}

// 0x000A3D25
// Returns the NT device path a drive letter's symbolic link points to,
// e.g. 'E' -> "\Device\Harddisk0\Partition1".
HRESULT GetDriveDevicePath(char drive, std::string &devicePath)
{
	HRESULT hr = E_FAIL;
	if (isalpha(drive))
	{
		char linkName[] = "\\??\\D:";
		linkName[4] = (char)toupper(drive);

		ANSI_STRING name = { 6, 7, linkName };
		OBJECT_ATTRIBUTES attributes = { NULL, &name, OBJ_CASE_INSENSITIVE };
		HANDLE link = NULL;
		NtOpenSymbolicLinkObject(&link, &attributes);
		if (link != NULL)
		{
			// The original never frees this buffer.
			char *buffer = (char *)calloc(0x208, 1);  // TODO: verify allocator (0x0009D427)
			ANSI_STRING target = { 0, 0x208, buffer };
			ULONG length = 0;
			NtQuerySymbolicLinkObject(link, &target, &length);
			const char *result = "";
			if (target.Buffer != NULL && length != 0)
			{
				result = target.Buffer;
				hr = S_OK;
			}
			devicePath = result;
			NtClose(link);
			return hr;
		}
	}
	devicePath = "";
	return hr;
}

// 0x000A3934
// The kernel's name for the running XBE, with the extension normalized to
// ".xbe" and anything after it cut off.
std::string GetXbePath()
{
	std::string path;
	path = UX_XE_IMAGE_FILE_NAME.Buffer;
	StrReplaceAll(path, ".Xbe", ".xbe");
	StrReplaceAll(path, ".XBE", ".xbe");
	int ext = StrFind(path, ".xbe");
	if (ext != -1)
		path = StrLeft(path, ext + 4);
	return path;
}

// 0x000AC73F
// - "X:\..." stays as it is.
// - "\..." gets the home drive in front.
// - ".\..." and other relative paths are relative to the home directory;
//   each "..\" goes up one level (never above the drive root).
// - Unless running from DVD, "D:\" means the home directory too.
// The result is lowercased (the input is copied with _strdup, never freed)
// and has all '"' removed.
void ResolvePath(const char *path, std::string &out)
{
	const std::string &home = CAppSettings::s_pInstance->m_strHomePath;

	out = StrToLower(path);
	if (out.size() == 0)
		out = home;
	StrReplaceAll(out, "/", "\\");

	if (!CAppSettings::s_pInstance->m_bRunFromDVD)
	{
		StrReplaceAll(out, "D:\\", home.c_str());
		StrReplaceAll(out, "d:\\", home.c_str());
	}

	if (StrFind(out, ":\\") == -1)
	{
		std::string base;
		base = "";

		if (StrLeft(out, 1).compare("\\") == 0)
		{
			base = StrLeft(home, 2);
			base += out;
			out = base.c_str();
			base.clear();
		}
		else if (StrLeft(out, 2).compare(".\\") == 0)
		{
			base = home;
			StrReplaceAll(out, ".\\", "");
			base += out;
			out = base.c_str();
			base.clear();
		}
		else
		{
			base = home.c_str();
			if (StrFind(out, "..\\") != -1)
			{
				// Each "..\" becomes ".." (one char shorter), so the size
				// difference is the number of levels to go up.
				int before = (int)out.size();
				StrReplaceAll(out, "..\\", "..");
				int levels = before - (int)out.size();

				if (StrRight(base, 1).compare("\\") == 0)
					base = StrLeft(base, (int)base.size() - 1);
				int slash = StrReverseFind(base, "\\");
				while (levels > 0 && slash > 2)
				{
					base = StrLeft(base, slash);
					slash = StrReverseFind(base, "\\", slash - 1);
					levels--;
				}
				StrReplaceAll(out, "..", "");
			}
			StrAddBackslash(base);
			base += out;
			out = base.c_str();
			base.clear();
			StrReplaceAll(out, "\\\\", "\\");
		}
	}

	StrRemoveChar(out, '"');
}

// The characters FATX allows in names (plus '\\' and ':' for whole paths).
// 0x0025AF78, through the pointer at 0x0026BC5C
static const char *const s_fatxCharacters =
	" !#$%&'()-.@[]^_`{}~0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ\\:";

// 0x0009F0CA
BOOL IsFatxName(const char *name)
{
	if (name == NULL || strlen(name) == 0 || strlen(name) > 42)
		return FALSE;
	for (; *name != '\0'; name++)
	{
		if (*name < 0)
			continue;   // characters >= 0x80 aren't checked
		if (strchr(s_fatxCharacters, *name) == NULL)
			return FALSE;
	}
	return TRUE;
}

// 0x000A4630
// (Rewritten; not the original's.) Every part of the path after the drive is
// made fit for FATX, as XBMC4Gamers does it (CUtil::GetFatXQualifiedPath):
// accented Latin letters lose their accents (as Latin-1 or UTF-8), any other
// character FATX can't hold becomes '_', a folder name over 42 characters is
// cut to 42, and a file name over 42 is cut keeping its extension and a
// "cd1"/"part2"-style tag at its end, so the parts of a set stay apart
// (folders by the same rule: a folder must map to the same name whether
// it ends the path, as when it's made, or not, as when a file goes in it);
// trailing spaces and dots are dropped where a name was cut. The original
// fixed only the file name, deleted characters rather than replacing them
// (and all of them >= 0x80), and cut at spaces and punctuation first. TRUE
// when anything changed (out is then the new path); otherwise FALSE and out
// is left alone. Names that come out the same aren't told apart: a copy then
// meets the existing file as any copy does.
static char FoldLatin1(unsigned char c)
{
	// 0xC0..0xFF, Latin-1: the letter without its accent, or 0.
	static const char s_folded[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo_ouuuuyty";
	if (c < 0xC0)
		return 0;
	char f = s_folded[c - 0xC0];
	return f == 'x' || f == '_' ? 0 : f;
}

static std::string FatxCharacters(const std::string &part)
{
	std::string out;
	for (size_t i = 0; i < part.size(); i++)
	{
		unsigned char c = (unsigned char)part[i];
		if (c < 0x80)
		{
			out.append(1, strchr(s_fatxCharacters, c) != NULL && c != '\\' && c != ':' ? (char)c : '_');
			continue;
		}
		char folded = 0;
		// UTF-8 for U+00C0..U+00FF: C3 80..BF.
		if (c == 0xC3 && i + 1 < part.size() && ((unsigned char)part[i + 1] & 0xC0) == 0x80)
		{
			folded = FoldLatin1((unsigned char)(part[i + 1] + 0x40));
			i++;
		}
		else if (((c & 0xE0) == 0xC0 || (c & 0xF0) == 0xE0 || (c & 0xF8) == 0xF0) && i + 1 < part.size() &&
		         ((unsigned char)part[i + 1] & 0xC0) == 0x80)
		{
			// Any other UTF-8 character: one '_' for all its bytes.
			int more = (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : 3;
			while (more-- > 0 && i + 1 < part.size() && ((unsigned char)part[i + 1] & 0xC0) == 0x80)
				i++;
		}
		else
		{
			folded = FoldLatin1(c);
		}
		out.append(1, folded != 0 ? folded : '_');
	}
	return out;
}

static void TrimNameEnd(std::string &name)
{
	while (name.size() > 1 && (name[name.size() - 1] == ' ' || name[name.size() - 1] == '.'))
		name.erase(name.size() - 1);
}

// A "cd1", "part02" (after ' ', '.', '-' or '_') ending stem, or "".
static std::string PartTag(const std::string &stem)
{
	std::string lower = StrToLower(stem);
	size_t end = lower.size();
	size_t digits = end;
	while (digits > 0 && isdigit((unsigned char)lower[digits - 1]))
		digits--;
	if (digits == end)
		return "";
	const char *words[2] = {"part", "cd"};
	for (int w = 0; w < 2; w++)
	{
		size_t len = strlen(words[w]);
		if (digits < len + 1 || lower.compare(digits - len, len, words[w]) != 0)
			continue;
		char sep = lower[digits - len - 1];
		if (sep == ' ' || sep == '.' || sep == '-' || sep == '_')
			return stem.substr(digits - len - 1);
	}
	return "";
}

std::string MakeFatxName(const std::string &name, BOOL isFile)
{
	std::string clean = FatxCharacters(name);
	if (clean.size() <= 42)
		return clean;
	if (!isFile)
	{
		clean = clean.substr(0, 42);
		TrimNameEnd(clean);
		return clean;
	}
	std::string stem = clean;
	std::string extension;
	size_t dot = clean.rfind('.');
	if (dot != std::string::npos && dot > 0 && clean.size() - dot <= 10)
	{
		extension = clean.substr(dot);
		stem = clean.substr(0, dot);
	}
	std::string tag = PartTag(stem);
	size_t room = 42 - extension.size();
	if (tag.size() >= room)
		tag = "";
	stem = stem.substr(0, room - tag.size());
	TrimNameEnd(stem);
	return stem + tag + extension;
}

BOOL MakeFatxPath(const char *path, std::string &out)
{
	std::string all = path;
	std::string result;
	size_t start = 0;
	if (all.size() >= 2 && all[1] == ':')
	{
		start = all.size() >= 3 && all[2] == '\\' ? 3 : 2;
		result = all.substr(0, start);
	}
	while (start <= all.size())
	{
		size_t slash = all.find('\\', start);
		BOOL last = slash == std::string::npos;
		std::string part = all.substr(start, last ? std::string::npos : slash - start);
		if (part.size() != 0)
			result.append(part == "." || part == ".." ? part : MakeFatxName(part, TRUE));
		if (last)
			break;
		if (result.size() == 0 || result[result.size() - 1] != '\\')
			result.append("\\");
		start = slash + 1;
	}
	if (result == all)
		return FALSE;
	out = result;
	return TRUE;
}


// 0x000A439A
BOOL WriteBinaryFile(const char *path, const void *data, DWORD size)
{
	BOOL ok = FALSE;
	if (size == 0 || data == NULL)
		return FALSE;
	std::string dir = path;
	int slash = StrReverseFind(dir, "\\", -1);
	if (slash != -1)
	{
		dir = StrLeft(dir, slash);
		CreateDirectoryTree(dir.c_str());
	}
	SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
	HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file != INVALID_HANDLE_VALUE)
	{
		DWORD written = 0;
		if (WriteFile(file, data, size, &written, NULL) == TRUE)
			ok = TRUE;
		CloseHandle(file);
	}
	return ok;
}

// 0x000A3F51
BOOL CreateDirectoryTree(const char *path)
{
	if (path == NULL || strlen(path) == 0)
		return FALSE;
	std::string all = path;
	StrReplaceAll(all, "/", "\\");
	if (DirectoryExists(path))
		return TRUE;
	// (Not in the original.) With auto-rename on, folder names FATX can't
	// hold are made legal the way MakeFatxPath makes them, so the files the
	// callers then put in them (through MakeFatxPath) land in these folders.
	// Without it, such a folder couldn't be made at all.
	std::string legal;
	if (g_pSettings != NULL && g_pSettings->m_prefs.m_bAutoRename && MakeFatxPath((all + "\\x").c_str(), legal))
	{
		all = legal.substr(0, legal.size() - 2);
		path = all.c_str();
		if (DirectoryExists(path))
			return TRUE;
	}
	// Each folder on the way, from the drive root.
	std::string part = StrLeft(all, 3);
	int pos = 3;
	while (all.compare(part) != 0)
	{
		if (!DirectoryExists(part.c_str()) && !CreateDirectoryA(part.c_str(), NULL))
			return FALSE;
		pos = StrFind(all, "\\", pos + 1);
		if (pos == -1)
			break;
		part = StrLeft(all, pos);
	}
	// (path as given, slashes and all)
	if (DirectoryExists(path))
		return TRUE;
	return CreateDirectoryA(path, NULL) != 0;
}

// 0x000A4B0E
std::string GetPathDirectory(const char *path)
{
	std::string dir;
	std::string all = path;
	int dot = StrReverseFind(all, ".", -1);
	int slash = StrReverseFind(all, "\\", -1);
	if (dot >= 0 && slash >= 0 && dot <= slash && slash != (int)all.size() - 1)
		dir = all;                  // "C:\a.b\c": a folder
	else if (slash < 0 || dot < 0)
	{
		if (dot != -1)
			dir = "";               // "name.ext"
		else if (slash == -1)
			dir = all;              // "name"
		else
			dir = StrLeft(all, slash);   // "C:\Games\Halo": the last part counts as a file
	}
	else
		dir = StrLeft(all, slash);
	// No trailing backslash, except on a drive root.
	slash = StrReverseFind(dir, "\\", -1);
	if (slash > 0 && slash == (int)dir.size() - 1)
		dir = StrLeft(dir, slash);
	if (StrReverseFind(dir, ":", -1) == 1 && dir.size() == 2)
		dir.append("\\");
	return dir;
}

// 0x000A4C4C
// (Quirk: a dot anywhere in the path counts as an extension.)
std::string GetPathFileName(const char *path, BOOL requireExtension)
{
	std::string name = "";
	std::string full = path;
	int dot = StrReverseFind(full, ".", -1);
	int slash = StrReverseFind(full, "\\", -1);
	if (!requireExtension || dot >= 0)
	{
		if (slash < 0)
			name = full;
		else
			name = StrMid(full, slash + 1, (int)full.size() - (slash + 1));
	}
	return name;
}

// 0x000A5A6C
BOOL WriteUnicodeTextFile(const char *path, const wchar_t *text)
{
	if (path == NULL || strlen(path) == 0 || text == NULL || wcslen(text) == 0)
		return FALSE;
	CreateDirectoryTree(GetPathDirectory(path).c_str());
	FILE *fp = fopen(path, "w+b");
	if (fp == NULL)
		return FALSE;
	const unsigned char bom[2] = {0xFF, 0xFE};
	fwrite(bom, 1, 2, fp);
	// (UTF-16 units, as the Xbox's wchar_t.)
	for (size_t i = 0; i < wcslen(text); i++)
	{
		unsigned char unit[2] = {(unsigned char)text[i], (unsigned char)(text[i] >> 8)};
		fwrite(unit, 1, 2, fp);
	}
	const unsigned char crlf[4] = {'\r', 0, '\n', 0};
	fwrite(crlf, 2, 2, fp);
	fclose(fp);
	return TRUE;
}

// 0x000A5B45
// The text widened (each byte to a character) and written as UTF-16.
static BOOL WriteTextFileAsUnicode(const char *path, const char *text)
{
	if (text == NULL || strlen(text) == 0)
		return FALSE;
	size_t length = strlen(text);
	wchar_t *wide = new wchar_t[length + 1];
	if (wide == NULL)
		return FALSE;
	memset(wide, 0, length * sizeof(wchar_t));
	swprintf(wide, length + 1, L"%hs", text);
	BOOL ok = WriteUnicodeTextFile(path, wide);
	delete[] wide;
	return ok;
}

// 0x000A5BC6
BOOL WriteTextFile(const char *path, const char *text, BOOL unicode)
{
	if (unicode)
		return WriteTextFileAsUnicode(path, text) != FALSE;
	if (text == NULL || strlen(text) == 0)
		return FALSE;
	return WriteBinaryFile(path, text, (DWORD)strlen(text));
}

// 0x000A6D44
std::string GetPersistentDataPath(const wchar_t *name, unsigned int creation)
{
	char folder[MAX_PATH];
	if (creation == 0)
	{
		creation = OPEN_ALWAYS;
	}
	else if (creation & 1)
	{
		XDeleteSaveGame("U:\\", name);
		XDeleteSaveGame("X:\\", name);
		Sleep(5);
	}
	if (XCreateSaveGame("U:\\", name, creation, 0, folder, MAX_PATH) != 0)
	{
		if (!DirectoryExists("X:\\"))
			MountDrive('X');
		if (XCreateSaveGame("X:\\", name, creation, 0, folder, MAX_PATH) != 0)
			return std::string("");
	}
	std::string path = folder;
	if (StrRight(path, 1).compare("\\") != 0)
	{
		strcat(folder, "\\");
		path = folder;
	}
	return path;
}

// --- CFileFinder ------------------------------------------------------------

// What a file attribute filter skips: 1 folders, 2 files.
static BOOL SkipFindData(int filter, const WIN32_FIND_DATAA *data)
{
	BOOL folder = (data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
	return (filter == 1 && folder) || (filter == 2 && !folder);
}

// 0x000132DF (inlined in the constructor; this in esi)
// Quirk: the folder starts as "*C:\*".
static void InitFileFinder(CFileFinder *finder)
{
	finder->m_nFilter = 0;
	finder->m_hFind = INVALID_HANDLE_VALUE;
	finder->m_strPattern = "*";
	finder->m_strDir = "*C:\\*";
	memset(finder->m_findData, 0, sizeof(finder->m_findData));
	finder->m_pDir = NULL;
	finder->m_unknown180 = 0;
}

// 0x0001331C
CFileFinder::CFileFinder()
{
	InitFileFinder(this);
}

// 0x00013564
CFileFinder::~CFileFinder()
{
	Close();
}

// 0x0001352D
void CFileFinder::Close()
{
	CDirectory *dir = (CDirectory *)m_pDir;
	if (m_hFind != INVALID_HANDLE_VALUE && dir != NULL)
		dir->FindClose(m_hFind);
	m_hFind = INVALID_HANDLE_VALUE;
	if (dir != NULL)
		delete dir;
	m_pDir = NULL;
}

// 0x00013338
// An empty folder or pattern keeps the previous one.
const char *CFileFinder::FindFirst(const char *dir, const char *pattern)
{
	WIN32_FIND_DATAA *data = (WIN32_FIND_DATAA *)m_findData;
	memset(m_findData, 0, sizeof(m_findData));
	Close();
	if (dir != NULL && *dir != 0 && StrCmpNoCase(m_strDir, dir) != 0)
		m_strDir = dir;
	if (StrRight(m_strDir, 1) != "\\")
		m_strDir += "\\";
	if (pattern != NULL && *pattern != 0)
		m_strPattern = pattern;
	if (m_strPattern.size() == 0)
		m_strPattern = "*";

	CDirectory *directory = CreateDirObject(m_strDir.c_str());
	m_pDir = directory;
	if (directory == NULL || !directory->DirectoryExists(m_strDir.c_str()))
		return NULL;
	std::string search = m_strDir + m_strPattern;
	m_hFind = directory->FindFirst(search.c_str(), data);
	if (m_hFind == INVALID_HANDLE_VALUE)
		return NULL;
	if (SkipFindData(m_nFilter, data))
		return FindNext();
	return data->cFileName;
}

// 0x000134C3 (this in esi)
// Without an open search it starts one with the last folder and "*".
const char *CFileFinder::FindNext()
{
	WIN32_FIND_DATAA *data = (WIN32_FIND_DATAA *)m_findData;
	do
	{
		memset(m_findData, 0, sizeof(m_findData));
		if (m_hFind == INVALID_HANDLE_VALUE)
			return FindFirst(NULL, "*");
		CDirectory *directory = (CDirectory *)m_pDir;
		if (directory == NULL || !directory->FindNext(m_hFind, data))
		{
			Close();
			return NULL;
		}
	} while (SkipFindData(m_nFilter, data));
	return data->cFileName;
}

// 0x0009E11C (unused)
// A file's size in KB. Quirk: the high part of the result comes from the
// path's length (a reused variable), not the size's high dword.
DWORD GetFileSizeKB(const char *path)
{
	if (path == NULL)
		return 0;
	int length = (int)strlen(path);
	if (length == 0)
		return 0;
	HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return 0;
	LARGE_INTEGER size;
	DWORD low = 0;
	int high = 0;
	if (GetFileSizeEx(file, &size))
	{
		high = length;
		low = (DWORD)size.QuadPart;
	}
	CloseHandle(file);
	return (low >> 10) | ((DWORD)high << 22);
}
