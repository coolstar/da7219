#define DESCRIPTOR_DEF
#include "da7219.h"
#include "registers.h"
#include "registers-aad.h"

#define bool int

static ULONG Da7219DebugLevel = 100;
static ULONG Da7219DebugCatagories = DBG_INIT || DBG_PNP || DBG_IOCTL;

NTSTATUS
DriverEntry(
	__in PDRIVER_OBJECT  DriverObject,
	__in PUNICODE_STRING RegistryPath
)
{
	NTSTATUS               status = STATUS_SUCCESS;
	WDF_DRIVER_CONFIG      config;
	WDF_OBJECT_ATTRIBUTES  attributes;

	Da7219Print(DEBUG_LEVEL_INFO, DBG_INIT,
		"Driver Entry\n");

	WDF_DRIVER_CONFIG_INIT(&config, Da7219EvtDeviceAdd);

	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);

	//
	// Create a framework driver object to represent our driver.
	//

	status = WdfDriverCreate(DriverObject,
		RegistryPath,
		&attributes,
		&config,
		WDF_NO_HANDLE
	);

	if (!NT_SUCCESS(status))
	{
		Da7219Print(DEBUG_LEVEL_ERROR, DBG_INIT,
			"WdfDriverCreate failed with status 0x%x\n", status);
	}

	return status;
}

NTSTATUS da7219_reg_read(
	_In_ PDA7219_CONTEXT pDevice,
	uint8_t reg,
	unsigned int* data
) {
	uint8_t raw_data = 0;
	NTSTATUS status = SpbXferDataSynchronously(&pDevice->I2CContext, &reg, sizeof(uint8_t), &raw_data, sizeof(uint8_t));
	*data = raw_data;
	return status;
}

NTSTATUS da7219_reg_write(
	_In_ PDA7219_CONTEXT pDevice,
	uint8_t reg,
	unsigned int data
) {
	uint8_t buf[2];
	buf[0] = reg;
	buf[1] = data;
	return SpbWriteDataSynchronously(&pDevice->I2CContext, buf, sizeof(buf));
}

NTSTATUS da7219_reg_update(
	_In_ PDA7219_CONTEXT pDevice,
	uint8_t reg,
	unsigned int mask,
	unsigned int val
) {
	unsigned int tmp = 0, orig = 0;

	NTSTATUS status = da7219_reg_read(pDevice, reg, &orig);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	tmp = orig & ~mask;
	tmp |= val & mask;

	if (tmp != orig) {
		status = da7219_reg_write(pDevice, reg, tmp);
	}
	return status;
}

NTSTATUS
OnPrepareHardware(
	_In_  WDFDEVICE     FxDevice,
	_In_  WDFCMRESLIST  FxResourcesRaw,
	_In_  WDFCMRESLIST  FxResourcesTranslated
)
/*++

Routine Description:

This routine caches the SPB resource connection ID.

Arguments:

FxDevice - a handle to the framework device object
FxResourcesRaw - list of translated hardware resources that
the PnP manager has assigned to the device
FxResourcesTranslated - list of raw hardware resources that
the PnP manager has assigned to the device

Return Value:

Status

--*/
{
	PDA7219_CONTEXT pDevice = GetDeviceContext(FxDevice);
	BOOLEAN fSpbResourceFound = FALSE;
	NTSTATUS status = STATUS_INSUFFICIENT_RESOURCES;

	UNREFERENCED_PARAMETER(FxResourcesRaw);

	//
	// Parse the peripheral's resources.
	//

	ULONG resourceCount = WdfCmResourceListGetCount(FxResourcesTranslated);

	for (ULONG i = 0; i < resourceCount; i++)
	{
		PCM_PARTIAL_RESOURCE_DESCRIPTOR pDescriptor;
		UCHAR Class;
		UCHAR Type;

		pDescriptor = WdfCmResourceListGetDescriptor(
			FxResourcesTranslated, i);

		switch (pDescriptor->Type)
		{
		case CmResourceTypeConnection:
			//
			// Look for I2C or SPI resource and save connection ID.
			//
			Class = pDescriptor->u.Connection.Class;
			Type = pDescriptor->u.Connection.Type;
			if (Class == CM_RESOURCE_CONNECTION_CLASS_SERIAL &&
				Type == CM_RESOURCE_CONNECTION_TYPE_SERIAL_I2C)
			{
				if (fSpbResourceFound == FALSE)
				{
					status = STATUS_SUCCESS;
					pDevice->I2CContext.I2cResHubId.LowPart = pDescriptor->u.Connection.IdLowPart;
					pDevice->I2CContext.I2cResHubId.HighPart = pDescriptor->u.Connection.IdHighPart;
					fSpbResourceFound = TRUE;
				}
				else
				{
				}
			}
			break;
		default:
			//
			// Ignoring all other resource types.
			//
			break;
		}
	}

	//
	// An SPB resource is required.
	//

	if (fSpbResourceFound == FALSE)
	{
		status = STATUS_NOT_FOUND;
	}

	status = SpbTargetInitialize(FxDevice, &pDevice->I2CContext);

	if (!NT_SUCCESS(status))
	{
		return status;
	}

	return status;
}

NTSTATUS
OnReleaseHardware(
	_In_  WDFDEVICE     FxDevice,
	_In_  WDFCMRESLIST  FxResourcesTranslated
)
/*++

Routine Description:

Arguments:

FxDevice - a handle to the framework device object
FxResourcesTranslated - list of raw hardware resources that
the PnP manager has assigned to the device

Return Value:

Status

--*/
{
	PDA7219_CONTEXT pDevice = GetDeviceContext(FxDevice);
	NTSTATUS status = STATUS_SUCCESS;

	UNREFERENCED_PARAMETER(FxResourcesTranslated);

	SpbTargetDeinitialize(FxDevice, &pDevice->I2CContext);

	return status;
}

