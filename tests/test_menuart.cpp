// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include "mods/menuart.h"

static std::vector<unsigned char> Bmp(int w, int h, unsigned char (*pixel)(int x, int y, int c))
{
	const size_t stride = (static_cast<size_t>(w) * 3 + 3) & ~static_cast<size_t>(3);
	std::vector<unsigned char> b(54 + stride * h, 0);
	auto put = [&](size_t at, unsigned v) {
		for (int i = 0; i < 4; i++) b[at + i] = static_cast<unsigned char>(v >> (8 * i));
	};
	b[0] = 'B';
	b[1] = 'M';
	put(2, static_cast<unsigned>(b.size()));
	put(10, 54);
	put(14, 40);
	put(18, static_cast<unsigned>(w));
	put(22, static_cast<unsigned>(h));
	b[26] = 1;
	b[28] = 24;
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			for (int c = 0; c < 3; c++) b[54 + stride * y + x * 3 + c] = pixel(x, y, c);
		}
	}
	return b;
}

static int Size(const std::vector<unsigned char> &b, int at)
{
	return b[at] | b[at + 1] << 8 | b[at + 2] << 16 | b[at + 3] << 24;
}

static void Shrink()
{
	const std::vector<unsigned char> src = Bmp(4, 2, [](int x, int, int) { return static_cast<unsigned char>(x * 40); });
	std::vector<unsigned char> out;
	CHECK(ScaleBmpToCover(src, 2, 1, out));
	CHECK_EQ(Size(out, 18), 2);
	CHECK_EQ(Size(out, 22), 1);
	CHECK_EQ(out[54], 20);
	CHECK_EQ(out[57], 100);
	CHECK(ScaleBmpToCover(src, 1, 1, out));
	CHECK_EQ(out[54], 60);
	CHECK(!ScaleBmpToCover(src, 4, 2, out));
	CHECK(!ScaleBmpToCover(src, 0, 1, out));
	std::vector<unsigned char> bad = src;
	bad[28] = 32;
	CHECK(!ScaleBmpToCover(bad, 2, 1, out));
	CHECK(!ScaleBmpToCover(std::vector<unsigned char>(src.begin(), src.begin() + 60), 2, 1, out));
}

static void Cover()
{
	const std::vector<unsigned char> tall = Bmp(2, 4, [](int, int y, int) { return static_cast<unsigned char>(y * 40); });
	std::vector<unsigned char> out;
	CHECK(ScaleBmpToCover(tall, 1, 1, out));
	CHECK_EQ(out[54], 60);
	const std::vector<unsigned char> art = Bmp(1920, 1080, [](int x, int y, int c) { return static_cast<unsigned char>((x + y + c) & 0xff); });
	CHECK(ScaleBmpToCover(art, 1704, 960, out));
	CHECK_EQ(Size(out, 18), 1704);
	CHECK_EQ(Size(out, 22), 960);
	CHECK_EQ(out.size(), size_t(54 + 1704 * 3 * 960));
	CHECK(ScaleBmpToCover(art, 1280, 1024, out));
	CHECK_EQ(Size(out, 18), 1280);
}

static void Enlarge()
{
	const std::vector<unsigned char> src = Bmp(4, 2, [](int x, int, int) { return static_cast<unsigned char>(x * 40); });
	std::vector<unsigned char> out;
	CHECK(ScaleBmpToCover(src, 8, 4, out));
	CHECK_EQ(Size(out, 18), 8);
	CHECK_EQ(Size(out, 22), 4);
	CHECK_EQ(out[54], 0);
	CHECK_EQ(out[57], 10);
	CHECK_EQ(out[60], 30);
	CHECK_EQ(out[63], 50);
	CHECK_EQ(out[54 + 7 * 3], 120);
	const std::vector<unsigned char> art = Bmp(1920, 1080, [](int x, int y, int c) { return static_cast<unsigned char>((x + y + c) & 0xff); });
	CHECK(ScaleBmpToCover(art, 3440, 1440, out));
	CHECK_EQ(Size(out, 18), 3440);
	CHECK_EQ(Size(out, 22), 1440);
	CHECK_EQ(out.size(), size_t(54 + 3440 * 3 * 1440));
	CHECK(ScaleBmpToCover(art, 2560, 1440, out));
	CHECK_EQ(Size(out, 18), 2560);
}

