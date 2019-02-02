#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/syscall.h>
#include <debugnet.h>

#include <kernel.h>
#include <systemservice.h>
#include <orbis2d.h>
#include <orbisPad.h>
#include <sys/fcntl.h>
#include <ps4link.h>
#include <orbisGl.h>

#define MAX_LOG_LINE 50000
#define TEXT_LEN 2000

#include "jailbreak.h"
#include "font_draw.h"
#include "main.h"

OrbisGlobalConf *defaultConfig;

int64_t flipArg=0;
int homebrewRun = 1;

int isGLinit = 0;

static EGLDisplay s_display = EGL_NO_DISPLAY;
static EGLSurface s_surface = EGL_NO_SURFACE;
static EGLContext s_context = EGL_NO_CONTEXT;

ScePthread compilationThread;
int compilationStart = 0;

int VertexNbr, FragmentNbr = 0;

char VertexFiles[1000][256];
char FragmentFiles[1000][256];

// Log Drawing system
int currentTextScale = 2;
int nbrLine = 0;
int currentPos = 0;
int maxPosInScreen;
int follow = 1;

struct Line log[MAX_LOG_LINE];

void ClearLog() {
	for (int i = 0; i < 50000; i++) {
		memset(log[i].text, 0, 500);
	}
	nbrLine = 0;
}

void WriteLogVA(uint32_t fontColor, uint32_t backColor, char* text, va_list argptr) {
	if (nbrLine >= MAX_LOG_LINE) {
		ClearLog();
	}

	memset(log[nbrLine].text, 0, TEXT_LEN);
	if (strlen(text) < TEXT_LEN) {
		char finalText[TEXT_LEN];

	    vsnprintf(finalText, TEXT_LEN, text, argptr);

		strncpy(log[nbrLine].text, finalText, TEXT_LEN);
	}

	log[nbrLine].fontColor = fontColor;
	log[nbrLine].backColor = backColor;
	nbrLine++;

	if (follow) {
		if (nbrLine > maxPosInScreen) {
			currentPos++;
		}
	}
}

void WriteLogColor(uint32_t fontColor, uint32_t backColor, char* text, ...) {
    va_list argptr;
    va_start(argptr, text);
	WriteLogVA(fontColor, backColor, text, argptr);
	va_end(argptr);
}

void WriteLog(char* text, ...) {
    va_list argptr;
    va_start(argptr, text);
	WriteLogVA(getRGB(255, 255, 255), getRGB(0, 0, 0), text, argptr);
	va_end(argptr);
}

void DrawLog() {
	for (int i = 0; i < maxPosInScreen; i++) {
		int textPos = currentPos + i;

		if (textPos > 50000)
			continue;

		if (log[textPos].text[0] == 0)
			continue;

		font_setFontColor(log[textPos].fontColor);
		font_setBackFontColor(log[textPos].backColor);
		font_drawString(0, i * 10 * currentTextScale, currentTextScale, log[textPos].text);
	}
}

void LogScale(int scale) {
	currentTextScale = scale;
	maxPosInScreen = ATTR_HEIGHT / (10 * currentTextScale);
}

// End of Drawing log system

int own_ceil(float num) {
    int inum = (int)num;
    if (num == (float)inum) {
        return inum;
    }
    return inum + 1;
}

// Load all shaders file
int GetShadersList(char *dir) 
{
	int type = 0;
	int res = 0;
	int dfd = ps4LinkDopen(dir);
	if (dfd<0)
		return dfd;

	for (int i = 0; i < VertexNbr; i++)
		memset(VertexFiles[i], 0, 256);

	for (int i = 0; i < FragmentNbr; i++)
		memset(FragmentFiles[i], 0, 256);

	VertexNbr = 0;
	FragmentNbr = 0;

	do
	{
		OrbisDirEntry *dir;
		dir=malloc(sizeof(OrbisDirEntry));
		res=ps4LinkDread(dfd,dir);
		if(res>0)
		{
			if(dir->type!=DT_DIR)
			{
				char name[255];
				char* ext = strrchr(dir->name, '.')+1;
				if (strncmp("vert", ext, 4) == 0 && VertexNbr < 1000) {
					//int name_size = (ext-0x1) - dir->name;
					strncpy(VertexFiles[VertexNbr], dir->name, strlen(dir->name));
					VertexNbr++;
				} else if (strncmp("frag", ext, 4) == 0 && FragmentNbr < 1000) {
					//int name_size = (ext-0x1) - dir->name;
					strncpy(FragmentFiles[FragmentNbr], dir->name, strlen(dir->name));
					FragmentNbr++;
				}
			}			
		}
			
	}
	while(res>0);	

	ps4LinkDclose(dfd);

	WriteLog("%i vertex was found.", VertexNbr);
	WriteLog("%i fragment was found.", FragmentNbr);
	return res;
}