VOID
DA7219BootWorkItem(
	IN WDFWORKITEM  WorkItem
)
{
	WDFDEVICE Device = (WDFDEVICE)WdfWorkItemGetParentObject(WorkItem);
	PDA7219_CONTEXT pDevice = GetDeviceContext(Device);

	unsigned int system_active, system_status;
	int i;
	da7219_reg_read(pDevice, DA7219_SYSTEM_ACTIVE, &system_active);
	if (system_active) {
		da7219_reg_write(pDevice, DA7219_GAIN_RAMP_CTRL,
			DA7219_GAIN_RAMP_RATE_NOMINAL);
		da7219_reg_write(pDevice, DA7219_SYSTEM_MODES_INPUT, 0x00);
		da7219_reg_write(pDevice, DA7219_SYSTEM_MODES_OUTPUT, 0x01);

		for (i = 0; i < DA7219_SYS_STAT_CHECK_RETRIES; ++i) {
			da7219_reg_read(pDevice, DA7219_SYSTEM_STATUS,
				&system_status);
			if (!system_status)
				break;

			LARGE_INTEGER Interval;
			Interval.QuadPart = -10 * 1000 * DA7219_SYS_STAT_CHECK_DELAY;
			KeDelayExecutionThread(KernelMode, false, &Interval);
		}
	}

	/* Soft reset component */
	da7219_reg_update(pDevice, DA7219_ACCDET_CONFIG_1,
		DA7219_ACCDET_EN_MASK, 0);
	da7219_reg_update(pDevice, DA7219_CIF_CTRL,
		DA7219_CIF_REG_SOFT_RESET_MASK,
		DA7219_CIF_REG_SOFT_RESET_MASK);
	da7219_reg_update(pDevice, DA7219_SYSTEM_ACTIVE,
		DA7219_SYSTEM_ACTIVE_MASK, 0);
	da7219_reg_update(pDevice, DA7219_SYSTEM_ACTIVE,
		DA7219_SYSTEM_ACTIVE_MASK, 1);

	da7219_reg_write(pDevice, DA7219_IO_CTRL, DA7219_IO_VOLTAGE_LEVEL_2_5V_3_6V);

	unsigned int rev;
	da7219_reg_read(pDevice, DA7219_CHIP_REVISION, &rev);
	DbgPrint("DA7219 revision: %d\n", rev & DA7219_CHIP_MINOR_MASK);
	if ((rev & DA7219_CHIP_MINOR_MASK) == 0) {
		da7219_reg_write(pDevice, DA7219_REFERENCES, 0x08);
	}

	/* Default PC counter to free-running */
	da7219_reg_update(pDevice, DA7219_PC_COUNT, DA7219_PC_FREERUN_MASK,
		DA7219_PC_FREERUN_MASK);

	/* Default gain ramping */
	da7219_reg_update(pDevice, DA7219_MIXIN_L_CTRL,
		DA7219_MIXIN_L_AMP_RAMP_EN_MASK,
		DA7219_MIXIN_L_AMP_RAMP_EN_MASK);
	da7219_reg_update(pDevice, DA7219_ADC_L_CTRL, DA7219_ADC_L_RAMP_EN_MASK,
		DA7219_ADC_L_RAMP_EN_MASK);
	da7219_reg_update(pDevice, DA7219_DAC_L_CTRL, DA7219_DAC_L_RAMP_EN_MASK,
		DA7219_DAC_L_RAMP_EN_MASK);
	da7219_reg_update(pDevice, DA7219_DAC_R_CTRL, DA7219_DAC_R_RAMP_EN_MASK,
		DA7219_DAC_R_RAMP_EN_MASK);
	da7219_reg_update(pDevice, DA7219_HP_L_CTRL,
		DA7219_HP_L_AMP_RAMP_EN_MASK,
		DA7219_HP_L_AMP_RAMP_EN_MASK);
	da7219_reg_update(pDevice, DA7219_HP_R_CTRL,
		DA7219_HP_R_AMP_RAMP_EN_MASK,
		DA7219_HP_R_AMP_RAMP_EN_MASK);

	/* Default minimum gain on HP to avoid pops during DAPM sequencing */
	da7219_reg_update(pDevice, DA7219_HP_L_CTRL,
		DA7219_HP_L_AMP_MIN_GAIN_EN_MASK,
		DA7219_HP_L_AMP_MIN_GAIN_EN_MASK);
	da7219_reg_update(pDevice, DA7219_HP_R_CTRL,
		DA7219_HP_R_AMP_MIN_GAIN_EN_MASK,
		DA7219_HP_R_AMP_MIN_GAIN_EN_MASK);

	/* Default infinite tone gen, start/stop by Kcontrol */
	da7219_reg_write(pDevice, DA7219_TONE_GEN_CYCLES, DA7219_BEEP_CYCLES_MASK);

	{ //AAD init
		da7219_reg_update(pDevice, DA7219_ACCDET_CONFIG_1, DA7219_MIC_DET_THRESH_MASK, DA7219_AAD_MIC_DET_THR_500_OHMS);

		da7219_reg_update(pDevice, DA7219_ACCDET_CONFIG_2, DA7219_JACKDET_DEBOUNCE_MASK | DA7219_JACK_DETECT_RATE_MASK | DA7219_JACKDET_REM_DEB_MASK,
			DA7219_AAD_JACK_INS_DEB_20MS | DA7219_AAD_JACK_DET_RATE_32_64MS | DA7219_AAD_JACK_REM_DEB_1MS);

		da7219_reg_write(pDevice, DA7219_ACCDET_CONFIG_3,
			0xA);
		da7219_reg_write(pDevice, DA7219_ACCDET_CONFIG_4,
			0x16);
		da7219_reg_write(pDevice, DA7219_ACCDET_CONFIG_5,
			0x21);
		da7219_reg_write(pDevice, DA7219_ACCDET_CONFIG_6,
			0x3e);

		da7219_reg_update(pDevice, DA7219_ACCDET_CONFIG_7, DA7219_BUTTON_AVERAGE_MASK | DA7219_ADC_1_BIT_REPEAT_MASK, DA7219_AAD_BTN_AVG_4 | DA7219_AAD_ADC_1BIT_RPT_1);
	}

	{
		//Set sample rate
		da7219_reg_write(pDevice, DA7219_SR, DA7219_SR_48000);
		//Set PLL
		da7219_reg_write(pDevice, DA7219_PLL_CTRL, DA7219_PLL_MODE_SRM | DA7219_PLL_INDIV_9_TO_18_MHZ | DA7219_PLL_INDIV_4_5_TO_9_MHZ);
		da7219_reg_write(pDevice, DA7219_PLL_FRAC_TOP, 0x1E & DA7219_PLL_FBDIV_FRAC_TOP_MASK);
		da7219_reg_write(pDevice, DA7219_PLL_FRAC_BOT, 0xB8 & DA7219_PLL_FBDIV_FRAC_BOT_MASK);
		da7219_reg_write(pDevice, DA7219_PLL_INTEGER, 0x28 & DA7219_PLL_FBDIV_INTEGER_MASK);

		da7219_reg_write(pDevice, DA7219_DIG_ROUTING_DAI, 0);
		da7219_reg_write(pDevice, DA7219_DAI_CTRL, DA7219_DAI_FORMAT_I2S | (2 << DA7219_DAI_CH_NUM_SHIFT) | DA7219_DAI_EN_MASK);
		da7219_reg_write(pDevice, DA7219_DAI_TDM_CTRL, DA7219_DAI_OE_MASK);

		da7219_reg_write(pDevice, DA7219_MIXIN_L_SELECT, DA7219_MIXIN_L_MIX_SELECT_MASK);
		da7219_reg_write(pDevice, DA7219_MIXIN_L_GAIN, 0xA);

		da7219_reg_write(pDevice, DA7219_MIC_1_GAIN, 0x5);

		da7219_reg_write(pDevice, DA7219_CP_CTRL, 0xE0);
		da7219_reg_write(pDevice, DA7219_HP_L_GAIN, 0x32);
		da7219_reg_write(pDevice, DA7219_HP_R_GAIN, 0x32);

		da7219_reg_write(pDevice, DA7219_MIXOUT_L_SELECT, DA7219_MIXOUT_L_MIX_SELECT_MASK);
		da7219_reg_write(pDevice, DA7219_MIXOUT_R_SELECT, DA7219_MIXOUT_R_MIX_SELECT_MASK);

		da7219_reg_write(pDevice, DA7219_MICBIAS_CTRL, 0x0D);
		da7219_reg_write(pDevice, DA7219_MIC_1_CTRL, DA7219_MIC_1_AMP_EN_MASK);
		da7219_reg_write(pDevice, DA7219_MIXIN_L_CTRL, DA7219_MIXIN_L_AMP_EN_MASK | DA7219_MIXIN_L_AMP_RAMP_EN_MASK | DA7219_MIXIN_L_MIX_EN_MASK);
		da7219_reg_write(pDevice, DA7219_ADC_L_CTRL, DA7219_ADC_L_EN_MASK | DA7219_ADC_L_RAMP_EN_MASK);

		da7219_reg_write(pDevice, DA7219_DAC_L_CTRL, 8 | DA7219_DAC_L_RAMP_EN_MASK | DA7219_DAC_L_EN_MASK);
		da7219_reg_write(pDevice, DA7219_DAC_R_CTRL, DA7219_DAC_R_RAMP_EN_MASK | DA7219_DAC_R_EN_MASK);
		da7219_reg_write(pDevice, DA7219_HP_L_CTRL, DA7219_HP_L_AMP_OE_MASK | DA7219_HP_L_AMP_RAMP_EN_MASK | DA7219_HP_L_AMP_EN_MASK);
		da7219_reg_write(pDevice, DA7219_HP_R_CTRL, DA7219_HP_R_AMP_OE_MASK | DA7219_HP_R_AMP_RAMP_EN_MASK | DA7219_HP_R_AMP_EN_MASK);
		da7219_reg_write(pDevice, DA7219_HP_R_CTRL, DA7219_HP_R_AMP_OE_MASK | DA7219_HP_R_AMP_RAMP_EN_MASK | DA7219_HP_R_AMP_EN_MASK);

		da7219_reg_write(pDevice, DA7219_MIXOUT_L_CTRL, DA7219_MIXOUT_L_AMP_EN_MASK);
		da7219_reg_write(pDevice, DA7219_MIXOUT_R_CTRL, DA7219_MIXOUT_R_AMP_EN_MASK);

		da7219_reg_write(pDevice, DA7219_GAIN_RAMP_CTRL, DA7219_GAIN_RAMP_RATE_NOMINAL);
		da7219_reg_write(pDevice, DA7219_PC_COUNT, DA7219_PC_RESYNC_AUTO_MASK);

		da7219_reg_write(pDevice, DA7219_ACCDET_CONFIG_1, 0xD9);
		da7219_reg_write(pDevice, DA7219_ACCDET_CONFIG_2, 0x04);
	}

	pDevice->DevicePoweredOn = TRUE;

end:
	WdfObjectDelete(WorkItem);
}

