#include "kernel.h"
#include "py32io16.h"
#include "splash_image.h"
#include <circle/util.h>
#include <fatfs/ff.h>
#include <circle/string.h>
#include <assert.h>

// Hardware configuration for CardputerZero
#define SPI_MASTER_DEVICE	0
#define SPI_CLOCK_SPEED		50000000	// 50 MHz
#define SPI_CPOL		0
#define SPI_CPHA		0
#define SPI_CHIP_SELECT		0

#define LCD_DC_PIN		25

#define I2C_DEVICE		1		// I2C1 (GPIO2/GPIO3)
#define I2C_FAST_MODE		FALSE		// 100KHz standard mode (py32io16 may not handle 400K at cold boot)

#define BACKLIGHT_DUTY		3000		// out of 4095 (~73%)


static const char FromKernel[] = "splash";

CKernel::CKernel (void)
:	m_Serial (&m_Interrupt, TRUE),
	m_Timer (&m_Interrupt),
	m_Logger (m_Options.GetLogLevel (), &m_Timer),
	m_SPIMaster (SPI_CLOCK_SPEED, SPI_CPOL, SPI_CPHA, SPI_MASTER_DEVICE),
	m_I2CMaster (I2C_DEVICE, I2C_FAST_MODE),
	m_DCPin (LCD_DC_PIN, GPIOModeOutput),
	m_EMMC (&m_Interrupt, &m_Timer, &m_ActLED)
{
}

CKernel::~CKernel (void)
{
}

static FATFS s_LogFS;
static boolean s_bLogMounted = FALSE;

static void LogStep (const char *pMsg)
{
	if (!s_bLogMounted)
	{
		if (f_mount (&s_LogFS, "SD:", 1) == FR_OK)
			s_bLogMounted = TRUE;
		else
			return;
	}

	// Get uptime in ms from ARM system timer
	unsigned nUptime = CTimer::GetClockTicks () / (CLOCKHZ / 1000);

	FIL file;
	if (f_open (&file, "SD:/SPLASH.LOG", FA_WRITE | FA_OPEN_ALWAYS) == FR_OK)
	{
		f_lseek (&file, f_size (&file));
		UINT bw;
		CString line;
		line.Format ("[%5ums] %s\n", nUptime, pMsg);
		f_write (&file, (const char *) line, line.GetLength (), &bw);
		f_sync (&file);
		f_close (&file);
	}
}

boolean CKernel::Initialize (void)
{
	boolean bOK = TRUE;

	if (bOK)
		bOK = m_Serial.Initialize (115200);

	if (bOK)
		bOK = m_Interrupt.Initialize ();

	if (bOK)
		bOK = m_Timer.Initialize ();

	if (bOK)
		bOK = m_Logger.Initialize (&m_Serial);

	if (bOK)
		bOK = m_EMMC.Initialize ();

	if (bOK)
	{
		LogStep ("INIT: before I2C init");
		bOK = m_I2CMaster.Initialize ();
		LogStep (bOK ? "INIT: I2C OK" : "INIT: I2C FAILED");
	}

	if (bOK)
	{
		LogStep ("INIT: before SPI init");
		bOK = m_SPIMaster.Initialize ();
		LogStep (bOK ? "INIT: SPI OK" : "INIT: SPI FAILED");
	}

	LogStep (bOK ? "INIT: all OK" : "INIT: FAILED");
	return bOK;
}

boolean CKernel::InitBacklight (void)
{
	CPY32IO16 py32 (&m_I2CMaster);

	boolean bOK = py32.EnableBacklight (BACKLIGHT_DUTY);

	// Log readback values to SPLASH.LOG for diagnostics
	CString Msg;
	Msg.Format ("BL: ok=%d readback: FREQ=%02X%02X PWM4=%02X%02X",
		    bOK, s_readback[1], s_readback[0], s_readback[3], s_readback[2]);
	LogStep ((const char *) Msg);

	return bOK;
}

