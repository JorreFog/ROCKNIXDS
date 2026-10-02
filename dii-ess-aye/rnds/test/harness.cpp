// Host test harness for the rnds engine: the mockup's own data as the Source, a software compositor as the Backend.
//   harness <assets> <mockup dir> <scale> <outprefix> <script>
// script: commands separated by ';'  e.g. "home 7; wait 2400; shot home-psx"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "rnds/RndsUI.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

using namespace rnds;

struct G { std::string title, genre, year; int plays; std::string last, time; int a, b; std::string slug; };
struct S { std::string id, name, maker; unsigned accent; int games, played; std::string time; int resume; std::vector<G> list; };
static std::vector<S> SYS = {
#include "mockdata.inc"
};

static Bitmap stbDecode(const std::string& path)
{
	Bitmap b;
	int w, h, n;
	unsigned char* p = stbi_load(path.c_str(), &w, &h, &n, 4);
	if (!p) return b;
	b.w = w; b.h = h; b.px.assign(p, p + (size_t)w * h * 4);
	stbi_image_free(p);
	return b;
}

struct CpuTex : Tex { Bitmap bmp; bool smooth; };

struct CpuBackend : Backend
{
	int W, H;
	std::vector<float> scr[2];
	int cur = 0;
	struct Clip { float x0, y0, x1, y1; };
	std::vector<Clip> clips;
	CpuBackend(int w, int h) : W(w), H(h) { for (auto& s : scr) s.assign((size_t)W * H * 3, 0); }
	TexPtr upload(const Bitmap& b, bool smooth) override { auto t = std::make_shared<CpuTex>(); t->bmp = b; t->w = b.w; t->h = b.h; t->alpha = b.alpha; t->smooth = smooth; return t; }
	void beginScreen(int s) override { cur = s; clips.clear(); clips.push_back({ 0, 0, (float)W, (float)H }); }
	void endScreen() override {}
	void pushClip(float x, float y, float w, float h) override
	{
		Clip c = clips.back();
		clips.push_back({ std::max(c.x0, x), std::max(c.y0, y), std::min(c.x1, x + w), std::min(c.y1, y + h) });
	}
	void popClip() override { clips.pop_back(); }
	void blend(int x, int y, float r, float g, float b, float a)
	{
		float* p = &scr[cur][((size_t)y * W + x) * 3];
		p[0] = r * a + p[0] * (1 - a); p[1] = g * a + p[1] * (1 - a); p[2] = b * a + p[2] * (1 - a);
	}
	void fill(float x, float y, float w, float h, const Color& c) override
	{
		Clip cl = clips.back();
		int x0 = (int)std::max(cl.x0, x), y0 = (int)std::max(cl.y0, y), x1 = (int)std::ceil(std::min(cl.x1, x + w)), y1 = (int)std::ceil(std::min(cl.y1, y + h));
		for (int yy = y0; yy < y1; yy++) for (int xx = x0; xx < x1; xx++) blend(xx, yy, c.r, c.g, c.b, c.a);
	}
	void sample(const CpuTex* t, float u, float v, float out[4])
	{
		const Bitmap& b = t->bmp;
		auto px = [&](int x, int y, float o[4])
		{
			x = std::min(std::max(x, 0), b.w - 1); y = std::min(std::max(y, 0), b.h - 1);
			if (b.alpha) { o[0] = o[1] = o[2] = 1; o[3] = b.px[(size_t)y * b.w + x] / 255.0f; }
			else { const uint8_t* q = &b.px[((size_t)y * b.w + x) * 4]; o[0] = q[0] / 255.0f; o[1] = q[1] / 255.0f; o[2] = q[2] / 255.0f; o[3] = q[3] / 255.0f; }
		};
		if (!t->smooth) { px((int)std::floor(u), (int)std::floor(v), out); return; }
		float fx = u - 0.5f, fy = v - 0.5f;
		int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
		float tx = fx - x0, ty = fy - y0;
		float a[4], bb[4], c[4], d[4];
		px(x0, y0, a); px(x0 + 1, y0, bb); px(x0, y0 + 1, c); px(x0 + 1, y0 + 1, d);
		for (int k = 0; k < 4; k++) out[k] = (a[k] * (1 - tx) + bb[k] * tx) * (1 - ty) + (c[k] * (1 - tx) + d[k] * tx) * ty;
	}
	void draw(const TexPtr& tp, float x, float y, float w, float h, const Color& tint) override
	{
		auto t = (const CpuTex*)tp.get();
		if (!t || w <= 0 || h <= 0) return;
		Clip cl = clips.back();
		int x0 = (int)std::max(cl.x0, std::floor(x)), y0 = (int)std::max(cl.y0, std::floor(y));
		int x1 = (int)std::min(cl.x1, std::ceil(x + w)), y1 = (int)std::min(cl.y1, std::ceil(y + h));
		bool exact = std::fabs(w - t->w) < 1e-3 && std::fabs(h - t->h) < 1e-3 && x == std::floor(x) && y == std::floor(y);
		for (int yy = y0; yy < y1; yy++)
			for (int xx = x0; xx < x1; xx++)
			{
				float cx = xx + 0.5f, cy = yy + 0.5f;
				if (cx < x || cy < y || cx >= x + w || cy >= y + h) continue;
				float s[4];
				if (exact)
				{
					int ix = xx - (int)x, iy = yy - (int)y;
					const Bitmap& b = t->bmp;
					if (b.alpha) { s[0] = s[1] = s[2] = 1; s[3] = b.px[(size_t)iy * b.w + ix] / 255.0f; }
					else { const uint8_t* q = &b.px[((size_t)iy * b.w + ix) * 4]; s[0] = q[0] / 255.0f; s[1] = q[1] / 255.0f; s[2] = q[2] / 255.0f; s[3] = q[3] / 255.0f; }
				}
				else sample(t, (cx - x) * t->w / w, (cy - y) * t->h / h, s);
				float a = s[3] * tint.a;
				if (a <= 0) continue;
				blend(xx, yy, s[0] * tint.r, s[1] * tint.g, s[2] * tint.b, a);
			}
	}
	void save(int s, const std::string& path)
	{
		std::string tmp = path + ".ppm";
		FILE* f = fopen(tmp.c_str(), "wb");
		fprintf(f, "P6\n%d %d\n255\n", W, H);
		for (size_t i = 0; i < (size_t)W * H * 3; i++) { float v = scr[s][i]; fputc((int)std::lround(std::min(1.0f, std::max(0.0f, v)) * 255), f); }
		fclose(f);
		std::string cmd = "convert " + tmp + " " + path + " && rm " + tmp;
		if (system(cmd.c_str()) != 0) fprintf(stderr, "convert failed\n");
	}
};

