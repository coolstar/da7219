#if !defined(_DA7219_H_)
#define _DA7219_H_

#pragma warning(disable:4200)  // suppress nameless struct/union warning
#pragma warning(disable:4201)  // suppress nameless struct/union warning
#pragma warning(disable:4214)  // suppress bit field types other than int warning
#include <initguid.h>
#include <wdm.h>

#pragma warning(default:4200)
#pragma warning(default:4201)
#pragma warning(default:4214)
#include <wdf.h>

#include <portcls.h>

#include <acpiioct.h>
#include <ntstrsafe.h>

#include <stdint.h>

#include "spb.h"

#define JACKDESC_RGB(r, g, b) \
    ((COLORREF)((r << 16) | (g << 8) | (b)))

//
// String definitions
//

#define DRIVERNAME                 "da7219.sys: "

#define DA7219_POOL_TAG            (ULONG) 'B343'

#define true 1
#define false 0

typedef struct _DA7219_CONTEXT
{

	WDFDEVICE FxDevice;

	WDFQUEUE ReportQueue;

	SPB_CONTEXT I2CContext;

	BOOLEAN DevicePoweredOn;

} DA7219_CONTEXT, *PDA7219_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DA7219_CONTEXT, GetDeviceContext)

//
// Function definitions
//

DRIVER_INITIALIZE DriverEntry;

EVT_WDF_DRIVER_UNLOAD Da7219DriverUnload;

EVT_WDF_DRIVER_DEVICE_ADD Da7219EvtDeviceAdd;

EVT_WDFDEVICE_WDM_IRP_PREPROCESS Da7219EvtWdmPreprocessMnQueryId;

EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL Da7219EvtInternalDeviceControl;

//
// Helper macros
//

#define DEBUG_LEVEL_ERROR   1
#define DEBUG_LEVEL_INFO    2
#define DEBUG_LEVEL_VERBOSE 3

#define DBG_INIT  1
#define DBG_PNP   2
#define DBG_IOCTL 4

#if 0
#define Da7219Print(dbglevel, dbgcatagory, fmt, ...) {          \
    if (Da7219DebugLevel >= dbglevel &&                         \
        (Da7219DebugCatagories && dbgcatagory))                 \
	    {                                                           \
        DbgPrint(DRIVERNAME);                                   \
        DbgPrint(fmt, __VA_ARGS__);                             \
	    }                                                           \
}
#else
#define Da7219Print(dbglevel, fmt, ...) {                       \
}
#endif

#endif