boolean CKernel::ShowSplash (void)
{
	// Use custom init matching m5stack st7789v driver (HSD20_IPS mode)
	// instead of Circle's default st7789display init sequence.
	//
	// Reference: m5stack-linux-dtoverlays/modules/st7789v-1.0/st7789v_m5stack.c
	// DTS config: 170x320, rotate=90, ram-y-offset=35, spi 50MHz

	// Software reset
	SendCommand (0x01);
	CTimer::SimpleMsDelay (5);

	// Exit sleep
	SendCommand (0x11);
	CTimer::SimpleMsDelay (5);

	// Pixel format: 16bit RGB565
	SendCommand (0x3A);
	SendData8 (0x05);

	// Porch control (PORCTRL 0xB2)
	SendCommand (0xB2);
	SendData8 (0x05);
	SendData8 (0x05);
	SendData8 (0x00);
	SendData8 (0x33);
	SendData8 (0x33);

	// Gate control (GCTRL 0xB7): VGH=13.26V, VGL=-10.43V
	SendCommand (0xB7);
	SendData8 (0x75);

	// VCOM setting (VCOMS 0xBB)
	SendCommand (0xBB);
	SendData8 (0x22);

	// VDV and VRH enable (VDVVRHEN 0xC2)
	SendCommand (0xC2);
	SendData8 (0x01);
	SendData8 (0xFF);

	// VRH set (VRHS 0xC3)
	SendCommand (0xC3);
	SendData8 (0x13);

	// VDV set (VDVS 0xC4)
	SendCommand (0xC4);
	SendData8 (0x20);

	// VCOM offset (VCMOFSET 0xC5)
	SendCommand (0xC5);
	SendData8 (0x20);

	// Power control 1 (PWCTRL1 0xD0)
	SendCommand (0xD0);
	SendData8 (0xA4);
	SendData8 (0xA1);

	// Positive gamma (PVGAMCTRL 0xE0) - HSD20_IPS
	SendCommand (0xE0);
	SendData8 (0xD0); SendData8 (0x05); SendData8 (0x0A); SendData8 (0x09);
	SendData8 (0x08); SendData8 (0x05); SendData8 (0x2E); SendData8 (0x44);
	SendData8 (0x45); SendData8 (0x0F); SendData8 (0x17); SendData8 (0x16);
	SendData8 (0x2B); SendData8 (0x33);

	// Negative gamma (NVGAMCTRL 0xE1) - HSD20_IPS
	SendCommand (0xE1);
	SendData8 (0xD0); SendData8 (0x05); SendData8 (0x0A); SendData8 (0x09);
	SendData8 (0x08); SendData8 (0x05); SendData8 (0x2E); SendData8 (0x43);
	SendData8 (0x45); SendData8 (0x0F); SendData8 (0x16); SendData8 (0x16);
	SendData8 (0x2B); SendData8 (0x33);

	// Invert display (required for HSD20_IPS panel)
	SendCommand (0x21);

	// Display ON
	SendCommand (0x29);

	// MADCTL: rotate 90 degrees (MV=0x20 | MY=0x80)
	SendCommand (0x36);
	SendData8 (0xA0);

	// Set address window (with ram-y-offset=35)
	// Column: 0 to 319
	SendCommand (0x2A);
	SendData8 (0x00); SendData8 (0x00);
	SendData8 (0x01); SendData8 (0x3F);

	// Row: 35 to 35+169 = 204
	SendCommand (0x2B);
	SendData8 (0x00); SendData8 (35);
	SendData8 (0x00); SendData8 (35 + 170 - 1);

	// Write memory
	SendCommand (0x2C);

	// Send splash image (320x170 RGB565 BE)
	m_DCPin.Write (HIGH);
	const u16 *pImg = s_SplashImage;
	for (unsigned y = 0; y < SPLASH_HEIGHT; y++)
	{
		m_SPIMaster.Write (SPI_CHIP_SELECT, &pImg[y * SPLASH_WIDTH],
				   SPLASH_WIDTH * sizeof (u16));
	}

	m_Logger.Write (FromKernel, LogNotice, "Splash displayed (320x170)");
	return TRUE;
}


TShutdownMode CKernel::Run (void)
{
	LogStep ("STEP0: Run() entered");

	LogStep ("STEP1: init backlight");

	InitBacklight ();
	LogStep ("STEP2: backlight done");

	ShowSplash ();
	LogStep ("STEP3: after ShowSplash");

	// Swap kernel files: put Linux as kernel8.img, then reboot
	// Firmware will load Linux directly on next boot (clean EL2, DTB, everything)
	LogStep ("STEP4: swapping kernel files");
	f_rename ("SD:/kernel8.img", "SD:/kernel8-splash.bak");
	FRESULT renFr = f_rename ("SD:/kernel8.img.linux", "SD:/kernel8.img");
	if (renFr != FR_OK)
	{
		CString errMsg;
		errMsg.Format ("STEP4: rename failed (%d)", renFr);
		LogStep ((const char *) errMsg);
		return ShutdownHalt;
	}
	LogStep ("STEP5: files swapped, rebooting to Linux");

	return ShutdownReboot;
}

void CKernel::SendCommand (u8 nCmd)
{
	m_DCPin.Write (LOW);
	m_SPIMaster.Write (SPI_CHIP_SELECT, &nCmd, 1);
}

void CKernel::SendData8 (u8 nData)
{
	m_DCPin.Write (HIGH);
	m_SPIMaster.Write (SPI_CHIP_SELECT, &nData, 1);
}