static std::vector<unsigned char> Bar16(int w, int h, unsigned (*pixel)(int x))
{
	const size_t stride = (static_cast<size_t>(w) * 2 + 3) & ~static_cast<size_t>(3);
	std::vector<unsigned char> b(54 + stride * h, 0);
	auto put = [&](size_t at, unsigned v) {
		for (int i = 0; i < 4; i++) b[at + i] = static_cast<unsigned char>(v >> (8 * i));
	};
	b[0] = 'B';
	b[1] = 'M';
	put(2, static_cast<unsigned>(b.size()));
	put(10, 54);
	put(14, 40);
	put(18, static_cast<unsigned>(w));
	put(22, static_cast<unsigned>(h));
	b[26] = 1;
	b[28] = 16;
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			const unsigned v = pixel(x);
			b[54 + stride * y + x * 2] = static_cast<unsigned char>(v);
			b[54 + stride * y + x * 2 + 1] = static_cast<unsigned char>(v >> 8);
		}
	}
	return b;
}

static unsigned BarPixel(int x)
{
	return x < 50 ? 0x7fffu : static_cast<unsigned>((x % 100) * 300 + 7);
}

static void Bar()
{
	const std::vector<unsigned char> src = Bar16(1024, 4, BarPixel);
	std::vector<unsigned char> out;
	CHECK(ExtendBarBmp(src, 1500, out));
	CHECK_EQ(Size(out, 18), 1500);
	CHECK_EQ(Size(out, 22), 4);
	CHECK_EQ(out.size(), size_t(54 + 3000 * 4));
	CHECK_EQ(static_cast<size_t>(Size(out, 2)), out.size());
	bool same = true;
	for (int y = 0; y < 4; y++) {
		for (int x = 0; x < 1500; x++) {
			const unsigned v = out[54 + 3000 * y + x * 2] | out[54 + 3000 * y + x * 2 + 1] << 8;
			same = same && v == BarPixel(x);
		}
	}
	CHECK(same);
	CHECK(!ExtendBarBmp(src, 1024, out));
	CHECK(!ExtendBarBmp(Bar16(512, 4, BarPixel), 1500, out));
	CHECK(!ExtendBarBmp(Bar16(1024, 400, BarPixel), 1500, out));

	std::string ini = "[Background]\r\nImage = %Art%/Cmdbar/CmdBarIRome.bmp\r\nRectWH = 0, 0, 2048, 54\r\nHAlign = 0, 0, 1\r\n\r\n"
					  "[BackgroundFrame]\r\nRectWH = 882, 0, 1024, 54\r\n[Wide]\nRectWH=0,0,2048,80\n[Expr]\nRectWH = #0#, 0, 2048, 80\n";
	CHECK(WidenBarRects(ini, 2048, 4096));
	CHECK(ini.find("RectWH = 0, 0, 4096, 54\r\nHAlign") != std::string::npos);
	CHECK(ini.find("RectWH = 882, 0, 1024, 54\r\n") != std::string::npos);
	CHECK(ini.find("[Wide]\nRectWH = 0, 0, 4096, 80\n") != std::string::npos);
	CHECK(ini.find("RectWH = #0#, 0, 2048, 80") != std::string::npos);
	CHECK(!WidenBarRects(ini, 2048, 4096));
}

static void Ini()
{
	const std::string back = "[MenuBack]\r\nRectWH = 0, 0, %Larghezza%, %Altezza%\r\n\r\n[Back]\r\nType = Button\r\n"
							 "Image = UI/Interface/MenuMod.bmp\r\nRectWH = 0,0,%larghezza%, %ALTEZZA%\r\nHAlign = 1, 0, 1\r\n\r\n"
							 "[Beta]\r\nImage = UI/Beta Version.bmp, 0, 0\r\nRectWH = 10, 740, 200, 28\r\n";
	std::string text = back;
	std::vector<std::string> asked;
	CHECK(FitFullFrameImages(text, [&](const std::string &image) {
		asked.push_back(image);
		return std::string("cache/back.bmp");
	}));
	CHECK_EQ(asked.size(), size_t(1));
	if (!asked.empty()) CHECK_EQ(asked[0], "UI/Interface/MenuMod.bmp");
	CHECK(text.find("Image = cache/back.bmp, 0, 0\r\n") != std::string::npos);
	CHECK(text.find("Image = UI/Beta Version.bmp, 0, 0") != std::string::npos);
	CHECK(text.find("[MenuBack]\r\nRectWH = 0, 0, %Larghezza%, %Altezza%\r\n") == 0);

	text = back;
	CHECK(!FitFullFrameImages(text, [](const std::string &) { return std::string(); }));
	CHECK_EQ(text, back);

	const std::string menu = "[Background]\nType = Button\nRectWH = -276, -153, 1920, 1080\nImage = Currentlang/MenuBackgroind.bmp, 30, 10\n";
	text = menu;
	CHECK(!FitFullFrameImages(text, [](const std::string &) { return std::string("x.bmp"); }));
	CHECK_EQ(text, menu);
}