struct MockSource : Source
{
	std::string art, icons;
	int lib = 0;
	std::vector<bool> sysFav;
	std::vector<std::string> log;
	MockSource(const std::string& mock) : art(mock + "/art/"), icons(mock + "/icons/"), sysFav(SYS.size(), false) {}
	int systemCount() override { return (int)SYS.size(); }
	SysInfo system(int i) override
	{
		const S& s = SYS[i];
		SysInfo r;
		r.id = s.id; r.name = s.name; r.maker = s.maker; r.accent = Color::hex(s.accent);
		r.icon = icons + s.id + ".png"; r.games = s.games; r.played = s.played; r.time = s.time; r.fav = sysFav[i];
		const G& g = s.list[s.resume < (int)s.list.size() ? s.resume : 0];
		r.hasResume = true; r.resumeEyebrow = "Last played"; r.resumeTitle = g.title; r.resumeBox = art + g.slug + "-box.jpg";
		return r;
	}
	int gameCount() override { return (int)SYS[lib].list.size(); }
	GameInfo game(int i) override
	{
		const G& g = SYS[lib].list[i];
		GameInfo r;
		r.title = g.title;
		r.sub = g.genre + " · " + g.year;
		r.line = g.genre + " · " + g.year + ". " + (g.plays ? std::to_string(g.plays) + " plays" : std::string("Never played")) + " · " + g.last + " · " + g.time + ".";
		r.box = art + g.slug + "-box.jpg"; r.snap = art + g.slug + "-snap.jpg";
		r.achA = g.a; r.achB = g.b; r.achLabel = std::to_string(g.a) + " / " + std::to_string(g.b) + " achievements";
		return r;
	}
	std::string clockText() override { return "01:01 PM"; }
	std::string dateText() override { return "10/02"; }
	float battery() override { return 0.75f; }
	bool online() override { return true; }
	void selectSystem(int i) override { log.push_back("selectSystem " + std::to_string(i)); }
	void selectGame(int i) override { log.push_back("selectGame " + std::to_string(i)); }
	int openLibrary(int sys) override { lib = sys; log.push_back("openLibrary " + std::to_string(sys)); return SYS[sys].resume; }
	void backToHome() override { log.push_back("backToHome"); }
	void launch(int g) override { log.push_back("launch " + std::to_string(g)); }
	void toggleSystemFav(int s) override { sysFav[s] = !sysFav[s]; }
	void toggleGameFav(int) override {}
	int resumeGame(int sys) override { lib = sys; return SYS[sys].resume; }
};

int main(int argc, char** argv)
{
	if (argc < 6) { fprintf(stderr, "usage: harness assets mockup scale outprefix script\n"); return 1; }
	setDecoder(stbDecode);
	float scale = atof(argv[3]);
	MockSource src(argv[2]);
	CpuBackend be((int)std::lround(640 * scale), (int)std::lround(480 * scale));
	UI ui(&src, &be, argv[1], scale);
	double now = 100000;
	std::string script = argv[5], prefix = argv[4];
	std::stringstream ss(script);
	std::string cmd;
	auto step = [&]() { ui.update(now); ui.finishWork(); ui.update(now); };
	while (std::getline(ss, cmd, ';'))
	{
		std::stringstream cs(cmd);
		std::string op; cs >> op;
		ui.update(now);
		if (op == "home") { int s; cs >> s; ui.setHome(s); }
		else if (op == "lib") { int s, g; cs >> s >> g; src.lib = s; ui.setLibrary(s, g); }
		else if (op == "launch") { int s, g; cs >> s >> g; src.lib = s; ui.setLaunch(s, g, false); }
		else if (op == "wait") { double ms; cs >> ms; now += ms; }
		else if (op == "press") { std::string b; cs >> b; UI::Button m = b == "left" ? UI::LEFT : b == "right" ? UI::RIGHT : b == "a" ? UI::A : b == "b" ? UI::B : b == "x" ? UI::X : b == "y" ? UI::Y : b == "l" ? UI::L : UI::R; ui.update(now); ui.press(m); }
		else if (op == "shot")
		{
			std::string name; cs >> name;
			step();
			ui.render();
			step();	// images that finished loading
			ui.render();
			be.save(0, prefix + name + "-top.png");
			be.save(1, prefix + name + "-bot.png");
			fprintf(stderr, "shot %s (wake %d)\n", name.c_str(), ui.nextWake());
		}
	}
	for (auto& l : src.log) fprintf(stderr, "  %s\n", l.c_str());
	return 0;
}