// Write File
void WriteFile(char* file_path, void* data, int size) {
	int fd = orbisOpen(file_path, O_CREAT|O_WRONLY, 0777);
	if (fd > 0) {
		if (orbisWrite(fd, data, size) < 0) {
			WriteLog("Unable to write the file ! (%s) (fd: %i)", file_path, fd);
		}

		orbisClose(fd);
	} else {
		WriteLog("Unable to create the file ! (%s)", file_path);
	}
}

// Read File
void* ReadFile(char* file_path, int* size) {
	int fd = orbisOpen(file_path, O_RDONLY, 0777);

	if (fd > 0) {
		*size = orbisLseek(fd, 0, SEEK_END);
		orbisLseek(fd, 0, SEEK_SET);

		char* data = malloc(*size+1);

		if (orbisRead(fd, data, *size) < 0) {
			WriteLog("Unable to read the file ! (%s) (fd: %i)", file_path, fd);
		}
		orbisClose(fd);

		data[*size] = 0;
		return data;
	} else {
		WriteLog("Unable to open the file ! (%s)", file_path);
	}

	return NULL;
}

// Draw shader error log on the screen
void ShaderLog(GLuint shader_id, char* shaderName)
{
	int ret;
	if(shader_id>0)
	{
		GLint log_length;
		glGetShaderiv(shader_id,GL_INFO_LOG_LENGTH,&log_length);
		if(log_length)
		{
			GLchar log_buffer[log_length];
			glGetShaderInfoLog(shader_id,log_length,NULL,log_buffer);
			ret=glGetError();
			if(ret)
			{
				WriteLog("glGetShaderInfoLog failed: 0x%08X",ret);
				return;
			}

			WriteLog("shader compilation with log");

    		char save_path[500];
  			sprintf(save_path, "host0:compiled/%s.log", shaderName);
			WriteFile(save_path, log_buffer, strlen(log_buffer));
			WriteLog("Shader log saved to %s", save_path);

		}
		else
		{
			WriteLog("shader compilation no log");
		}
	}
}

// Compile shaders
GLuint CompileShader(const GLenum type,const GLchar* source, char* shaderName) 
{
	GLuint shader_id;
	GLint compile_status;
	int ret;
	
	shader_id=glCreateShader(type);
	
	if(!shader_id)
	{
		ret=glGetError();
		WriteLog("glCreateShader failed: 0x%08X",ret);
		return 0;
	}
	glShaderSource(shader_id,1,(const GLchar **)&source,NULL);
	ret=glGetError();
	if(ret)
	{
		WriteLog("glShaderSource failed: 0x%08X",ret);
	}
	glCompileShader(shader_id);
	ret=glGetError();
	if(ret)
	{	
		WriteLog("glCompileShader failed: 0x%08X",ret);
		return 0;
	}
	glGetShaderiv(shader_id,GL_COMPILE_STATUS,&compile_status);
	ret=glGetError();
	if(ret)
	{
		WriteLog("glGetShaderiv failed: 0x%08X",ret);
		return 0;
	}
	if (!compile_status)
	{
			
		WriteLog("shader compilation failed");
		ShaderLog(shader_id, shaderName);
		return 0;
	}

	WriteLog("shader compilation shader_id=%d done",shader_id);
	ShaderLog(shader_id, shaderName);
	return shader_id;
}

// Dumping the program
void DumpShader(GLuint shaderID, char* shaderName) {
	uint8_t* shader_binary = NULL;
    GLenum format;
    GLsizei shader_size;
    int error = 0x502;
    int ret = 0;

    WriteLog("Dumping %s ...", shaderName);

    char save_path[500];
  	sprintf(save_path, "host0:compiled/%s.sb", shaderName);

  	GLsizei bufferSize = 0;
  	void* shaderBinary = malloc(50000);

	debugNetPrintf(ERROR,"[ShaderDumper] Starting shader dump ...");

  	while (error == 0x502) {
  		bufferSize += 50000;
  		shaderBinary = realloc(shaderBinary, bufferSize);
    	memset(shaderBinary, 0, bufferSize);

  		glPigletGetShaderBinarySCE(shaderID, bufferSize, &shader_size, &format, shaderBinary);
  		error = eglGetError();
  	}

    WriteLog("Writing %s (%i bytes) ...", shaderName, shader_size);

  	if (error != 0x3000) {
	    WriteLog("Error during dump: 0x%08x",error);
  	} else {
		WriteFile(save_path, shaderBinary, shader_size);
	    WriteLog("Shader %s (%u) was dumped ! (%i bytes)", shaderName, shaderID, shader_size);
	}

    free(shaderBinary);
    return;
}

