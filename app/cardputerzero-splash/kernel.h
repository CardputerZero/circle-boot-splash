#ifndef _kernel_h
#define _kernel_h

#include <circle/actled.h>
#include <circle/koptions.h>
#include <circle/devicenameservice.h>
#include <circle/serial.h>
#include <circle/exceptionhandler.h>
#include <circle/interrupt.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/spimaster.h>
#include <circle/i2cmaster.h>
#include <circle/gpiopin.h>
#include <circle/dmachannel.h>
#include "../circle/addon/SDCard/emmc.h"
#include <circle/types.h>

enum TShutdownMode
{
	ShutdownNone,
	ShutdownHalt,
	ShutdownReboot
};

class CKernel
{
public:
	CKernel (void);
	~CKernel (void);

	boolean Initialize (void);
	TShutdownMode Run (void);

private:
	boolean InitBacklight (void);
	boolean ShowSplash (void);

	void SendCommand (u8 nCmd);
	void SendData8 (u8 nData);

private:
	CActLED			m_ActLED;
	CKernelOptions		m_Options;
	CDeviceNameService	m_DeviceNameService;
	CSerialDevice		m_Serial;
	CExceptionHandler	m_ExceptionHandler;
	CInterruptSystem	m_Interrupt;
	CTimer			m_Timer;
	CLogger			m_Logger;

	CSPIMaster		m_SPIMaster;
	CI2CMaster		m_I2CMaster;
	CGPIOPin		m_DCPin;
	CEMMCDevice		m_EMMC;
};

#endif
