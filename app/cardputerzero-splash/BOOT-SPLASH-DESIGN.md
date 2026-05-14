# CardputerZero 最快亮屏方案设计

## 目标

通电后以最短时间在 ST7789V SPI LCD 上显示内容（splash）。

## 硬件约束

- LCD: ST7789V 170x320, SPI0 CS0 (GPIO8), DC=GPIO25, 50MHz
- 背光: PWM 通过 I2C1 → py32io16 (addr 0x4F) → channel 3
- 背光不是简单 GPIO，必须 I2C 通信才能点亮
- 目标平台: Raspberry Pi Zero 2W / CM Zero (BCM2710, Cortex-A53, aarch64)

## RPi 启动时序参考

```
通电 → SoC ROM → bootcode.bin (GPU, ~1s) → start.elf (GPU, ~1-2s) → kernel8.img (ARM)
```

总计到 ARM 开始执行: ~2-3 秒（不可压缩，官方闭源固件）

---

## 互联网调研方案汇总

### 方案 A: GPU 固件 splash (config.txt disable_splash)

- 原理: RPi 固件 `start.elf` 可在加载内核前显示 splash
- 亮屏时间: ~1s
- **不可用原因**: 仅支持 HDMI/DSI 输出，不支持 SPI 外接屏
- 参考: RPi 官方文档 config.txt

### 方案 B: LibreRPi/lk-overlay (VPU 级)

- 项目: https://github.com/librerpi/lk-overlay (~100 stars)
- 原理: 替换 `bootcode.bin`，在 VideoCore GPU 上跑 Little Kernel
- SPI 寄存器定义已有但标记 "untested"，I2C 驱动已可用
- 理论亮屏: 通电后 500ms-1s（SDRAM 初始化完成后即可）
- **风险**: 替换官方固件，实验性质，Pi Zero 2W 适配不确定
- **本质**: 半逆向（VPU ISA 社区逆向，外设寄存器有公开文档）
- 仓库已 clone 到: `/Users/eggfly/github/CardputerZero/lk-overlay`

### 方案 C: Circle 裸机 chainloader（当前选择）

- 项目: https://github.com/rsta2/circle (~2244 stars)
- 原理: `start.elf` 加载 Circle 裸机程序作为 `kernel8.img`，显示 splash 后跳转 Linux
- 已有: BCM2835 SPI master、I2C master、ST7789 驱动、chainboot 机制
- 预估亮屏: 通电后 2-3s（固件 ~2s + Circle SPI init 即刻）
- **优势**: 不碰官方固件，出问题换回 kernel8.img 即可恢复
- 仓库已 clone 到: `/Users/eggfly/github/CardputerZero/circle`

### 方案 D: initramfs 用户空间 SPI 直刷

- 原理: 在 initramfs 里放 userspace 程序，用 spidev + i2c-dev 直接操作硬件
- 预估亮屏: 通电后 4-6s
- 缺点: 需要内核加载 spi-bcm2835 + i2c-bcm2835 后才能操作

### 方案 E: Linux 内核 builtin 驱动

- 原理: 将 st7789v + spi-bcm2835 + i2c-bcm2835 + py32ioexp 编译为 `=y`
- config.txt 优化: `boot_delay=0`, `disable_splash=1`, `initial_turbo=30`
- 预估亮屏: 通电后 4-6s
- **优势**: 最稳定，无 chainboot 风险
- 缺点: 受限于 Linux 内核启动时间，无法再压缩

### 方案 F: armstub8.bin 自定义

- 原理: 在 ARM 启动最早阶段（EL3/EL2）执行自定义代码
- **不可行**: 此时无 cache/MMU，SPI bit-bang 极慢，无法实际刷屏

### 方案 G: fbcp-ili9341（用户空间极速 SPI）

- 项目: https://github.com/juj/fbcp-ili9341 (~1795 stars)
- 原理: 直接访问 BCM2835 SPI 寄存器 + DMA，绕过内核 SPI 驱动
- SPI 实际可跑 50-70MHz
- 缺点: userspace daemon，必须等 init 进程启动后才运行

---

## 方案对比

| 方案 | 亮屏时间 | 稳定性 | 复杂度 | 可行性 |
|------|---------|--------|--------|--------|
| A. GPU firmware splash | ~1s | 高 | 低 | **不可用**（SPI 不支持） |
| B. lk-overlay (VPU) | <1s | 低 | 很高 | 实验性，高风险 |
| **C. Circle chainloader** | **2-3s** | **中** | **中** | **当前实现** |
| D. initramfs userspace | 4-6s | 高 | 中 | 可行 |
| E. Linux builtin 驱动 | 4-6s | 高 | 低 | 可行，作为保底 |
| F. armstub8.bin | — | — | — | **不可行** |
| G. fbcp-ili9341 | 8-10s | 高 | 低 | 太晚 |

---

## 当前实现: Circle Chainloader (方案 C)

### 启动流程