NTSTATUS
OnD0Entry(
	_In_  WDFDEVICE               FxDevice,
	_In_  WDF_POWER_DEVICE_STATE  FxPreviousState
)
/*++

Routine Description:

This routine allocates objects needed by the driver.

Arguments:

FxDevice - a handle to the framework device object
FxPreviousState - previous power state

Return Value:

Status

--*/
{
	UNREFERENCED_PARAMETER(FxPreviousState);

	PDA7219_CONTEXT pDevice = GetDeviceContext(FxDevice);
	NTSTATUS status = STATUS_SUCCESS;

	WDF_OBJECT_ATTRIBUTES attributes;
	WDF_WORKITEM_CONFIG workitemConfig;
	WDFWORKITEM hWorkItem;

	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
	WDF_OBJECT_ATTRIBUTES_SET_CONTEXT_TYPE(&attributes, DA7219_CONTEXT);
	attributes.ParentObject = pDevice->FxDevice;
	WDF_WORKITEM_CONFIG_INIT(&workitemConfig, DA7219BootWorkItem);

	WdfWorkItemCreate(&workitemConfig,
		&attributes,
		&hWorkItem);

	WdfWorkItemEnqueue(hWorkItem);

	return status;
}

NTSTATUS
OnD0Exit(
	_In_  WDFDEVICE               FxDevice,
	_In_  WDF_POWER_DEVICE_STATE  FxPreviousState
)
/*++

Routine Description:

This routine destroys objects needed by the driver.

Arguments:

FxDevice - a handle to the framework device object
FxPreviousState - previous power state

Return Value:

Status

--*/
{
	UNREFERENCED_PARAMETER(FxPreviousState);

	PDA7219_CONTEXT pDevice = GetDeviceContext(FxDevice);
	NTSTATUS status = STATUS_SUCCESS;

	pDevice->DevicePoweredOn = FALSE;

	return STATUS_SUCCESS;
}

