#include "py32io16.h"
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/string.h>

static const char FromPY32[] = "py32io16";

u8 s_readback[4] = {0};

CPY32IO16::CPY32IO16 (CI2CMaster *pI2CMaster)
:	m_pI2CMaster (pI2CMaster)
{
}

CPY32IO16::~CPY32IO16 (void)
{
}

boolean CPY32IO16::WriteReg (u8 nReg, u8 nValue)
{
	u8 buf[2] = { nReg, nValue };
	int nResult = m_pI2CMaster->Write (PY32IO16_I2C_ADDR, buf, sizeof buf);
	if (nResult != (int) sizeof buf)
		return FALSE;

	// Small delay between writes (py32io16 needs processing time)
	CTimer::SimpleusDelay (1000);
	return TRUE;
}

u8 CPY32IO16::ReadReg (u8 nReg)
{
	u8 reg = nReg;
	u8 val = 0;
	m_pI2CMaster->Write (PY32IO16_I2C_ADDR, &reg, 1);
	CTimer::SimpleusDelay (100);
	m_pI2CMaster->Read (PY32IO16_I2C_ADDR, &val, 1);
	return val;
}

boolean CPY32IO16::EnableBacklight (unsigned nDuty)
{
	if (nDuty > 4095)
		nDuty = 4095;

	// CRITICAL: Set GPIO drive mode to push-pull (default is open-drain!)
	// Without this, PWM output cannot drive MOSFET gate high on cold boot.
	// Linux driver does this in py32io_gpio_setup() → GPIO_DRV_H = 0x00
	WriteReg (0x13, 0x00);  // GPIO_DRV_L: all push-pull
	WriteReg (0x14, 0x00);  // GPIO_DRV_H: all push-pull (includes IO10/PWM4)

	// Disable PWM4 first (clear enable bit)
	WriteReg (PY32IO_PWM4_H, 0x00);
	CTimer::SimpleMsDelay (10);

	// Set PWM frequency: 500Hz (0x01F4)
	WriteReg (PY32IO_PWM_FREQ_L, 0xF4);
	WriteReg (PY32IO_PWM_FREQ_H, 0x01);

	// Set duty low byte first
	u8 dutyL = nDuty & 0xFF;
	WriteReg (PY32IO_PWM4_L, dutyL);
	CTimer::SimpleMsDelay (10);

	// Then enable with duty high + enable bit
	u8 dutyH = ((nDuty >> 8) & 0x0F) | PY32IO_PWM_EN_BIT;
	WriteReg (PY32IO_PWM4_H, dutyH);
	CTimer::SimpleMsDelay (50);

	// Readback verification
	u8 readL = ReadReg (PY32IO_PWM4_L);
	u8 readH = ReadReg (PY32IO_PWM4_H);
	u8 readFL = ReadReg (PY32IO_PWM_FREQ_L);
	u8 readFH = ReadReg (PY32IO_PWM_FREQ_H);

	// Write readback values to log via a global buffer
	// (will be logged by caller)
	s_readback[0] = readFL;
	s_readback[1] = readFH;
	s_readback[2] = readL;
	s_readback[3] = readH;

	return (readH & PY32IO_PWM_EN_BIT) != 0;
}
