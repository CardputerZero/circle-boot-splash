# CardputerZero Boot Splash

Bare metal (Circle framework) early boot splash for M5Stack CardputerZero.

Shows a splash screen on ST7789V SPI LCD **~2.3 seconds after power-on**, then reboots into Linux.

## How It Works

```
Power on
  → GPU firmware (bootcode.bin + start.elf) ~2.1s
  → ARM starts, loads kernel8.img (this program, 227KB)
  → I2C: py32io16 push-pull mode + PWM backlight ON         ~85ms
  → SPI: ST7789V init + display splash image                ~35ms
  → FAT32: rename kernel files                              ~50ms
  → Reboot
  → GPU firmware loads kernel8.img (now Linux kernel)
  → Linux boots normally (with overlays, DTB, etc.)
  → systemd service restores files for next cold boot
```

## Hardware

| Component | Connection | Details |
|-----------|-----------|---------|
| LCD | SPI0, CS0 (GPIO8), DC=GPIO25 | ST7789V 320×170, 50MHz |
| Backlight | I2C1 → py32io16 (0x4F) → PWM4 | Must set GPIO_DRV to push-pull first |
| Target | Raspberry Pi Zero 2W / CM Zero | aarch64, Cortex-A53 |

## Key Hardware Notes

- **py32io16 GPIO_DRV registers (0x13/0x14):** Default is open-drain (0xFF). Must write 0x00 (push-pull) before PWM can drive the MOSFET gate for backlight.
- **I2C speed:** Must use 100KHz standard mode. 400KHz is unreliable on cold boot.
- **PWM channel:** DTS `pwms = <&py32io16 3 ...>` → hwpwm=3 → registers 0x21/0x22 (PWM4).
- **ST7789V delays:** Datasheet says 120ms after sleep-out, but 5ms works fine in practice.
- **MADCTL for rotate 90°:** 0xA0 (MV|MY).
- **RAM Y offset:** 35 pixels.

## Building

Requires `aarch64-none-elf-` toolchain (ARM bare metal).

```bash
# 1. Clone Circle framework alongside this project
cd ..
git clone --depth=1 https://github.com/rsta2/circle.git

# 2. Configure and build Circle libraries
cd circle
./configure -r 3 -p aarch64-none-elf-
make -j$(nproc) -C lib
make -j$(nproc) -C lib/usb
make -j$(nproc) -C lib/input
make -j$(nproc) -C lib/fs
make -j$(nproc) -C addon/fatfs
make -j$(nproc) -C addon/SDCard

# 3. Build splash
cd ../circle-boot-splash
make
# Output: kernel8.img (~227KB)
```

## SD Card Setup

```
bootfs/
├── kernel8.img         ← This splash program (227KB)
├── kernel8.img.linux   ← Real Linux kernel (gzip compressed)
├── config.txt          ← Must have: arm_64bit=1
├── bcm2710-rpi-*.dtb
└── overlays/
    └── cardputerzero-overlay.dtbo
```

## Boot Cycle

```
Cold boot:
  kernel8.img (Circle) → show splash → rename files → reboot

After reboot:
  kernel8.img (now Linux) → normal boot → systemd restores files

Next cold boot:
  kernel8.img (Circle again) → splash → ...
```

### systemd Service (installed on Linux side)

```ini
# /etc/systemd/system/splash-restore.service
[Unit]
Description=Restore Circle splash kernel after boot
DefaultDependencies=no
After=boot-firmware.mount
Before=sysinit.target
ConditionPathExists=/boot/firmware/kernel8-splash.bak

[Service]
Type=oneshot
ExecStart=/bin/mv /boot/firmware/kernel8.img /boot/firmware/kernel8.img.linux
ExecStart=/bin/mv /boot/firmware/kernel8-splash.bak /boot/firmware/kernel8.img

[Install]
WantedBy=sysinit.target
```

## Timing (measured)

| Stage | Time | Notes |
|-------|------|-------|
| Power on → ARM start | 2112ms | GPU firmware, not compressible |
| I2C backlight init | +85ms | py32io16 push-pull + PWM |
| SPI LCD + splash | +35ms | ST7789V init + 320×170 image |
| File rename + reboot | +50ms | FAT32 rename on SD card |
| **Total to screen on** | **~2.26s** | |

## Splash Image

Replace `CARDPUTERZERO_UI.png` and regenerate `splash_image.h`:

```bash
python3 -c "
from PIL import Image
img = Image.open('CARDPUTERZERO_UI.png').convert('RGB')
pixels = list(img.getdata())
data = []
for r, g, b in pixels:
    rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    be = ((rgb565 >> 8) & 0xFF) | ((rgb565 & 0xFF) << 8)
    data.append(be)
with open('splash_image.h', 'w') as f:
    f.write('// Auto-generated from CARDPUTERZERO_UI.png (320x170 RGB565 BE)\n')
    f.write('#ifndef _splash_image_h\n#define _splash_image_h\n\n')
    f.write('#include <circle/types.h>\n\n')
    f.write('#define SPLASH_WIDTH 320\n#define SPLASH_HEIGHT 170\n\n')
    f.write('static const u16 s_SplashImage[SPLASH_WIDTH * SPLASH_HEIGHT] = {\n')
    for i in range(0, len(data), 16):
        f.write('    ' + ', '.join('0x%04X' % d for d in data[i:i+16]) + ',\n')
    f.write('};\n\n#endif\n')
"
```

## Why Not Chainboot?

We tried chainbooting Linux directly from Circle (avoiding the reboot). It failed due to:
1. **Compressed kernel:** `kernel8.img` on RPi is gzip-compressed; firmware auto-decompresses but `memcpy` doesn't.
2. **EL1 vs EL2:** Circle drops to EL1; returning to EL2 via `hvc #0` works but Linux still wouldn't boot.
3. **DTB issues:** Firmware generates DTB at runtime (with overlays, cmdline, memory info). Hard to replicate.
4. **Memory overlap:** 23MB uncompressed kernel copy overlaps source/destination addresses.

The reboot approach is 100% reliable because the firmware handles everything correctly on the second boot.
