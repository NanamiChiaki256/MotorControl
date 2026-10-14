# MotorControl

基于 **STM32F103C8T6** 的嵌入式学习工程，使用 STM32CubeMX、Keil MDK 和 STM32 HAL。

当前版本完成 **OLED 硬件 I2C 显示验证**，后续逐步加入 FreeRTOS、编码器测速、速度闭环与电流故障诊断。仓库名称表示项目目标，并不表示所有电机控制功能已经实现。

## 当前已完成

- CubeMX 生成的 STM32F103C8T6 HAL 工程。
- I2C1 重映射至 PB8（SCL）、PB9（SDA）。
- 原 GPIO 模拟 I2C / 标准外设库 OLED 驱动适配至 HAL 硬件 I2C。
- 128×64 OLED 使用 8×16 ASCII 字库显示文字和数字。
- 基础错误检测：初始化失败时进入 Error_Handler。
- 用户已在开发板验证显示 `HAL OLED OK` 与 `12345`。

**当前未启用 FreeRTOS，也未实现电机闭环或电流采集。**

## 硬件连接

| STM32 引脚 | 用途 |
|---|---|
| PB8 | OLED SCL / I2C1_SCL |
| PB9 | OLED SDA / I2C1_SDA |
| PA13 | ST-Link SWDIO |
| PA14 | ST-Link SWCLK |
| GND | 模块与调试器共地 |

OLED 逻辑电平应与 STM32 的 3.3V 逻辑匹配。显示驱动使用7位地址0x3C，HAL接口传入左移后的0x78。

## 开发环境

本次工程配置记录：

- STM32CubeMX 6.17.0。
- STM32Cube MCU Package for STM32F1 V1.8.7。
- Keil MDK / µVision 5；工程记录的编译器为 Arm Compiler 5.06 update 5。
- Keil.STM32F1xx_DFP 2.2.0。
- 当前系统时钟：内部 HSI 8MHz；HAL 时基：SysTick。
- ST-Link，SWD 调试。

使用其他版本工具时需确认编译器和器件包兼容性。没有捆绑工具安装包、开发工具许可证或任何账号凭据。

## 打开、编译与运行

1. 克隆仓库。
2. 用 Keil 打开 `MDK-ARM/MotorControl.uvprojx`。
3. 安装 STM32F1 器件支持包并选择可用的兼容编译器。
4. 编译工程，确认无错误。
5. 配置 ST-Link / SWD，连接开发板并下载。
6. 复位后 OLED 应显示第一行 `HAL OLED OK`，第二行 `12345`。

CubeMX 配置位于 `MotorControl.ioc`。再次生成代码前先保存或提交工作。
自定义代码应放在 `USER CODE BEGIN/END` 区域；当前 `main.c` 中的 `#include "OLED.h"` 位于该区域外，重新生成前应移入 `USER CODE BEGIN Includes`，避免被覆盖。
如果重新生成后 Keil 项目树中缺少 `OLED.c`，将其重新加入 Application/User/Core 组。

## 主要文件

- `MotorControl.ioc`：CubeMX 配置。
- `Core/Src/main.c`：初始化与OLED显示验收。
- `Core/Src/OLED.c`：HAL I2C OLED 驱动。
- `Core/Inc/OLED.h`：OLED显示接口。
- `Core/Inc/OLED_Font.h`：8×16 ASCII字库。
- `Core/Src/i2c.c`：I2C初始化及重映射配置。
- `Drivers/`：所需 STM32 HAL 与 CMSIS 驱动。
- `MDK-ARM/`：Keil工程及启动文件。

编译输出、个人 IDE 布局、CMSIS未使用的DSP示例/预编译库等由 `.gitignore` 排除，原文件仍保留在本地。

## 后续计划

- [ ] 启用 FreeRTOS，验证任务内 OLED 显示与计数。
- [ ] 使用两个任务和队列传递数据。
- [ ] 编码器测速与电机 PWM 输出。
- [ ] 速度 PI/PID 闭环、斜坡启动与限幅。
- [ ] INA226 电流与电压采集。
- [ ] 通信超时停机和故障记录。

## 来源与许可

HAL与CMSIS保留各自目录下的许可证及原始版权声明。
OLED字库来源于用户提供的STM32入门教程资料；OLED显示接口沿用教程形式，底层已适配为HAL I2C。
本仓库未对第三方内容重新授权；公开发布或另行分发前，应核实相关教程字库的许可。