BOOLEAN OnInterruptIsr(
	WDFINTERRUPT Interrupt,
	ULONG MessageID) {
	UNREFERENCED_PARAMETER(MessageID);

	WDFDEVICE Device = WdfInterruptGetDevice(Interrupt);
	PDA7219_CONTEXT pDevice = GetDeviceContext(Device);

	if (!pDevice->DevicePoweredOn)
		return true;

	NTSTATUS status = STATUS_SUCCESS;

	unsigned int reg_a;
	unsigned int reg_b;

	da7219_reg_read(pDevice, DA7219_ACCDET_IRQ_EVENT_A, &reg_a);
	da7219_reg_read(pDevice, DA7219_ACCDET_IRQ_EVENT_B, &reg_b);

	unsigned int status_a;
	da7219_reg_read(pDevice, DA7219_ACCDET_STATUS_A, &status_a);

	//Clear events
	da7219_reg_write(pDevice, DA7219_ACCDET_IRQ_EVENT_A, reg_a);
	da7219_reg_write(pDevice, DA7219_ACCDET_IRQ_EVENT_B, reg_b);

	if (status_a & DA7219_JACK_INSERTION_STS_MASK) {
		if (reg_a & DA7219_E_JACK_INSERTED_MASK) {
			//DbgPrint("Jack inserted\n");
		}
		if (reg_a & DA7219_E_JACK_DETECT_COMPLETE_MASK) {
			CsAudioSpecialKeyReport report;
			report.ReportID = REPORTID_SPECKEYS;
			report.ControlCode = CONTROL_CODE_JACK_TYPE;
			report.ControlValue = SND_JACK_HEADSET;

			size_t bytesWritten;
			Da7219ProcessVendorReport(pDevice, &report, sizeof(report), &bytesWritten);
		}

		if (status_a & DA7219_JACK_TYPE_STS_MASK) {
			for (int i = 0; i < DA7219_AAD_MAX_BUTTONS; ++i) {
				/* Button Release */
				if (reg_b &
					(DA7219_E_BUTTON_A_RELEASED_MASK >> i)) {
					Da7219MediaReport report;
					report.ReportID = REPORTID_MEDIA;
					report.ControlCode = i + 1;

					size_t bytesWritten;
					Da7219ProcessVendorReport(pDevice, &report, sizeof(report), &bytesWritten);
				}
			}
		}
	}
	else if (reg_a & DA7219_E_JACK_REMOVED_MASK) {
		CsAudioSpecialKeyReport report;
		report.ReportID = REPORTID_SPECKEYS;
		report.ControlCode = CONTROL_CODE_JACK_TYPE;
		report.ControlValue = 0;

		size_t bytesWritten;
		Da7219ProcessVendorReport(pDevice, &report, sizeof(report), &bytesWritten);
	}

	return true;
}

NTSTATUS
Da7219EvtDeviceAdd(
	IN WDFDRIVER       Driver,
	IN PWDFDEVICE_INIT DeviceInit
)
{
	NTSTATUS                      status = STATUS_SUCCESS;
	WDF_IO_QUEUE_CONFIG           queueConfig;
	WDF_OBJECT_ATTRIBUTES         attributes;
	WDFDEVICE                     device;
	WDF_INTERRUPT_CONFIG interruptConfig;
	WDFQUEUE                      queue;
	UCHAR                         minorFunction;
	PDA7219_CONTEXT               devContext;

	UNREFERENCED_PARAMETER(Driver);

	PAGED_CODE();

	Da7219Print(DEBUG_LEVEL_INFO, DBG_PNP,
		"Da7219EvtDeviceAdd called\n");

	//
	// Tell framework this is a filter driver. Filter drivers by default are  
	// not power policy owners. This works well for this driver because
	// HIDclass driver is the power policy owner for HID minidrivers.
	//

	WdfFdoInitSetFilter(DeviceInit);

	{
		WDF_PNPPOWER_EVENT_CALLBACKS pnpCallbacks;
		WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpCallbacks);

		pnpCallbacks.EvtDevicePrepareHardware = OnPrepareHardware;
		pnpCallbacks.EvtDeviceReleaseHardware = OnReleaseHardware;
		pnpCallbacks.EvtDeviceD0Entry = OnD0Entry;
		pnpCallbacks.EvtDeviceD0Exit = OnD0Exit;

		WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpCallbacks);
	}

	//
	// Setup the device context
	//

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DA7219_CONTEXT);

	//
	// Create a framework device object.This call will in turn create
	// a WDM device object, attach to the lower stack, and set the
	// appropriate flags and attributes.
	//

	status = WdfDeviceCreate(&DeviceInit, &attributes, &device);

	if (!NT_SUCCESS(status))
	{
		Da7219Print(DEBUG_LEVEL_ERROR, DBG_PNP,
			"WdfDeviceCreate failed with status code 0x%x\n", status);

		return status;
	}

	{
		WDF_DEVICE_STATE deviceState;
		WDF_DEVICE_STATE_INIT(&deviceState);

		deviceState.NotDisableable = WdfFalse;
		WdfDeviceSetDeviceState(device, &deviceState);
	}

	WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);

	queueConfig.EvtIoInternalDeviceControl = Da7219EvtInternalDeviceControl;

	status = WdfIoQueueCreate(device,
		&queueConfig,
		WDF_NO_OBJECT_ATTRIBUTES,
		&queue
	);

	if (!NT_SUCCESS(status))
	{
		Da7219Print(DEBUG_LEVEL_ERROR, DBG_PNP,
			"WdfIoQueueCreate failed 0x%x\n", status);

		return status;
	}

	//
	// Create manual I/O queue to take care of hid report read requests
	//

	devContext = GetDeviceContext(device);

	devContext->FxDevice = device;

	WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);

	queueConfig.PowerManaged = WdfFalse;

	status = WdfIoQueueCreate(device,
		&queueConfig,
		WDF_NO_OBJECT_ATTRIBUTES,
		&devContext->ReportQueue
	);

	if (!NT_SUCCESS(status))
	{
		Da7219Print(DEBUG_LEVEL_ERROR, DBG_PNP,
			"WdfIoQueueCreate failed 0x%x\n", status);

		return status;
	}

	//
	// Create an interrupt object for hardware notifications
	//
	WDF_INTERRUPT_CONFIG_INIT(
		&interruptConfig,
		OnInterruptIsr,
		NULL);
	interruptConfig.PassiveHandling = TRUE;

	status = WdfInterruptCreate(
		device,
		&interruptConfig,
		WDF_NO_OBJECT_ATTRIBUTES,
		&devContext->Interrupt);

	if (!NT_SUCCESS(status))
	{
		Da7219Print(DEBUG_LEVEL_ERROR, DBG_PNP,
			"Error creating WDF interrupt object - %!STATUS!",
			status);

		return status;
	}

	return status;
}