// Dumping all shaders
int DumpAllShaders() {
	char shaderPath[500];
	int shaderSize = 0;
	int i = 0;

	int errorNbr = 0;

	for (i = 0; i < VertexNbr; i++) {
		shaderSize = 0;
		sprintf(shaderPath, "host0:shaders/%s", VertexFiles[i]);

		char* source = (char *)ReadFile(shaderPath, &shaderSize);
		if(source == NULL)
		{
		    WriteLog("Can't open shader at %s !", shaderPath);
		    errorNbr++;
			continue;
		}

		GLuint ShaderID = CompileShader(GL_VERTEX_SHADER, source, VertexFiles[i]);
		if(ShaderID == 0)
		{
		    WriteLog("Can't compile vertex shader at %s !", shaderPath);
		    free(source);
		    errorNbr++;
			continue;
		}

		free(source);

		DumpShader(ShaderID, VertexFiles[i]);
	}

	for (i = 0; i < FragmentNbr; i++) {
		shaderSize = 0;
		sprintf(shaderPath, "host0:shaders/%s", FragmentFiles[i]);

		char* source = (char *)ReadFile(shaderPath, &shaderSize);
		if(source == NULL)
		{
		    WriteLog("Can't open shader at %s !", shaderPath);
		    errorNbr++;
			continue;
		}

		GLuint ShaderID = CompileShader(GL_FRAGMENT_SHADER, source, FragmentFiles[i]);
		if(ShaderID == 0)
		{
		    WriteLog("Can't compile fragment shader at %s !", shaderPath);
		    free(source);
		    errorNbr++;
			continue;
		}

		free(source);

		DumpShader(ShaderID, FragmentFiles[i]);
	}

	return errorNbr;
}

// Compilation Function Thread
int CompilationFunc() {
	int error = DumpAllShaders();

	if (!error) {
		WriteLog("Compilation(s) done !");
	} else {
		WriteLogColor(getRGB(255, 255, 255), getRGB(255, 0, 0), "Compilation end with %i error(s)", error);
	}

	compilationStart = 0;
	return 0;
}

