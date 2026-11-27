// Unit tests of the dashboard's pure logic: the string helpers, number
// formatting, Base64 and MD5 (as the original behaves), the screen scaling
// (the original's and the Windows build's even scaling), and the frame-time
// helpers. Built by the host Makefile (build/unit_test) and, on Windows, by
// win32/UnitTests.vcxproj, so it runs in Visual Studio too.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "../src/common/Base64.h"
#include "../src/common/FileUtil.h"
#include "../src/common/FrameTime.h"
#include "../src/common/StrUtil.h"
#include "../src/render/Draw.h"
#include "../src/settings/AppSettings.h"
#include "../src/system/SystemInfo.h"
#include "../src/thirdparty/Md5.h"

#ifdef main
#undef main   // (the PC build's headers rename the dashboard's main)
#endif

static int s_checks;
static int s_failures;

// A failure as compilers print errors, so Visual Studio's error list (the
// UnitTests project runs this after building) and editors go to the line.
#ifdef _MSC_VER
#define FAILED_AT "%s(%d): error: "
#else
#define FAILED_AT "%s:%d: "
#endif

#define CHECK(cond)                                                               \
	do                                                                            \
	{                                                                             \
		s_checks++;                                                               \
		if (!(cond))                                                              \
		{                                                                         \
			printf(FAILED_AT "CHECK(%s) failed\n", __FILE__, __LINE__, #cond);       \
			s_failures++;                                                         \
		}                                                                         \
	} while (0)

static void CheckStr(const char *what, const std::string &got, const char *expected, int line)
{
	s_checks++;
	if (got != expected)
	{
		printf(FAILED_AT "%s = \"%s\", expected \"%s\"\n", __FILE__, line, what, got.c_str(), expected);
		s_failures++;
	}
}
#define CHECK_STR(expr, expected) CheckStr(#expr, (expr), (expected), __LINE__)

static void CheckWide(const char *what, const wchar_t *got, const std::wstring &expected, int line)
{
	s_checks++;
	if (expected != got)
	{
		printf(FAILED_AT "%s = \"%s\", expected \"%s\"\n", __FILE__, line, what, StrFromWide(got).c_str(),
		       StrFromWide(expected).c_str());
		s_failures++;
	}
}

static void CheckNear(const char *what, double got, double expected, double tolerance, int line)
{
	s_checks++;
	if (!(fabs(got - expected) <= tolerance))
	{
		printf(FAILED_AT "%s = %f, expected %f\n", __FILE__, line, what, got, expected);
		s_failures++;
	}
}
#define CHECK_NEAR(expr, expected, tolerance) CheckNear(#expr, (double)(expr), (expected), (tolerance), __LINE__)

// ---- The original's string helpers -------------------------------------------

static void TestStrings()
{
	std::string s = "UnleashX";
	CHECK_STR(StrLeft(s, 3), "Unl");
	CHECK_STR(StrLeft(s, 100), "UnleashX");
	CHECK_STR(StrLeft(s, -1), "");
	CHECK_STR(StrRight(s, 1), "X");
	CHECK_STR(StrRight(s, 100), "UnleashX");
	CHECK_STR(StrRight(s, 0), "");
	CHECK_STR(StrMid(s, 2, 3), "lea");
	CHECK_STR(StrMid(s, 5, 100), "shX");
	CHECK_STR(StrMid(s, -3, 2), "Un");
	CHECK_STR(StrMid(s, 8, 1), "");
	CHECK_STR(StrMid(s, 20, 1), "");
	CHECK_STR(StrMidFrom(s, 7), "X");

	std::string t = "  \t padded \r\n";
	CHECK_STR(StrTrim(t), "padded");

	std::string path = "E:\\\\Apps\\\\UnleashX\\\\";
	CHECK(StrReplaceAll(path, "\\\\", "\\") == 3);
	CHECK_STR(path, "E:\\Apps\\UnleashX\\");
	std::string none = "abc";
	CHECK(StrReplaceAll(none, "", "x") == 0);
	CHECK(StrReplaceAll(none, "z", "x") == 0);
	std::string grow = "aaa";
	CHECK(StrReplaceAll(grow, "a", "aa") == 3);
	CHECK_STR(grow, "aaaaaa");

	CHECK(StrCmpNoCase("Config.XML", "config.xml") == 0);
	CHECK(StrCmpNoCase("abc", "abd") < 0);
	CHECK(StrFind(s, "eash") == 3);
	CHECK(StrFind(s, "zzz") == -1);
	CHECK(StrReverseFind("a\\b\\c", "\\") == 3);
	CHECK(StrReverseFind("abc", "\\") == -1);
	CHECK(StrFindChar(s, 'X') == 7);

	std::string dir = "E:\\Apps";
	StrAddBackslash(dir);
	CHECK_STR(dir, "E:\\Apps\\");
	StrAddBackslash(dir);
	CHECK_STR(dir, "E:\\Apps\\");
	CHECK_STR(StrToLower("MiXeD 123"), "mixed 123");

	// The settings' yes/no words, case-insensitive.
	CHECK(IsTrueString("Yes") && IsTrueString("TRUE") && IsTrueString("1"));
	CHECK(!IsTrueString("No") && !IsTrueString("") && !IsTrueString(NULL));
	CHECK(IsFalseString("no") && IsFalseString("False") && IsFalseString("0"));
	CHECK(!IsFalseString("yes") && !IsFalseString(NULL));

	// UTF-8 to wide. Latin-1 text converts properly...
	wchar_t wide[32];
	StrUtf8ToWide("Caf\xC3\xA9", wide, 31);
	CHECK(wcscmp(wide, L"Caf\x00E9") == 0);
	CHECK_STR(StrFromWide(wide), "Caf\xE9");   // (narrowed to Latin-1)
	// ...but a character past Latin-1 sends the whole string through the
	// original's hand-made fallback, which folds byte pairs: the euro sign's
	// three bytes become two characters, while the e-acute still comes out.
	StrUtf8ToWide("Caf\xC3\xA9 \xE2\x82\xAC", wide, 31);
	CHECK(wcscmp(wide, L"Caf\x00E9 \x00E1\x00AC") == 0);
	// The other way is real UTF-8.
	char utf8[32];
	StrWideToUtf8(L"Caf\x00E9 \x20AC", utf8, 31);
	CHECK_STR(std::string(utf8), "Caf\xC3\xA9 \xE2\x82\xAC");

	BYTE bytes[4] = {0, 0, 0, 0};
	CHECK(HexToBytes("0aFF10", bytes) == 0);
	CHECK(bytes[0] == 0x0A && bytes[1] == 0xFF && bytes[2] == 0x10);
	CHECK(HexToBytes("abc", bytes) == 1);   // an odd length is refused

	std::string formatted;
	CHECK_STR(StrFormat(formatted, "%s:%04d", "id", 42), "id:0042");
}

// ---- Number formatting (the free space, sizes, block counts) ------------------

static void TestNumbers()
{
	wchar_t comma = GetThousandsSeparator();
	wchar_t point = GetDecimalSeparator();
	wchar_t text[64];

	FormatIntW(text, 0);
	CheckWide("FormatIntW(0)", text, L"0", __LINE__);
	FormatIntW(text, 999);
	CheckWide("FormatIntW(999)", text, L"999", __LINE__);
	FormatIntW(text, 1000);
	CheckWide("FormatIntW(1000)", text, std::wstring(L"1") + comma + L"000", __LINE__);
	FormatIntW(text, 1234567);
	CheckWide("FormatIntW(1234567)", text, std::wstring(L"1") + comma + L"234" + comma + L"567", __LINE__);
	FormatIntW(text, -1234);
	CheckWide("FormatIntW(-1234)", text, std::wstring(L"-1") + comma + L"234", __LINE__);

	// As the menu shows free space: "59,362.42 MB free".
	FormatDoubleW(text, 2, TRUE, 59362.42);
	CheckWide("FormatDoubleW(59362.42)", text, std::wstring(L"59") + comma + L"362" + point + L"42", __LINE__);
	FormatDoubleW(text, 2, FALSE, 1234.5);
	CheckWide("FormatDoubleW(1234.5, no grouping)", text, std::wstring(L"1234") + point + L"50", __LINE__);
	FormatDoubleW(text, 2, TRUE, 0.5);
	CheckWide("FormatDoubleW(0.5)", text, std::wstring(L"0") + point + L"50", __LINE__);
	FormatDoubleW(text, 0, TRUE, 1234567.0);
	CheckWide("FormatDoubleW(1234567, 0 decimals)", text,
	          std::wstring(L"1") + comma + L"234" + comma + L"567", __LINE__);
}

// ---- Base64 and MD5 -------------------------------------------------------------

static std::string DigestHex(const unsigned char digest[16])
{
	char hex[33];
	for (int i = 0; i < 16; i++)
		sprintf(hex + i * 2, "%02x", digest[i]);
	return std::string(hex, 32);
}

static std::string Md5Hex(const char *text)
{
	MD5_CTX context;
	unsigned char digest[16];
	MD5Init(&context);
	MD5Update(&context, (const unsigned char *)text, (unsigned int)strlen(text));
	MD5Final(digest, &context);
	return DigestHex(digest);
}

static void TestEncodings()
{
	// RFC 4648's vectors.
	const char *plain[] = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};
	const char *coded[] = {"", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy"};
	for (int i = 0; i < 7; i++)
	{
		std::string out;
		Base64Encode(plain[i], out);
		CheckStr("Base64Encode", out, coded[i], __LINE__);
		std::string back;
		Base64Decode(coded[i], back);
		CheckStr("Base64Decode", back, plain[i], __LINE__);
	}

	// RFC 1321's vectors.
	CHECK_STR(Md5Hex(""), "d41d8cd98f00b204e9800998ecf8427e");
	CHECK_STR(Md5Hex("abc"), "900150983cd24fb0d6963f7d28e17f72");
	CHECK_STR(Md5Hex("message digest"), "f96b697d7cb7938d525a2f31aaf161d0");
	CHECK_STR(Md5Hex("abcdefghijklmnopqrstuvwxyz"), "c3fcd3d76192e4007dfb496cca67e13b");
	CHECK_STR(Md5Hex("12345678901234567890123456789012345678901234567890123456789012345678901234567890"),
	          "57edf4a22be3c955ac49da2e2107b67a");

	// In pieces, as a file is read, the same as all at once.
	const char *text = "The quick brown fox jumps over the lazy dog";
	MD5_CTX context;
	unsigned char digest[16];
	MD5Init(&context);
	for (size_t at = 0, length = strlen(text); at < length; at += 5)
		MD5Update(&context, (const unsigned char *)text + at, (unsigned int)(length - at < 5 ? length - at : 5));
	MD5Final(digest, &context);
	CHECK_STR(DigestHex(digest), "9e107d9d372bb6826bd81d3542a419d6");
}

// ---- Screen scaling -------------------------------------------------------------

static void SetScreen(DWORD x, DWORD y, DWORD width, DWORD height, int skinWidth, int skinHeight, float aspect)
{
	g_pSettings->m_viewport.X = x;
	g_pSettings->m_viewport.Y = y;
	g_pSettings->m_viewport.Width = width;
	g_pSettings->m_viewport.Height = height;
	g_pSettings->m_nSkinWidth = skinWidth;
	g_pSettings->m_nSkinHeight = skinHeight;
	g_fPictureAspect = aspect;
}

static void TestScaling()
{
	float x, y, w, h;

	// The original: the skin's size on screen is left as it is.
	SetScreen(0, 0, 640, 480, 640, 480, 0.0f);
	x = 100.0f; y = 50.0f; w = 200.0f; h = 20.0f;
	ScaleToScreen(&x, &w, &y, &h, TRUE);
	CHECK(x == 100.0f && y == 50.0f && w == 200.0f && h == 20.0f);

	// 720p: each axis by its own scale (2 and 1.5), as the Xbox does.
	SetScreen(0, 0, 1280, 720, 640, 480, 0.0f);
	x = 100.0f; y = 50.0f; w = 200.0f; h = 20.0f;
	ScaleToScreen(&x, &w, &y, &h, TRUE);
	CHECK_NEAR(x, 200.0, 0.001);
	CHECK_NEAR(y, 75.0, 0.001);
	CHECK_NEAR(w, 400.0, 0.001);
	CHECK_NEAR(h, 30.0, 0.001);

	// The screen adjustment's offset is added to positions, not sizes.
	SetScreen(16, 8, 640 - 32, 480 - 16, 640, 480, 0.0f);
	x = 0.0f; y = 0.0f; w = 640.0f; h = 0.0f;
	ScaleToScreen(&x, &w, &y, &h, TRUE);
	CHECK_NEAR(x, 16.0, 0.001);
	CHECK_NEAR(y, 8.0, 0.001);
	CHECK_NEAR(w, 608.0, 0.001);
	CHECK(h == 0.0f);   // zero sizes stay zero

	// Whole numbers: zeros stay zero.
	SetScreen(0, 0, 1280, 960, 640, 480, 0.0f);
	DWORD ix = 10, iy = 0, iw = 0, ih = 5;
	ScaleToScreenInt(&ix, &iy, &iw, &ih);
	CHECK(ix == 20 && iy == 0 && iw == 0 && ih == 10);

	// Rectangles: positions round down, sizes up.
	SetScreen(0, 0, 960, 720, 640, 480, 0.0f);
	int rect[4] = {3, 3, 3, 3};
	ScaleRectToScreen(rect);
	CHECK(rect[0] == 4 && rect[1] == 4 && rect[2] == 5 && rect[3] == 5);

	// (Windows build) A window wider than 4:3: one scale (the height's), the
	// layout centered with bars at the sides.
	SetScreen(0, 0, 1920, 720, 640, 480, 1920.0f / 720.0f);
	x = 0.0f; y = 0.0f; w = 640.0f; h = 480.0f;
	ScaleToScreen(&x, &w, &y, &h, FALSE);
	CHECK_NEAR(x, 480.0, 0.001);   // (1920 - 640 * 1.5) / 2
	CHECK_NEAR(y, 0.0, 0.001);
	CHECK_NEAR(w, 960.0, 0.001);
	CHECK_NEAR(h, 720.0, 0.001);
	// A full-layout image scales the same way (texts on it stay on it).
	x = 0.0f; w = 640.0f;
	ScaleToScreen(&x, &w, NULL, NULL, FALSE);
	CHECK_NEAR(x, 480.0, 0.001);
	CHECK_NEAR(w, 960.0, 0.001);

	// A window taller than 4:3: the width's scale, bars above and below.
	SetScreen(0, 0, 640, 960, 640, 480, 640.0f / 960.0f);
	x = 10.0f; y = 0.0f; w = 100.0f; h = 100.0f;
	ScaleToScreen(&x, &w, &y, &h, FALSE);
	CHECK_NEAR(x, 10.0, 0.001);
	CHECK_NEAR(y, 240.0, 0.001);   // (960 - 480) / 2
	CHECK_NEAR(w, 100.0, 0.001);
	CHECK_NEAR(h, 100.0, 0.001);

	// (Xbox) 720x480 shown at 16:9: wide pixels. The layout is 480 tall
	// and, as seen, 640 wide: 640 * 720 / 853.3 = 540 pixels, centered.
	SetScreen(0, 0, 720, 480, 640, 480, 16.0f / 9.0f);
	g_fPixelShape = (16.0f / 9.0f) * 480.0f / 720.0f;
	x = 0.0f; y = 0.0f; w = 640.0f; h = 480.0f;
	ScaleToScreen(&x, &w, &y, &h, FALSE);
	CHECK_NEAR(x, 90.0, 0.01);
	CHECK_NEAR(w, 540.0, 0.01);
	CHECK_NEAR(y, 0.0, 0.01);
	CHECK_NEAR(h, 480.0, 0.01);
	// (1280x720 and 1920x1080 have square pixels: as the Windows build.)
	g_fPixelShape = 1.0f;

	// Text scale factors (a size of 1) come out even.
	SetScreen(0, 0, 2560, 720, 640, 480, 2560.0f / 720.0f);
	float scaleX = 1.0f, scaleY = 1.0f;
	ScaleToScreen(NULL, &scaleX, NULL, &scaleY, TRUE);
	CHECK_NEAR(scaleX, 1.5, 0.001);
	CHECK_NEAR(scaleY, 1.5, 0.001);

	SetScreen(0, 0, 640, 480, 640, 480, 0.0f);
}

// ---- Frame-rate independent animation ----------------------------------------------

static void TestFrameTime()
{
	SetFrameTime(1.0f / 60.0f);
	CHECK_NEAR(FrameScale(), 1.0, 0.0001);
	SetFrameTime(1.0f / 120.0f);
	CHECK_NEAR(FrameScale(), 0.5, 0.0001);
	CHECK_NEAR(FrameStep(0.008f), 0.004, 0.00001);
	// A long frame counts as 4 at most; nonsense counts as none.
	SetFrameTime(2.0f);
	CHECK_NEAR(FrameScale(), 4.0, 0.0001);
	SetFrameTime(-1.0f);
	CHECK(FrameScale() == 0.0f);
	SetFrameTime((float)sqrt(-1.0));
	CHECK(FrameScale() == 0.0f);

	// FrameTicks: a second at 144 fps is 60 ticks, the fraction carried.
	float carry = 0.0f;
	int ticks = 0;
	SetFrameTime(1.0f / 144.0f);
	for (int i = 0; i < 144; i++)
		ticks += FrameTicks(&carry);
	CHECK(ticks == 59 || ticks == 60);   // (float rounding at the last frame)

	// Easing and decay come out as two 60 fps frames at half the rate.
	SetFrameTime(2.0f / 60.0f);
	CHECK_NEAR(FrameRate(0.1f), 1.0 - 0.9 * 0.9, 0.0001);
	CHECK_NEAR(FrameFactor(0.9f), 0.81, 0.0001);
	CHECK(FrameRate(0.0f) == 0.0f && FrameRate(1.0f) == 1.0f);

	// A step "every n ms" took the next 60 Hz frame after n ms.
	CHECK_NEAR(FrameInterval(0), 1000.0 / 60.0, 0.01);
	CHECK_NEAR(FrameInterval(16), 1000.0 / 60.0, 0.01);
	CHECK_NEAR(FrameInterval(17), 2000.0 / 60.0, 0.01);
	CHECK_NEAR(FrameInterval(20), 2000.0 / 60.0, 0.01);
	CHECK_NEAR(FrameInterval(100), 7000.0 / 60.0, 0.01);

	// FrameDue keeps that pace at any frame rate: a 20 ms marquee steps 30
	// times a second at 60 fps and at 144 fps.
	for (int rate = 0; rate < 2; rate++)
	{
		double frame = rate == 0 ? 1000.0 / 60.0 : 1000.0 / 144.0;
		unsigned long tick = 0;
		int steps = 0;
		for (double now = frame; now <= 1000.0; now += frame)
			if (FrameDue(&tick, (unsigned long)now, 20))
				steps++;
		CHECK(steps >= 29 && steps <= 31);
	}
	// After a stall it starts again from now rather than catching up.
	unsigned long tick = 0;
	CHECK(FrameDue(&tick, 5000, 20));
	CHECK(!FrameDue(&tick, 5001, 20));
	// The smooth version: a 20 ms pace is half a pixel per 60 Hz frame.
	SetFrameTime(1.0f / 60.0f);
	CHECK_NEAR(FrameSteps(20), 0.5, 0.0001);
	SetFrameTime(1.0f / 120.0f);
	CHECK_NEAR(FrameSteps(20), 0.25, 0.0001);
	SetFrameTime(1.0f / 60.0f);
}

// ---- Names too long (or odd) for FATX -------------------------------------------

static void TestFatxNames()
{
	std::string out = "untouched";
	// Legal paths are left alone.
	CHECK(!MakeFatxPath("E:\\Games\\Halo\\default.xbe", out));
	CHECK_STR(out, "untouched");
	CHECK(!MakeFatxPath("E:\\", out));

	// A long file name: 42 characters, the extension kept.
	std::string longName = "An Extremely Long Game Name That Goes On And On Forever.iso";
	CHECK(MakeFatxPath(("E:\\Games\\" + longName).c_str(), out));
	std::string name = out.substr(9);
	CHECK(name.size() >= 40 && name.size() <= 42);   // (a space at the cut is dropped)
	CHECK_STR(name.substr(name.size() - 4), ".iso");
	CHECK_STR(out.substr(0, 9), "E:\\Games\\");

	// A disc tag at the end survives the cut, so a set's parts stay apart.
	std::string disc1 = MakeFatxName("Some Really Long Collection Of Games Edition cd1.iso", TRUE);
	std::string disc2 = MakeFatxName("Some Really Long Collection Of Games Edition cd2.iso", TRUE);
	CHECK(disc1.size() <= 42 && disc2.size() <= 42);
	CHECK_STR(disc1.substr(disc1.size() - 8), " cd1.iso");
	CHECK(disc1 != disc2);
	std::string part = MakeFatxName("A Very Long Archive Name For Some Big Download.part03.rar", TRUE);
	CHECK_STR(part.substr(part.size() - 11), ".part03.rar");

	// Characters FATX can't hold become '_'; accents are dropped (Latin-1 or
	// UTF-8); any other UTF-8 character is one '_'.
	CHECK_STR(MakeFatxName("a+b=c,d;e.txt", TRUE), "a_b_c_d_e.txt");
	CHECK_STR(MakeFatxName("Caf\xC3\xA9 Cr\xC3\xA8me.mp3", TRUE), "Cafe Creme.mp3");
	CHECK_STR(MakeFatxName("Caf\xE9.mp3", TRUE), "Cafe.mp3");
	CHECK_STR(MakeFatxName("\xE6\x97\xA5\xE6\x9C\xAC.txt", TRUE), "__.txt");

	// Folders too, and the same way whether they end the path or not, so a
	// folder made under a long name is found again when a file goes in it.
	std::string longFolder = "This Folder Name Is Much Too Long For The Xbox FATX";
	std::string folderAlone, fileInside;
	CHECK(MakeFatxPath(("F:\\" + longFolder).c_str(), folderAlone));
	CHECK(MakeFatxPath(("F:\\" + longFolder + "\\save.dat").c_str(), fileInside));
	CHECK_STR(fileInside, (folderAlone + "\\save.dat").c_str());
	CHECK(folderAlone.size() - 3 <= 42);

	// Cut names lose trailing spaces and dots.
	std::string spaced = MakeFatxName(std::string(41, 'x') + "   more text after.txt", TRUE);
	CHECK(spaced.size() <= 42 && spaced.find(" .") == std::string::npos);
}

int main()
{
	g_pSettings = CAppSettings::GetInstance();
	TestStrings();
	TestNumbers();
	TestEncodings();
	TestScaling();
	TestFrameTime();
	TestFatxNames();
	printf("unit_test: %d checks, %d failed\n", s_checks, s_failures);
	return s_failures == 0 ? 0 : 1;
}