VOID
Da7219EvtInternalDeviceControl(
	IN WDFQUEUE     Queue,
	IN WDFREQUEST   Request,
	IN size_t       OutputBufferLength,
	IN size_t       InputBufferLength,
	IN ULONG        IoControlCode
)
{
	NTSTATUS            status = STATUS_SUCCESS;
	WDFDEVICE           device;
	PDA7219_CONTEXT     devContext;
	BOOLEAN             completeRequest = TRUE;

	UNREFERENCED_PARAMETER(OutputBufferLength);
	UNREFERENCED_PARAMETER(InputBufferLength);

	device = WdfIoQueueGetDevice(Queue);
	devContext = GetDeviceContext(device);

	Da7219Print(DEBUG_LEVEL_INFO, DBG_IOCTL,
		"%s, Queue:0x%p, Request:0x%p\n",
		DbgHidInternalIoctlString(IoControlCode),
		Queue,
		Request
	);

	//
	// Please note that HIDCLASS provides the buffer in the Irp->UserBuffer
	// field irrespective of the ioctl buffer type. However, framework is very
	// strict about type checking. You cannot get Irp->UserBuffer by using
	// WdfRequestRetrieveOutputMemory if the ioctl is not a METHOD_NEITHER
	// internal ioctl. So depending on the ioctl code, we will either
	// use retreive function or escape to WDM to get the UserBuffer.
	//

	switch (IoControlCode)
	{

	case IOCTL_HID_GET_DEVICE_DESCRIPTOR:
		//
		// Retrieves the device's HID descriptor.
		//
		status = Da7219GetHidDescriptor(device, Request);
		break;

	case IOCTL_HID_GET_DEVICE_ATTRIBUTES:
		//
		//Retrieves a device's attributes in a HID_DEVICE_ATTRIBUTES structure.
		//
		status = Da7219GetDeviceAttributes(Request);
		break;

	case IOCTL_HID_GET_REPORT_DESCRIPTOR:
		//
		//Obtains the report descriptor for the HID device.
		//
		status = Da7219GetReportDescriptor(device, Request);
		break;

	case IOCTL_HID_GET_STRING:
		//
		// Requests that the HID minidriver retrieve a human-readable string
		// for either the manufacturer ID, the product ID, or the serial number
		// from the string descriptor of the device. The minidriver must send
		// a Get String Descriptor request to the device, in order to retrieve
		// the string descriptor, then it must extract the string at the
		// appropriate index from the string descriptor and return it in the
		// output buffer indicated by the IRP. Before sending the Get String
		// Descriptor request, the minidriver must retrieve the appropriate
		// index for the manufacturer ID, the product ID or the serial number
		// from the device extension of a top level collection associated with
		// the device.
		//
		status = Da7219GetString(Request);
		break;

	case IOCTL_HID_WRITE_REPORT:
	case IOCTL_HID_SET_OUTPUT_REPORT:
		//
		//Transmits a class driver-supplied report to the device.
		//
		status = Da7219WriteReport(devContext, Request);
		break;

	case IOCTL_HID_READ_REPORT:
	case IOCTL_HID_GET_INPUT_REPORT:
		//
		// Returns a report from the device into a class driver-supplied buffer.
		// 
		status = Da7219ReadReport(devContext, Request, &completeRequest);
		break;

	case IOCTL_HID_SET_FEATURE:
		//
		// This sends a HID class feature report to a top-level collection of
		// a HID class device.
		//
		status = Da7219SetFeature(devContext, Request, &completeRequest);
		break;

	case IOCTL_HID_GET_FEATURE:
		//
		// returns a feature report associated with a top-level collection
		status = Da7219GetFeature(devContext, Request, &completeRequest);
		break;

	case IOCTL_HID_ACTIVATE_DEVICE:
		//
		// Makes the device ready for I/O operations.
		//
	case IOCTL_HID_DEACTIVATE_DEVICE:
		//
		// Causes the device to cease operations and terminate all outstanding
		// I/O requests.
		//
	default:
		status = STATUS_NOT_SUPPORTED;
		break;
	}

	if (completeRequest)
	{
		WdfRequestComplete(Request, status);

		Da7219Print(DEBUG_LEVEL_INFO, DBG_IOCTL,
			"%s completed, Queue:0x%p, Request:0x%p\n",
			DbgHidInternalIoctlString(IoControlCode),
			Queue,
			Request
		);
	}
	else
	{
		Da7219Print(DEBUG_LEVEL_INFO, DBG_IOCTL,
			"%s deferred, Queue:0x%p, Request:0x%p\n",
			DbgHidInternalIoctlString(IoControlCode),
			Queue,
			Request
		);
	}

	return;
}

