#ifndef _VBOXVIDEO_COMMON_H
#define _VBOXVIDEO_COMMON_H

#include <Drivers.h>
#include <Accelerant.h>
#include <PCI.h>

struct SharedInfo {
	display_mode currentMode;
	
	area_id framebufferArea;
	void* framebuffer;
};

enum {
	VBOXVIDEO_GET_PRIVATE_DATA = B_DEVICE_OP_CODES_END + 1,
	VBOXVIDEO_GET_DEVICE_NAME,
	VBOXVIDEO_SET_DISPLAY_MODE
};

static inline uint32 get_color_space_for_depth(uint32 depth)
{
	switch (depth) {
		case 1:
			return B_GRAY1;
		case 4:
			return B_GRAY8;
				// the app_server is smart enough to translate this to VGA mode
		case 8:
			return B_CMAP8;
		case 15:
			return B_RGB15;
		case 16:
			return B_RGB16;
		case 24:
			return B_RGB24;
		case 32:
			return B_RGB32;
	}

	return 0;
}

static inline uint32 get_depth_for_color_space(uint32 depth)
{
	switch (depth) {
		case B_GRAY1:
			return 1;
		case B_GRAY8:
			return 4;
		case B_CMAP8:
			return 8;
		case B_RGB15:
			return 15;
		case B_RGB16:
			return 16;
		case B_RGB24:
			return 24;
		case B_RGB32:
			return 32;
	}

	return 0;
}

#endif