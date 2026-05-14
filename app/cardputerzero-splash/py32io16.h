#ifndef _py32io16_h
#define _py32io16_h

#include <circle/i2cmaster.h>
#include <circle/types.h>

// py32io16 I2C address
#define PY32IO16_I2C_ADDR	0x4F

// PWM registers (hwpwm=3 in DTS → PWM4 registers)
#define PY32IO_PWM4_L		0x21
#define PY32IO_PWM4_H		0x22
#define PY32IO_PWM_FREQ_L	0x25
#define PY32IO_PWM_FREQ_H	0x26

// PWM control bits
#define PY32IO_PWM_EN_BIT	(1 << 7)

// Readback buffer for diagnostics
extern u8 s_readback[4];

class CPY32IO16
{
public:
	CPY32IO16 (CI2CMaster *pI2CMaster);
	~CPY32IO16 (void);

	// Enable backlight PWM on channel 3 with given duty (0-4095)
	boolean EnableBacklight (unsigned nDuty);

	u8 ReadReg (u8 nReg);

private:
	boolean WriteReg (u8 nReg, u8 nValue);

private:
	CI2CMaster *m_pI2CMaster;
};

#endif
