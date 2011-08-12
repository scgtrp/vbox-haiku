#ifndef _VBOXVIDEO_COMMON_H
#define _VBOXVIDEO_COMMON_H

#include <Drivers.h>
#include <Accelerant.h>
#include <PCI.h>

struct SharedInfo {
	display_mode currentMode;
};

enum {
	VBOXVIDEO_GET_PRIVATE_DATA = B_DEVICE_OP_CODES_END + 1,
	VBOXVIDEO_GET_DEVICE_NAME,
	VBOXVIDEO_SET_DISPLAY_MODE
};

#endif