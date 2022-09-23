#if !defined(_DA7219_COMMON_H_)
#define _DA7219_COMMON_H_

//
//These are the device attributes returned by vmulti in response
// to IOCTL_HID_GET_DEVICE_ATTRIBUTES.
//

#define DA7219_PID              0x7219
#define DA7219_VID              0x2DCF
#define DA7219_VERSION          0x0001

//
// These are the report ids
//

#define REPORTID_MEDIA	0x01
#define REPORTID_SPECKEYS		0x02

#pragma pack(1)
typedef struct _DA7219_MEDIA_REPORT
{

	BYTE      ReportID;

	BYTE	  ControlCode;

} Da7219MediaReport;
#pragma pack()

#define CONTROL_CODE_JACK_TYPE 0x1

#pragma pack(1)
typedef struct _CSAUDIO_SPECKEY_REPORT
{

	BYTE      ReportID;

	BYTE	  ControlCode;

	BYTE	  ControlValue;

} CsAudioSpecialKeyReport;

#pragma pack()

#endif