// Initialize GL
static int initGL(unsigned int width, unsigned int height) 
{
	ScePglConfig pgl_config;
	SceWindow render_window = { 0, width, height };
	EGLConfig config = NULL;
	EGLint num_configs;

	EGLint attribs[] = {
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
		EGL_DEPTH_SIZE, 0,
		EGL_STENCIL_SIZE, 0,
		EGL_SAMPLE_BUFFERS, 0,
		EGL_SAMPLES, 0,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_NONE,
	};

	EGLint ctx_attribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE,
	};

	EGLint window_attribs[] = {
		EGL_RENDER_BUFFER, EGL_BACK_BUFFER,
		EGL_NONE,
	};

	int major, minor;
	int ret;

	memset(&pgl_config, 0, sizeof(pgl_config));
	{
		pgl_config.size = sizeof(pgl_config);
		pgl_config.flags = SCE_PGL_FLAGS_USE_COMPOSITE_EXT | SCE_PGL_FLAGS_USE_FLEXIBLE_MEMORY | 0x60;
		pgl_config.processOrder = 1;
		pgl_config.systemSharedMemorySize = 0x200000;
		pgl_config.videoSharedMemorySize = 0x2400000;
		pgl_config.maxMappedFlexibleMemory = 0xAA00000;
		pgl_config.drawCommandBufferSize = 0xC0000;
		pgl_config.lcueResourceBufferSize = 0x10000;
		pgl_config.dbgPosCmd_0x40 = 1920;
		pgl_config.dbgPosCmd_0x44 = 1080;
		pgl_config.dbgPosCmd_0x48 = 0;
		pgl_config.dbgPosCmd_0x4C = 0;
		pgl_config.unk_0x5C = 2;
	}

	if (!scePigletSetConfigurationVSH(&pgl_config)) {
		WriteLog("scePigletSetConfigurationVSH failed.");
		goto err;
	}

	s_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (s_display == EGL_NO_DISPLAY) {
		WriteLog("eglGetDisplay failed.");
		goto err;
	}

	if (!eglInitialize(s_display, &major, &minor)) {
		ret = eglGetError();
		WriteLog("eglInitialize failed: 0x%08X", ret);
		goto err;
	}
	WriteLog("EGL version major:%d, minor:%d", major, minor);

	if (!eglBindAPI(EGL_OPENGL_ES_API)) {
		ret = eglGetError();
		WriteLog("eglBindAPI failed: 0x%08X", ret);
		goto err;
	}

	if (!eglSwapInterval(s_display, 0)) {
		ret = eglGetError();
		WriteLog("eglSwapInterval failed: 0x%08X", ret);
		goto err;
	}

	if (!eglChooseConfig(s_display, attribs, &config, 1, &num_configs)) {
		ret = eglGetError();
		WriteLog("eglChooseConfig failed: 0x%08X", ret);
		goto err;
	}
	if (num_configs != 1) {
		WriteLog("No available configuration found.");
		goto err;
	}

	s_surface = eglCreateWindowSurface(s_display, config, &render_window, window_attribs);
	if (s_surface == EGL_NO_SURFACE) {
		ret = eglGetError();
		WriteLog("eglCreateWindowSurface failed: 0x%08X", ret);
		goto err;
	}

	s_context = eglCreateContext(s_display, config, EGL_NO_CONTEXT, ctx_attribs);
	if (s_context == EGL_NO_CONTEXT) {
		ret = eglGetError();
		WriteLog("eglCreateContext failed: 0x%08X", ret);
		goto err;
	}

	if (!eglMakeCurrent(s_display, s_surface, s_surface, s_context)) {
		ret = eglGetError();
		WriteLog("eglMakeCurrent failed: 0x%08X", ret);
		goto err;
	}

	WriteLog("GL_VERSION: %s", glGetString(GL_VERSION));
	WriteLog("GL_RENDERER: %s", glGetString(GL_RENDERER));

	return 1;

err:
	return 0;
}

int init(char* intptr_arg) {
	int ret;

	sceSystemServiceHideSplashScreen();

	uintptr_t intptr=0;
	sscanf(intptr_arg, "%p", &intptr);
	defaultConfig = (OrbisGlobalConf *)intptr;
	ret=ps4LinkInitWithConf(defaultConfig->confLink);
	if(ret <= 0)
	{
		debugNetPrintf(DEBUG,"[PKGLoader][Error] ps4LinkInitWithConf : 0x%08x", ret);
		ps4LinkFinish();
		return 0;
	}

	debugNetPrintf(DEBUG,"[ShaderDumper] Initializing app ...");

	ret = orbisFileInit();
	if (ret <= 0)
	{
		debugNetPrintf(DEBUG,"[ShaderDumper][Error] orbisFileInit : 0x%08x", ret);
		return 0;
	}

	ret = orbisPadInitWithConf(defaultConfig->confPad);;
	if (ret <= 0)
	{
		debugNetPrintf(DEBUG,"[ShaderDumper][Error] orbisPadInitWithConf : 0x%08x", ret);
		return 0;
	}

	ret = orbis2dInitWithConf(defaultConfig->conf);
	if (ret <= 0)
	{
		debugNetPrintf(DEBUG,"[ShaderDumper][Error] orbis2dInitWithConf : 0x%08x", ret);
		return 0;
	}

	// Init folder
	int dfd = orbisDopen("host0:shaders");
	if (dfd < 0) {
		orbisMkdir("host0:shaders");
	} else {
		orbisDclose(dfd);
	}

	dfd = orbisDopen("host0:compiled");
	if (dfd < 0) {
		orbisMkdir("host0:compiled");
	} else {
		orbisDclose(dfd);
	}

	LogScale(2);
	orbis2dSetBgColor(getRGB(0, 0, 0));

	WriteLogColor(getRGB(255, 255, 255), getRGB(0, 0, 200), "ShaderDumper 1.0");
	WriteLog("By OrbisDev & OpenOrbis Team");
	WriteLog("Special thanks to @flatz, @idc, @bigboss, @theorywrong, @masterzorag");
	WriteLog("And all people involved in the scene !");
	WriteLog("");

	isGLinit = initGL(ATTR_WIDTH, ATTR_HEIGHT);
	if (isGLinit) {
		WriteLogColor(getRGB(255, 255, 255), getRGB(0, 170, 0), "OpenGL Initialized !");
		WriteLog("");

		GetShadersList("host0:shaders");

		WriteLog("");
		WriteLog("Press X for compile");
		WriteLog("Press [] for reload available file");

	} else {
		WriteLogColor(getRGB(255, 255, 255), getRGB(170, 0, 0), "OpenGL Not initialized !");
		WriteLog("Press O for exit the app.");
	}

	return 1;
}

