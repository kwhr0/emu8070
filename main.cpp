#include "main.h"
#include "INS8070.h"
#include <unistd.h>
#include <SDL2/SDL.h>
#include <string>
#include <queue>

static uint8_t m[0x10000];
static int keyboard, mhz;
static bool exit_flag;
static FILE *fi;
static std::queue<int16_t> sndData;
static INS8070 cpu;

void file_seek(int sector) {
	if (!fi) return;
	fseek(fi, sector << 9, SEEK_SET);
}

int file_getc() {
	if (!fi) return 0;
	return getc(fi);
}

int keyboard_getc() {
	return keyboard;
}

void sound_put(int d) {
	sndData.push(d);
}

bool sound_fill() {
	return sndData.size() >= 2048;
}

void emu_exit() {
	exit_flag = true;
}

static SDL_AudioCVT audioCVT;
static SDL_AudioSpec audiospec;

static void audio_callback(void *userdata, uint8_t *stream, int dstlen) {
	cpu.Execute(1000000LL * mhz * (dstlen >> 1) / audiospec.freq);
	audioCVT.len = dstlen / audioCVT.len_ratio + 2;
	int srclen = audioCVT.len >> 1;
	int16_t buf[srclen * audioCVT.len_mult];
	audioCVT.buf = (uint8_t *)buf;
	int i = 0;
	while (i < srclen && sndData.size()) {
		buf[i++] = sndData.front();
		sndData.pop();
	}
	if (i < srclen) printf("mute: %d samples\n", srclen - i);
	while (i < srclen) buf[i++] = 0;
	if (!SDL_ConvertAudio(&audioCVT)) memcpy(stream, buf, dstlen);
	else memset(stream, 0, dstlen);
}

static void audio_init() {
	SDL_Init(SDL_INIT_AUDIO);
	SDL_AudioSpec spec;
	SDL_zero(spec);
	spec.freq = 48000; // 32000だとノイズがひどい
	spec.format = AUDIO_S16SYS;
	spec.channels = 1;
	spec.samples = 512;
	spec.callback = audio_callback;
	int id = SDL_OpenAudioDevice(nullptr, 0, &spec, &audiospec, 0);
	if (id <= 0) exit(1);
	SDL_PauseAudioDevice(id, 0);
	SDL_BuildAudioCVT(&audioCVT, audiospec.format, audiospec.channels, 32000, audiospec.format, audiospec.channels, audiospec.freq);
}

static void exitfunc() {
	fclose(fi);
	SDL_Quit();
}

int main(int argc, char *argv[]) {
	int opt, sndEnable = 0;
	mhz = 1000;
	while ((opt = getopt(argc, argv, "c:s")) != -1) {
		switch (opt) {
			case 'c':
				sscanf(optarg, "%d", &mhz);
				break;
			case 's':
				sndEnable = 1;
				break;
		}
	}
	if (argc <= optind) {
		fprintf(stderr, "Usage: emu8070 [-s] [-c <clock freq. [MHz]> (default: %d)] <a.out file>\n", mhz);
		return 1;
	}
	char *path = argv[optind];
	fi = fopen(path, "rb");
	if (!fi) {
		fprintf(stderr, "Cannot open %s\n", path);
		return 1;
	}
	int c, i = 0;
	while ((c = getc(fi)) != EOF) m[i++] = c;
	fclose(fi);
	fi = fopen((std::string(getenv("HOME")) + "/fatmovie.dmg").c_str(), "rb");
	if (!fi) fprintf(stderr, "Warning: disk image cannot open.\n");
	cpu.SetMemoryPtr(m);
	cpu.Reset();
	atexit(exitfunc);
	SDL_Init(SDL_INIT_VIDEO);
	if (sndEnable) audio_init();
	constexpr int width = 128, height = 64, mag = 4;
	SDL_Window *window = SDL_CreateWindow("emu8070", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, mag * width, mag * height, 0);
	if (!window) exit(1);
	SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!renderer) exit(1);
	SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(SDL_SWSURFACE, width, height, 1, SDL_PIXELFORMAT_XBGR8888);
	if (!surface) exit(1);
	while (!exit_flag) {
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			static SDL_Keycode sym;
			switch (e.type) {
				case SDL_KEYDOWN:
					if (e.key.repeat) break;
					 switch (sym = e.key.keysym.sym) {
					 case SDLK_RIGHT: keyboard = 28; break;
					 case SDLK_LEFT:  keyboard = 29; break;
					 case SDLK_UP:    keyboard = 30; break;
					 case SDLK_DOWN:  keyboard = 31; break;
					 default: keyboard = sym & SDLK_SCANCODE_MASK ? 0 : sym; break;
					 }
					break;
				case SDL_KEYUP:
					if (e.key.keysym.sym == sym) keyboard = 0;
					break;
				case SDL_QUIT:
					exit_flag = true;
					break;
			}
		}
		cpu.IRQ();
		if (!sndEnable)
			cpu.Execute(1000000 / 60 * mhz);
		// guest VRAM: 0x8000-0xdfff RGB 3bytes x128 x64
		uint8_t *sp = &m[0x8000];
		uint32_t *dp = (uint32_t *)surface->pixels;
		for (int i = 0; i < 128 * 64; i++) {
			*dp++ = sp[0] | sp[1] << 8 | sp[2] << 16;
			sp += 3;
		}
		SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
		SDL_RenderCopy(renderer, texture, NULL, NULL);
		SDL_RenderPresent(renderer);
		SDL_DestroyTexture(texture);
	}
}
