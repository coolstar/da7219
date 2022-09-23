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
		//set sane defaults from Linux driver

		//Set sample rate
		da7219_reg_write(pDevice, DA7219_SR, DA7219_SR_48000);
		//Set PLL
		da7219_reg_write(pDevice, DA7219_PLL_CTRL, DA7219_PLL_MODE_SRM | DA7219_PLL_INDIV_9_TO_18_MHZ | DA7219_PLL_INDIV_4_5_TO_9_MHZ);
		da7219_reg_write(pDevice, DA7219_PLL_FRAC_TOP, 0x1E & DA7219_PLL_FBDIV_FRAC_TOP_MASK);
		da7219_reg_write(pDevice, DA7219_PLL_FRAC_BOT, 0xB8 & DA7219_PLL_FBDIV_FRAC_BOT_MASK);
		da7219_reg_write(pDevice, DA7219_PLL_INTEGER, 0x28 & DA7219_PLL_FBDIV_INTEGER_MASK);

		da7219_reg_write(pDevice, DA7219_DIG_ROUTING_DAI, 0);
		da7219_reg_write(pDevice, DA7219_DAI_CTRL, DA7219_DAI_FORMAT_I2S | (2 << DA7219_DAI_CH_NUM_SHIFT) | DA7219_DAI_EN_MASK);
		da7219_reg_write(pDevice, DA7219_DAI_TDM_CTRL, 0);

		da7219_reg_write(pDevice, DA7219_MIXIN_L_SELECT, DA7219_MIXIN_L_MIX_SELECT_MASK);
		da7219_reg_write(pDevice, DA7219_MIXIN_L_GAIN, 0xA);

		da7219_reg_write(pDevice, DA7219_MIC_1_GAIN, 0x5);

		da7219_reg_write(pDevice, DA7219_CP_CTRL, 0xE0);
		da7219_reg_write(pDevice, DA7219_HP_L_GAIN, 0x3F);
		da7219_reg_write(pDevice, DA7219_HP_R_GAIN, 0x3F);

		da7219_reg_write(pDevice, DA7219_MIXOUT_L_SELECT, DA7219_MIXOUT_L_MIX_SELECT_MASK);
		da7219_reg_write(pDevice, DA7219_MIXOUT_R_SELECT, DA7219_MIXOUT_R_MIX_SELECT_MASK);

		da7219_reg_write(pDevice, DA7219_MICBIAS_CTRL, 0x0D);
		da7219_reg_write(pDevice, DA7219_MIC_1_CTRL, 0);
		da7219_reg_write(pDevice, DA7219_MIXIN_L_CTRL, DA7219_MIXIN_L_AMP_RAMP_EN_MASK);
		da7219_reg_write(pDevice, DA7219_ADC_L_CTRL, DA7219_ADC_L_RAMP_EN_MASK);

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

	LARGE_INTEGER Interval;
	Interval.QuadPart = -10 * 1000 * 100;
	KeDelayExecutionThread(KernelMode, false, &Interval);

	DbgPrint("DA7219 dump:\n");
	for (int i = 0; i <= 0xff;) {
		DbgPrint("%02x: ", i);
		for (int j = 0; j <= 0xf; j++, i++) {
			unsigned int reg;
			da7219_reg_read(pDevice, i, &reg);
			DbgPrint("%02x ", reg);
		}
		DbgPrint("\n");
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

	UNREFERENCED_PARAMETER(OutputBufferLength);
	UNREFERENCED_PARAMETER(InputBufferLength);

	device = WdfIoQueueGetDevice(Queue);
	devContext = GetDeviceContext(device);

	switch (IoControlCode)
	{
	default:
		status = STATUS_NOT_SUPPORTED;
		break;
	}

	WdfRequestComplete(Request, status);

	return;
}