int finish() {
	debugNetPrintf(ERROR,"[ShaderDumper] Finish !");
	orbisGlFinish();
	orbis2dFinish();
	orbisPadFinish();
	orbisFileFinish();
	ps4LinkFinish();
	exit(0);
}

int update() {
	orbisPadUpdate();

	if(orbisPadGetButtonPressed(ORBISPAD_UP))
	{
		follow = 0;

		if (currentPos-1 >= 0)
			currentPos--; 
	}

	if(orbisPadGetButtonPressed(ORBISPAD_DOWN))
	{
		follow = 0;

		int calcPos = nbrLine - maxPosInScreen;
		if (calcPos < 0)
			calcPos = 0;

		if (nbrLine > maxPosInScreen && currentPos+1 < nbrLine && currentPos+1 <= calcPos) {
			currentPos++;
		}
	}

	if (orbisPadGetButtonPressed(ORBISPAD_LEFT)) {
		int new_scale = currentTextScale - 1;
		if (new_scale > 0)
			LogScale(new_scale);
	}

	if (orbisPadGetButtonPressed(ORBISPAD_RIGHT)) {
		int new_scale = currentTextScale + 1;
		if (new_scale < 10)
			LogScale(new_scale);
	}


	if(orbisPadGetButtonPressed(ORBISPAD_TRIANGLE))
	{
		if (follow) {
			follow = 0;
		} else {
			int calcPos = nbrLine - maxPosInScreen;
			if (calcPos < 0)
				calcPos = 0;

			currentPos = calcPos;
			follow = 1;
		}
	}

	if (orbisPadGetButtonPressed(ORBISPAD_SQUARE))
	{
		if (compilationStart) {
			WriteLogColor(getRGB(255, 255, 255), getRGB(255, 0, 0), "Compilation in progress !");
			return 0;
		}

		GetShadersList("host0:shaders");
	}

	if(orbisPadGetButtonPressed(ORBISPAD_CROSS))
	{
		debugNetPrintf(DEBUG,"X pressed");

		if (!compilationStart) {
			compilationStart++;
			scePthreadCreate(&compilationThread, NULL, CompilationFunc, NULL, "CompilationThread");
		}
	}

	if(orbisPadGetButtonPressed(ORBISPAD_CIRCLE))
	{
		if (compilationStart) {
			WriteLogColor(getRGB(255, 255, 255), getRGB(255, 0, 0), "Compilation in progress !");
			return 0;
		}

		homebrewRun = 0;
		WriteLog("Application exit !");
	}

	return 0;
}

int render() {
	orbis2dStartDrawing();
	orbis2dClearBuffer(0);

	DrawLog();

	if (!follow) {
		font_setFontColor(getRGB(255, 255, 255));
		font_setBackFontColor(getRGB(0, 175, 0));

		char followMessage[50] = "Press Triangle for follow Log";

		font_drawString(ATTR_WIDTH - (8 * currentTextScale * strlen(followMessage)) - 50, ATTR_HEIGHT - (10 * currentTextScale) - 15, currentTextScale, followMessage);
	}

	// Draw position cursor
	orbis2dDrawRectColor(ATTR_WIDTH - 10, 5, 5, ATTR_HEIGHT - 5, getRGB(255, 255, 255));

	int sizeLine = own_ceil( (ATTR_HEIGHT - 10) / nbrLine );
	int sizeCursor = sizeLine * maxPosInScreen;

	int cursorPos = 5 + ( currentPos * sizeLine );


	orbis2dDrawRectColor(ATTR_WIDTH - 10, 5, cursorPos, sizeCursor, getRGB(0, 0, 255));

	orbis2dFinishDrawing(flipArg);
	orbis2dSwapBuffers();
	flipArg++;

	return 0;
}

int main(int argc, char *argv[]) 
{
	if (!init(argv[1])) {
		debugNetPrintf(ERROR,"[ShaderDumper][FATAL] Cannot succefully initialize homebrew !");
		goto end;
	}

	while(homebrewRun)
	{
		update();
		render();
	}

	end:
	finish();

	return 0;
}
