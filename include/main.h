#ifndef MAINH
#define MAINH

#define WIDTH 1280
#define HEIGHT 720

typedef struct OrbisGlobalConf
{
	Orbis2dConfig *conf;
	OrbisPadConfig *confPad;
	void *confAudio; /* Unused */
	void *confKeyboard; /* Unused */
	ps4LinkConfiguration *confLink;
	int orbisLinkFlag;
}OrbisGlobalConf;

struct Line {
	uint32_t fontColor;
	uint32_t backColor;
	char text[TEXT_LEN];
};

#endif