static void Layout()
{
	const std::string fullMenu = "[MainMenu]\nRectWH = 0, 0, %Larghezza%, %Altezza%\n\n[MenuSash]\nRectWH = 725, 0, %Larghezza%, %Altezza%\n";
	const std::string tmpl1920 = "[Params]\nLarghezza = 1920\nButtonSpacing = 40\nButtonSpacing2 = %ButtonSpacing% + %ButtonSpacing% +40 ;more\nButtonXStart=842\nButtonYStart=250\nButton0Rect=#%ButtonXStart%+0#, #%ButtonYStart%+78#, 247, 38\nButton11Rect=#%ButtonXStart%-0#, #%ButtonYStart%+78 + (%ButtonSpacing2%) * 4#, 247, 38\nScreenRight = %Larghezza%-30\nScreenBottom = %Altezza%-30\n";
	const std::string tmpl1366 = "[Params]\nButtonXStart=564\nButtonYStart=10\nButton16Rect=#%ButtonXStart%-0#, #%ButtonYStart% + 640#, 247, 38\nScreenRight = %Larghezza%-30\n";
	CHECK(MenuLayoutIsFixed(fullMenu, tmpl1920));
	CHECK(MenuLayoutIsFixed("[MainMenu]\r\nTemplate = %TmplIni%, StdDlg\r\nRectWH = 0, 0, %Larghezza%, %Altezza%\r\n", tmpl1920));
	CHECK(!MenuLayoutIsFixed("[MainMenu]\nStyle = TRANSPARENT\n\n[Back]\nRectWH = 0, 0, %Larghezza%, %Altezza%\n", tmpl1920));
	CHECK(!MenuLayoutIsFixed("", tmpl1920));
	CHECK(!MenuLayoutIsFixed(fullMenu, tmpl1366));
	CHECK(MenuLayoutIsFixed(fullMenu + "[Version]\nRectWH = #920-59#, 858, 200, 25\n", tmpl1366));
	CHECK(!MenuLayoutIsFixed(fullMenu + "[Version]\nRectWH = #1230-30#, #768-20#, 80, 25\n", tmpl1366));
	CHECK(!MenuLayoutIsFixed(fullMenu + "[Caption]\nRectWH = -276, -153, 1920, 1080\n", tmpl1366));
	CHECK(!MenuLayoutIsFixed(fullMenu, "[Params]\nButtonXStart = %ScreenRight% - 272\nButton0Rect = #%ButtonXStart%#, 900, 1, 1\n"));
	CHECK(!MenuLayoutIsFixed(fullMenu + "[Version]\nRectWH = #%ScreenRight%-175#, #%ScreenBottom%-20#, 175, 25\n", tmpl1920));
	CHECK(!MenuLayoutIsFixed(fullMenu + "[Logo]\nRectWH = 90, #%Altezza% - 156#, 556, 66\n", tmpl1920));
	CHECK(MenuLayoutIsFixed(fullMenu + ";RectWH = #%ScreenRight%-175#, 0, 1, 1\n", tmpl1920));
	CHECK(!MenuLayoutIsFixed(fullMenu + "[Tips]\nRectWH = #left(Box) + 2000#, 0, 1, 1\n", tmpl1366));
	CHECK(!MenuLayoutIsFixed("[MainMenu]\nRectWH = 0, 0, 1366, 768\n", tmpl1920));
}

int main()
{
	Shrink();
	Cover();
	Enlarge();
	Bar();
	Ini();
	Layout();
	return test::Summary("menuart");
}