NTSTATUS
Da7219GetHidDescriptor(
	IN WDFDEVICE Device,
	IN WDFREQUEST Request
)
{
	NTSTATUS            status = STATUS_SUCCESS;
	size_t              bytesToCopy = 0;
	WDFMEMORY           memory;

	UNREFERENCED_PARAMETER(Device);

	Da7219Print(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Da7219GetHidDescriptor Entry\n");

	//
	// This IOCTL is METHOD_NEITHER so WdfRequestRetrieveOutputMemory
	// will correctly retrieve buffer from Irp->UserBuffer. 
	// Remember that HIDCLASS provides the buffer in the Irp->UserBuffer
	// field irrespective of the ioctl buffer type. However, framework is very
	// strict about type checking. You cannot get Irp->UserBuffer by using
	// WdfRequestRetrieveOutputMemory if the ioctl is not a METHOD_NEITHER
	// internal ioctl.
	//
	status = WdfRequestRetrieveOutputMemory(Request, &memory);

	if (!NT_SUCCESS(status))
	{
		Da7219Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfRequestRetrieveOutputMemory failed 0x%x\n", status);

		return status;
	}

	//
	// Use hardcoded "HID Descriptor" 
	//
	bytesToCopy = DefaultHidDescriptor.bLength;

	if (bytesToCopy == 0)
	{
		status = STATUS_INVALID_DEVICE_STATE;

		Da7219Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"DefaultHidDescriptor is zero, 0x%x\n", status);

		return status;
	}

	status = WdfMemoryCopyFromBuffer(memory,
		0, // Offset
		(PVOID)&DefaultHidDescriptor,
		bytesToCopy);

	if (!NT_SUCCESS(status))
	{
		Da7219Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfMemoryCopyFromBuffer failed 0x%x\n", status);

		return status;
	}

	//
	// Report how many bytes were copied
	//
	WdfRequestSetInformation(Request, bytesToCopy);

	Da7219Print(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Da7219GetHidDescriptor Exit = 0x%x\n", status);

	return status;
}

NTSTATUS
Da7219GetReportDescriptor(
	IN WDFDEVICE Device,
	IN WDFREQUEST Request
)
{
	NTSTATUS            status = STATUS_SUCCESS;
	ULONG_PTR           bytesToCopy;
	WDFMEMORY           memory;

	PDA7219_CONTEXT devContext = GetDeviceContext(Device);

	UNREFERENCED_PARAMETER(Device);

	Da7219Print(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Da7219GetReportDescriptor Entry\n");

	//
	// This IOCTL is METHOD_NEITHER so WdfRequestRetrieveOutputMemory
	// will correctly retrieve buffer from Irp->UserBuffer. 
	// Remember that HIDCLASS provides the buffer in the Irp->UserBuffer
	// field irrespective of the ioctl buffer type. However, framework is very
	// strict about type checking. You cannot get Irp->UserBuffer by using
	// WdfRequestRetrieveOutputMemory if the ioctl is not a METHOD_NEITHER
	// internal ioctl.
	//
	status = WdfRequestRetrieveOutputMemory(Request, &memory);
	if (!NT_SUCCESS(status))
	{
		Da7219Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfRequestRetrieveOutputMemory failed 0x%x\n", status);

		return status;
	}

	//
	// Use hardcoded Report descriptor
	//
	bytesToCopy = DefaultHidDescriptor.DescriptorList[0].wReportLength;

	if (bytesToCopy == 0)
	{
		status = STATUS_INVALID_DEVICE_STATE;

		Da7219Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"DefaultHidDescriptor's reportLength is zero, 0x%x\n", status);

		return status;
	}

	status = WdfMemoryCopyFromBuffer(memory,
		0,
		(PVOID)DefaultReportDescriptor,
		bytesToCopy);
	if (!NT_SUCCESS(status))
	{
		Da7219Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfMemoryCopyFromBuffer failed 0x%x\n", status);

		return status;
	}

	//
	// Report how many bytes were copied
	//
	WdfRequestSetInformation(Request, bytesToCopy);

	Da7219Print(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Da7219GetReportDescriptor Exit = 0x%x\n", status);

	return status;
}


NTSTATUS
Da7219GetDeviceAttributes(
	IN WDFREQUEST Request
)
{
	NTSTATUS                 status = STATUS_SUCCESS;
	PHID_DEVICE_ATTRIBUTES   deviceAttributes = NULL;

	Da7219Print(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Da7219GetDeviceAttributes Entry\n");

	//
	// This IOCTL is METHOD_NEITHER so WdfRequestRetrieveOutputMemory
	// will correctly retrieve buffer from Irp->UserBuffer. 
	// Remember that HIDCLASS provides the buffer in the Irp->UserBuffer
	// field irrespective of the ioctl buffer type. However, framework is very
	// strict about type checking. You cannot get Irp->UserBuffer by using
	// WdfRequestRetrieveOutputMemory if the ioctl is not a METHOD_NEITHER
	// internal ioctl.
	//
	status = WdfRequestRetrieveOutputBuffer(Request,
		sizeof(HID_DEVICE_ATTRIBUTES),
		(PVOID*)&deviceAttributes,
		NULL);
	if (!NT_SUCCESS(status))
	{
		Da7219Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfRequestRetrieveOutputBuffer failed 0x%x\n", status);

		return status;
	}

	//
	// Set USB device descriptor
	//

	deviceAttributes->Size = sizeof(HID_DEVICE_ATTRIBUTES);
	deviceAttributes->VendorID = DA7219_VID;
	deviceAttributes->ProductID = DA7219_PID;
	deviceAttributes->VersionNumber = DA7219_VERSION;

	//
	// Report how many bytes were copied
	//
	WdfRequestSetInformation(Request, sizeof(HID_DEVICE_ATTRIBUTES));

	Da7219Print(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Da7219GetDeviceAttributes Exit = 0x%x\n", status);

	return status;
}

NTSTATUS
Da7219GetString(
	IN WDFREQUEST Request
)
{

	NTSTATUS status = STATUS_SUCCESS;
	PWSTR pwstrID;
	size_t lenID;
	WDF_REQUEST_PARAMETERS params;
	void* pStringBuffer = NULL;

	Da7219Print(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Da7219GetString Entry\n");

	WDF_REQUEST_PARAMETERS_INIT(&params);
	WdfRequestGetParameters(Request, &params);

	switch ((ULONG_PTR)params.Parameters.DeviceIoControl.Type3InputBuffer & 0xFFFF)
	{
	case HID_STRING_ID_IMANUFACTURER:
		pwstrID = L"Da7219.\0";
		break;

	case HID_STRING_ID_IPRODUCT:
		pwstrID = L"MaxTouch Touch Screen\0";
		break;

	case HID_STRING_ID_ISERIALNUMBER:
		pwstrID = L"123123123\0";
		break;

	default:
		pwstrID = NULL;
		break;
	}

	lenID = pwstrID ? wcslen(pwstrID) * sizeof(WCHAR) + sizeof(UNICODE_NULL) : 0;

	if (pwstrID == NULL)
	{

		Da7219Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"Da7219GetString Invalid request type\n");

		status = STATUS_INVALID_PARAMETER;

		return status;
	}

	status = WdfRequestRetrieveOutputBuffer(Request,
		lenID,
		&pStringBuffer,
		&lenID);

	if (!NT_SUCCESS(status))
	{

		Da7219Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"Da7219GetString WdfRequestRetrieveOutputBuffer failed Status 0x%x\n", status);

		return status;
	}

	RtlCopyMemory(pStringBuffer, pwstrID, lenID);

	WdfRequestSetInformation(Request, lenID);

	Da7219Print(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Da7219GetString Exit = 0x%x\n", status);

	return status;
}

NTSTATUS
Da7219WriteReport(
	IN PDA7219_CONTEXT DevContext,
	IN WDFREQUEST Request
)
{
	NTSTATUS status = STATUS_SUCCESS;
	WDF_REQUEST_PARAMETERS params;
	PHID_XFER_PACKET transferPacket = NULL;
	size_t bytesWritten = 0;

	Da7219Print(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Da7219WriteReport Entry\n");

	WDF_REQUEST_PARAMETERS_INIT(&params);
	WdfRequestGetParameters(Request, &params);

	if (params.Parameters.DeviceIoControl.InputBufferLength < sizeof(HID_XFER_PACKET))
	{
		Da7219Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"Da7219WriteReport Xfer packet too small\n");

		status = STATUS_BUFFER_TOO_SMALL;
	}
	else
	{

		transferPacket = (PHID_XFER_PACKET)WdfRequestWdmGetIrp(Request)->UserBuffer;

		if (transferPacket == NULL)
		{
			Da7219Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
				"Da7219WriteReport No xfer packet\n");

			status = STATUS_INVALID_DEVICE_REQUEST;
		}
		else
		{
			//
			// switch on the report id
			//

			switch (transferPacket->reportId)
			{
			default:

				Da7219Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
					"Da7219WriteReport Unhandled report type %d\n", transferPacket->reportId);

				status = STATUS_INVALID_PARAMETER;

				break;
			}
		}
	}

	Da7219Print(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Da7219WriteReport Exit = 0x%x\n", status);

	return status;

}

NTSTATUS
Da7219ProcessVendorReport(
	IN PDA7219_CONTEXT DevContext,
	IN PVOID ReportBuffer,
	IN ULONG ReportBufferLen,
	OUT size_t* BytesWritten
)
{
	NTSTATUS status = STATUS_SUCCESS;
	WDFREQUEST reqRead;
	PVOID pReadReport = NULL;
	size_t bytesReturned = 0;

	Da7219Print(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Da7219ProcessVendorReport Entry\n");

	status = WdfIoQueueRetrieveNextRequest(DevContext->ReportQueue,
		&reqRead);

	if (NT_SUCCESS(status))
	{
		status = WdfRequestRetrieveOutputBuffer(reqRead,
			ReportBufferLen,
			&pReadReport,
			&bytesReturned);

		if (NT_SUCCESS(status))
		{
			//
			// Copy ReportBuffer into read request
			//

			if (bytesReturned > ReportBufferLen)
			{
				bytesReturned = ReportBufferLen;
			}

			RtlCopyMemory(pReadReport,
				ReportBuffer,
				bytesReturned);

			//
			// Complete read with the number of bytes returned as info
			//

			WdfRequestCompleteWithInformation(reqRead,
				status,
				bytesReturned);

			Da7219Print(DEBUG_LEVEL_INFO, DBG_IOCTL,
				"Da7219ProcessVendorReport %d bytes returned\n", bytesReturned);

			//
			// Return the number of bytes written for the write request completion
			//

			*BytesWritten = bytesReturned;

			Da7219Print(DEBUG_LEVEL_INFO, DBG_IOCTL,
				"%s completed, Queue:0x%p, Request:0x%p\n",
				DbgHidInternalIoctlString(IOCTL_HID_READ_REPORT),
				DevContext->ReportQueue,
				reqRead);
		}
		else
		{
			Da7219Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
				"WdfRequestRetrieveOutputBuffer failed Status 0x%x\n", status);
		}
	}
	else
	{
		Da7219Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfIoQueueRetrieveNextRequest failed Status 0x%x\n", status);
	}

	Da7219Print(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Da7219ProcessVendorReport Exit = 0x%x\n", status);

	return status;
}

NTSTATUS
Da7219ReadReport(
	IN PDA7219_CONTEXT DevContext,
	IN WDFREQUEST Request,
	OUT BOOLEAN* CompleteRequest
)
{
	NTSTATUS status = STATUS_SUCCESS;

	Da7219Print(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Da7219ReadReport Entry\n");

	//
	// Forward this read request to our manual queue
	// (in other words, we are going to defer this request
	// until we have a corresponding write request to
	// match it with)
	//

	status = WdfRequestForwardToIoQueue(Request, DevContext->ReportQueue);

	if (!NT_SUCCESS(status))
	{
		Da7219Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfRequestForwardToIoQueue failed Status 0x%x\n", status);
	}
	else
	{
		*CompleteRequest = FALSE;
	}

	Da7219Print(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Da7219ReadReport Exit = 0x%x\n", status);

	return status;
}

NTSTATUS
Da7219SetFeature(
	IN PDA7219_CONTEXT DevContext,
	IN WDFREQUEST Request,
	OUT BOOLEAN* CompleteRequest
)
{
	NTSTATUS status = STATUS_SUCCESS;
	WDF_REQUEST_PARAMETERS params;
	PHID_XFER_PACKET transferPacket = NULL;

	Da7219Print(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Da7219SetFeature Entry\n");

	WDF_REQUEST_PARAMETERS_INIT(&params);
	WdfRequestGetParameters(Request, &params);

	if (params.Parameters.DeviceIoControl.InputBufferLength < sizeof(HID_XFER_PACKET))
	{
		Da7219Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"Da7219SetFeature Xfer packet too small\n");

		status = STATUS_BUFFER_TOO_SMALL;
	}
	else
	{

		transferPacket = (PHID_XFER_PACKET)WdfRequestWdmGetIrp(Request)->UserBuffer;

		if (transferPacket == NULL)
		{
			Da7219Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
				"Da7219WriteReport No xfer packet\n");

			status = STATUS_INVALID_DEVICE_REQUEST;
		}
		else
		{
			//
			// switch on the report id
			//

			switch (transferPacket->reportId)
			{
			default:

				Da7219Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
					"Da7219SetFeature Unhandled report type %d\n", transferPacket->reportId);

				status = STATUS_INVALID_PARAMETER;

				break;
			}
		}
	}

	Da7219Print(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Da7219SetFeature Exit = 0x%x\n", status);

	return status;
}

NTSTATUS
Da7219GetFeature(
	IN PDA7219_CONTEXT DevContext,
	IN WDFREQUEST Request,
	OUT BOOLEAN* CompleteRequest
)
{
	NTSTATUS status = STATUS_SUCCESS;
	WDF_REQUEST_PARAMETERS params;
	PHID_XFER_PACKET transferPacket = NULL;

	Da7219Print(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Da7219GetFeature Entry\n");

	WDF_REQUEST_PARAMETERS_INIT(&params);
	WdfRequestGetParameters(Request, &params);

	if (params.Parameters.DeviceIoControl.OutputBufferLength < sizeof(HID_XFER_PACKET))
	{
		Da7219Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"Da7219GetFeature Xfer packet too small\n");

		status = STATUS_BUFFER_TOO_SMALL;
	}
	else
	{

		transferPacket = (PHID_XFER_PACKET)WdfRequestWdmGetIrp(Request)->UserBuffer;

		if (transferPacket == NULL)
		{
			Da7219Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
				"Da7219GetFeature No xfer packet\n");

			status = STATUS_INVALID_DEVICE_REQUEST;
		}
		else
		{
			//
			// switch on the report id
			//

			switch (transferPacket->reportId)
			{
			default:

				Da7219Print(DEBUG_LEVEL_ERROR, DBG_IOCTL,
					"Da7219GetFeature Unhandled report type %d\n", transferPacket->reportId);

				status = STATUS_INVALID_PARAMETER;

				break;
			}
		}
	}

	Da7219Print(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Da7219GetFeature Exit = 0x%x\n", status);

	return status;
}

PCHAR
DbgHidInternalIoctlString(
	IN ULONG IoControlCode
)
{
	switch (IoControlCode)
	{
	case IOCTL_HID_GET_DEVICE_DESCRIPTOR:
		return "IOCTL_HID_GET_DEVICE_DESCRIPTOR";
	case IOCTL_HID_GET_REPORT_DESCRIPTOR:
		return "IOCTL_HID_GET_REPORT_DESCRIPTOR";
	case IOCTL_HID_READ_REPORT:
		return "IOCTL_HID_READ_REPORT";
	case IOCTL_HID_GET_DEVICE_ATTRIBUTES:
		return "IOCTL_HID_GET_DEVICE_ATTRIBUTES";
	case IOCTL_HID_WRITE_REPORT:
		return "IOCTL_HID_WRITE_REPORT";
	case IOCTL_HID_SET_FEATURE:
		return "IOCTL_HID_SET_FEATURE";
	case IOCTL_HID_GET_FEATURE:
		return "IOCTL_HID_GET_FEATURE";
	case IOCTL_HID_GET_STRING:
		return "IOCTL_HID_GET_STRING";
	case IOCTL_HID_ACTIVATE_DEVICE:
		return "IOCTL_HID_ACTIVATE_DEVICE";
	case IOCTL_HID_DEACTIVATE_DEVICE:
		return "IOCTL_HID_DEACTIVATE_DEVICE";
	case IOCTL_HID_SEND_IDLE_NOTIFICATION_REQUEST:
		return "IOCTL_HID_SEND_IDLE_NOTIFICATION_REQUEST";
	case IOCTL_HID_SET_OUTPUT_REPORT:
		return "IOCTL_HID_SET_OUTPUT_REPORT";
	case IOCTL_HID_GET_INPUT_REPORT:
		return "IOCTL_HID_GET_INPUT_REPORT";
	default:
		return "Unknown IOCTL";
	}
}