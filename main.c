#define _PSP2_KERNEL_CLIB_H_ // Prevent incompatibility between libk and sceLibc
#include <vitasdkkern.h>
#include <taihen.h>
#include <libk/stdio.h>
#include <libk/stdarg.h>
#include <libk/string.h>
#include <psp2/rtc.h>

#include "renderer.h"

#define ALIGN(x, a) (((x) + ((a) - 1)) & ~((a) - 1))
 
#define SCREEN_PITCH  1024
#define SCREEN_W       960
#define SCREEN_H       544

#define HOOKS_NUM   1      // Hooked functions num

static uint8_t current_hook = 0;
static SceUID hooks[HOOKS_NUM];
static tai_hook_ref_t refs[HOOKS_NUM];
static SceUID fb_uid = -1;
SceDisplayFrameBuf fb;
static int retry = 2;
static int head = 0;
static void* addr = NULL;
static SceUID fd;
static char ppath[128];
static int pass[4] = {0, 0, 0, 0};
static int i = 0;
static int guess = 0;

int ksceDisplayWaitVblankStart(void);
int kscePowerRequestColdReset(void);
int ksceDisplayGetPrimaryHead(void);
int ksceDisplaySetFrameBufInternal(int head, int index, const SceDisplayFrameBuf *pParam, int sync);

void hookFunctionExport(uint32_t nid, const void *func, const char* module){
	hooks[current_hook] = taiHookFunctionExportForKernel(KERNEL_PID, &refs[current_hook], module, TAI_ANY_LIBRARY, nid, func);
	current_hook++;
}

void clearScreen(){
	for (int i = 0; i < SCREEN_H; i++) {
		for (int j = 0; j < SCREEN_W; j++) {
			((unsigned int *)addr)[j + i * SCREEN_PITCH] = 0xFF000000;
		}
	}
}

void updateDisplay(){
	setTextColor(0xFF0000FF);
	clearScreen();
	drawStringF(200, 100, "Application LOCKED! Insert password to continue!");
	drawStringF(270, 120, "Attempts remaining: %d", retry + 1);
	drawStringF(320 + 12 * i, 205, "_");
	drawStringF(320, 200, "%d%d%d%d", pass[0], pass[1], pass[2], pass[3]);
	if (retry == 0){
		setTextColor(0xFF0000FF);
		drawStringF(150, 140, "Attempt limit REACHED! EXITING!");
		int i;
        bubble_log = fopen("ux0:data/BubbleLock/bubblelocklog.txt" ,"w");
		for(i = 0; i < 10;i++){
       		fprintf (bubble_log, "WARNING: Someone has attempted to get into one of your applications! %d\n",i + 1);
	}
	ksceDisplaySetFrameBufInternal(head, 1, &fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
}

void initFrameBuffer(){
	
	if (addr == NULL){	
		unsigned int size = ALIGN(4 * SCREEN_PITCH * SCREEN_H, 0x40000);
		fb_uid = ksceKernelAllocMemBlock("FrameBuffer", 0x40404006, size, NULL);
		ksceKernelGetMemBlockBase(fb_uid, &addr);
	}
	
	fb.size        = sizeof(fb);
	fb.base        = addr;
	fb.pitch       = SCREEN_PITCH;
	fb.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
	fb.width       = SCREEN_W;
	fb.height      = SCREEN_H;
	
	head = ksceDisplayGetPrimaryHead();
	
	updateFramebuf(&fb);
	updateDisplay();
	ksceDisplaySetFrameBufInternal(head, 1, &fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
	
}

static SceUID ksceKernelLaunchAppPatched(void *args) {
	
	// Getting passed args
	char* tid  = (char*)((uintptr_t*)args)[0];
	uint32_t flags = (uint32_t)((uintptr_t*)args)[1];
	char* path     = (char*)((uintptr_t*)args)[2];
	void* unk = (void*)((uintptr_t*)args)[3];
	
	// Resetting retry attempts
	retry = 2;
	
	// Executing ksceKernelLaunchApp original call
	SceUID r = TAI_CONTINUE(int, refs[0], tid, flags, path, unk);
	
	// Checking if the app requires a password
	sprintf(ppath, "ux0:data/BubbleLock/%s.txt", tid);
	fd = ksceIoOpen(ppath, SCE_O_RDONLY, 0777);
	if (fd >= 0){
		pass[0] = pass[1] = pass[2] = pass[3] = 0;
		ksceIoRead(fd, ppath, 4);
		ksceIoClose(fd);
		ppath[4] = 0;
		sscanf(ppath, "%04d", &guess);
		initFrameBuffer();
		SceCtrlData pad;
		uint32_t oldpad = 0;
		while (retry >= 0){
			ksceCtrlPeekBufferPositive(0, &pad, 1);
			ksceDisplaySetFrameBufInternal(head, 1, &fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
			ksceDisplayWaitVblankStart();
			if ((pad.buttons & SCE_CTRL_UP) && (!(oldpad & SCE_CTRL_UP))){
				pass[i] = (pass[i] + 1) % 10;
				updateDisplay();
			}else if ((pad.buttons & SCE_CTRL_DOWN) && (!(oldpad & SCE_CTRL_DOWN))){
				pass[i]--;
				if (pass[i] < 0) pass[i] = 9;
				updateDisplay();
			}else if ((pad.buttons & SCE_CTRL_LEFT) && (!(oldpad & SCE_CTRL_LEFT))){
				i--;
				if (i < 0) i = 3;
				updateDisplay();
			}else if ((pad.buttons & SCE_CTRL_RIGHT) && (!(oldpad & SCE_CTRL_RIGHT))){
				i = (i + 1) % 4;
				updateDisplay();
			}else if ((pad.buttons & SCE_CTRL_CROSS) && (!(oldpad & SCE_CTRL_CROSS))){
				int tpass = pass[0] * 1000 + pass[1] * 100 + pass[2] * 10 + pass[3];
				if (guess == tpass) break;
				else retry--;
				updateDisplay();
			}
			oldpad = pad.buttons;
		}
	}
	
	if (retry < 0) SCE_KERNEL_STOP_SUCCESS();
	return r;

}

SceUID ksceKernelLaunchApp_patched(char *tid, uint32_t flags, char *path, void *unk) {
	uintptr_t args[4];
	args[0] = (uintptr_t)tid;
	args[1] = (uintptr_t)flags;
	args[2] = (uintptr_t)path;
	args[3] = (uintptr_t)unk;

	return ksceKernelRunWithStack(0x4000, ksceKernelLaunchAppPatched, args);
}

void _start() __attribute__ ((weak, alias ("module_start")));
int module_start(SceSize argc, const void *args) {
	
	// Hooking desired functions
	hookFunctionExport(0x71CF71FD,ksceKernelLaunchApp_patched,"SceProcessmgr");
	
	return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize argc, const void *args) {

	// Freeing hooks
	while (current_hook-- > 0){
		taiHookReleaseForKernel(hooks[current_hook], refs[current_hook]);
	}

	return SCE_KERNEL_STOP_SUCCESS;
	
}