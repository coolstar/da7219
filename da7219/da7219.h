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

#pragma warning(disable:4201)  // suppress nameless struct/union warning
#pragma warning(disable:4214)  // suppress bit field types other than int warning
#include <hidport.h>

#include "hidcommon.h"

#include <stdint.h>

#include "spb.h"

enum snd_jack_types {
	SND_JACK_HEADPHONE = 0x0001,
	SND_JACK_MICROPHONE = 0x0002,
	SND_JACK_HEADSET = SND_JACK_HEADPHONE | SND_JACK_MICROPHONE,
};

//
// String definitions
//

#define DRIVERNAME                 "da7219.sys: "

#define DA7219_POOL_TAG            (ULONG) 'B343'

#define true 1
#define false 0

typedef UCHAR HID_REPORT_DESCRIPTOR, * PHID_REPORT_DESCRIPTOR;

#ifdef DESCRIPTOR_DEF
HID_REPORT_DESCRIPTOR DefaultReportDescriptor[] = {
	//
	// Consumer Control starts here
	//
	0x05, 0x0C, /*		Usage Page (Consumer Devices)		*/
	0x09, 0x01, /*		Usage (Consumer Control)			*/
	0xA1, 0x01, /*		Collection (Application)			*/
	0x85, REPORTID_MEDIA,	/*		Report ID=1							*/
	0x05, 0x0C, /*		Usage Page (Consumer Devices)		*/
	0x15, 0x00, /*		Logical Minimum (0)					*/
	0x25, 0x01, /*		Logical Maximum (1)					*/
	0x75, 0x01, /*		Report Size (1)						*/
	0x95, 0x04, /*		Report Count (4)					*/
	0x09, 0xCD, /*		Usage (Play / Pause)				*/
	0x09, 0xCF, /*		Usage (Voice Command)				*/
	0x09, 0xE9, /*		Usage (Volume Up)					*/
	0x09, 0xEA, /*		Usage (Volume Down)					*/
	0x81, 0x02, /*		Input (Data, Variable, Absolute)	*/
	0x95, 0x04, /*		Report Count (4)					*/
	0x81, 0x01, /*		Input (Constant)					*/
	0xC0,        /*        End Collection                        */

	0x06, 0x00, 0xff,                    // USAGE_PAGE (Vendor Defined Page 1)
	0x09, 0x04,                          // USAGE (Vendor Usage 4)
	0xa1, 0x01,                          // COLLECTION (Application)
	0x85, REPORTID_SPECKEYS,             //   REPORT_ID (Special Keys)
	0x15, 0x00,                          //   LOGICAL_MINIMUM (0)
	0x26, 0xff, 0x00,                    //   LOGICAL_MAXIMUM (256)
	0x75, 0x08,                          //   REPORT_SIZE  (8)   - bits
	0x95, 0x01,                          //   REPORT_COUNT (1)  - Bytes
	0x09, 0x02,                          //   USAGE (Vendor Usage 1)
	0x81, 0x02,                          //   INPUT (Data,Var,Abs)
	0x09, 0x03,                          //   USAGE (Vendor Usage 2)
	0x81, 0x02,                          //   INPUT (Data,Var,Abs)
	0xc0,                                // END_COLLECTION
};


//
// This is the default HID descriptor returned by the mini driver
// in response to IOCTL_HID_GET_DEVICE_DESCRIPTOR. The size
// of report descriptor is currently the size of DefaultReportDescriptor.
//

CONST HID_DESCRIPTOR DefaultHidDescriptor = {
	0x09,   // length of HID descriptor
	0x21,   // descriptor type == HID  0x21
	0x0100, // hid spec release
	0x00,   // country code == Not Specified
	0x01,   // number of HID class descriptors
	{ 0x22,   // descriptor type 
	sizeof(DefaultReportDescriptor) }  // total length of report descriptor
};
#endif

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

NTSTATUS
Da7219GetHidDescriptor(
	IN WDFDEVICE Device,
	IN WDFREQUEST Request
);

NTSTATUS
Da7219GetReportDescriptor(
	IN WDFDEVICE Device,
	IN WDFREQUEST Request
);

NTSTATUS
Da7219GetDeviceAttributes(
	IN WDFREQUEST Request
);

NTSTATUS
Da7219GetString(
	IN WDFREQUEST Request
);

NTSTATUS
Da7219WriteReport(
	IN PDA7219_CONTEXT DevContext,
	IN WDFREQUEST Request
);

NTSTATUS
Da7219ProcessVendorReport(
	IN PDA7219_CONTEXT DevContext,
	IN PVOID ReportBuffer,
	IN ULONG ReportBufferLen,
	OUT size_t* BytesWritten
);

NTSTATUS
Da7219ReadReport(
	IN PDA7219_CONTEXT DevContext,
	IN WDFREQUEST Request,
	OUT BOOLEAN* CompleteRequest
);

NTSTATUS
Da7219SetFeature(
	IN PDA7219_CONTEXT DevContext,
	IN WDFREQUEST Request,
	OUT BOOLEAN* CompleteRequest
);

NTSTATUS
Da7219GetFeature(
	IN PDA7219_CONTEXT DevContext,
	IN WDFREQUEST Request,
	OUT BOOLEAN* CompleteRequest
);

PCHAR
DbgHidInternalIoctlString(
	IN ULONG        IoControlCode
);

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