```
通电 → bootcode.bin → start.elf → kernel8.img (Circle splash, 106KB)
  1. I2C1 → py32io16 (0x4F) → PWM ch3 enable → 背光 ON    (~5ms)
  2. SPI0 → ST7789V init sequence → display splash         (~150ms)
  3. FAT32 读取 kernel8.img.linux → 加载到 0x02000000
  4. Cache clean → MMU off → x0=DTB → jump to 0x80000 (Linux)
```

### SD 卡文件布局

```
bootfs/
├── bootcode.bin          (官方固件)
├── start.elf             (官方固件)
├── config.txt            (kernel=kernel8.img, arm_64bit=1)
├── kernel8.img           ← Circle splash 程序 (106KB)
├── kernel8.img.linux     ← 真正的 Linux 内核 (renamed)
├── bcm2710-rpi-zero-2-w.dtb
└── overlays/
    └── cardputerzero-overlay.dtbo
```

### 跳转 Linux 的两种子方案

#### 子方案 C1: 直接跳转（当前实现）

Circle 程序内直接跳转到 Linux 内核:
1. 从 SD FAT32 读 Linux kernel 到高地址 (0x02000000)
2. Clean/invalidate 所有 cache
3. 关闭 MMU
4. 设置 x0=DTB 地址, x1-x3=0
5. 将 kernel 复制到 0x80000 并跳转

**优点**: 快速，不需要二次启动
**风险**: cache/EL 状态可能导致 Linux 不稳定

#### 子方案 C2: Reboot 切换

Circle 程序显示 splash 后 reboot:
1. 固件第二次启动时加载 Linux 内核
2. 使用某种标记避免死循环

**优点**: Linux 拿到的是干净状态，零兼容性风险
**缺点**: 多一次 reboot (~2-3s)，需要标记机制

### 稳定性对比

| 维度 | C1 直接跳转 | C2 Reboot |
|------|------------|-----------|
| 速度 | +200ms | +2-3s |
| DTB 传递 | 需自行保存/传递 x0 | 固件自动处理 |
| EL2/EL1 | Circle 降到 EL1，Linux 无 KVM | 固件保证 EL2 |
| Cache 状态 | 需手动 clean+invalidate | 干净 |
| 外设状态 | SPI/I2C 被用过（Linux 会重新 init） | 完全干净 |
| 失败后果 | 可能 kernel panic | 最多 splash 不显示 |
| 实现复杂度 | 高（汇编 trampoline） | 低 |

---

## 跳转 Linux 的关键风险详解

### 1. EL2 → EL1 (Exception Level)

- ARM64 Linux 偏好从 EL2 进入（支持 KVM 虚拟化）
- Circle 启动时从 EL2 切换到 EL1 运行
- 一旦降到 EL1 **无法回到 EL2**（除非 reset）
- CardputerZero 不需要 KVM，EL1 进入 Linux 可接受

### 2. DTB (Device Tree Blob) 传递

- Linux ARM64 boot protocol: x0 = DTB 物理地址
- RPi 固件将 DTB 地址写入内存 0xF8 位置（ARM_DTB_PTR32）
- Circle 启动时 x0 被覆盖，但地址仍在 0xF8
- 当前实现: 从 0xF8 读取 DTB 地址，传入 x0

### 3. Cache / MMU 清理

- Linux 要求: MMU off, D-cache off, I-cache 可以 on
- 当前实现: set/way 遍历所有 cache level 做 clean+invalidate
- 然后 TLBI + 关闭 SCTLR_EL1 的 M 和 C 位
- 风险: 多核环境下 set/way 操作可能不完全（Pi Zero 2W 是 4 核）

### 4. 内存布局

- Circle 和 Linux 都在 0x80000
- 当前实现: Linux 先加载到 0x02000000，trampoline 复制到 0x80000 再跳转
- trampoline 代码本身不在 0x80000 区域，不会被覆盖

### 5. 外设残留状态

- SPI/I2C 被 Circle 初始化过
- Linux 的 spi-bcm2835 / i2c-bcm2835 驱动会完全重新初始化
- 风险: 极低，Linux 驱动设计为从任意状态恢复

### 6. GPU mailbox

- Circle 未使用 GPU mailbox（我们没有 HDMI 初始化）
- VideoCore 固件独立运行，不受 ARM 侧影响
- 风险: 无

---

## 后续计划

1. **实机测试 C1 方案**（直接跳转）
2. 如果 Linux 启动不稳定，切换到 C2 方案（reboot）
3. 长期: 评估方案 B（lk-overlay VPU）的可行性，理论上可做到 <1s
4. 保底: 方案 E（builtin 驱动）始终可用，4-5s 亮屏

---

## 参考项目

- Circle: https://github.com/rsta2/circle
- lk-overlay: https://github.com/librerpi/lk-overlay
- rpi-open-firmware: https://github.com/librerpi/rpi-open-firmware
- fbcp-ili9341: https://github.com/juj/fbcp-ili9341
- ARM64 Linux boot protocol: Documentation/arm64/booting.rst
- BCM2835 peripherals datasheet: SPI (0x204000), I2C (0x804000)
