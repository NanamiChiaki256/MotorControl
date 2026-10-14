#include "OLED.h"
#include "OLED_Font.h"
#include <stddef.h>
#include <string.h>

/* CubeMX must initialize I2C1 on PB8/PB9 before OLED_Init().
 * Only one task may call this driver after the scheduler starts.
 */
extern I2C_HandleTypeDef hi2c1;

#define OLED_ADDRESS       (0x3CU << 1)
#define OLED_TIMEOUT_MS    20U
#define OLED_PAYLOAD_MAX   32U

static HAL_StatusTypeDef oled_status = HAL_ERROR;

HAL_StatusTypeDef OLED_GetStatus(void)
{
    return oled_status;
}

static void OLED_Send(uint8_t control, const uint8_t *bytes, uint16_t count)
{
    uint8_t packet[OLED_PAYLOAD_MAX + 1U];
    HAL_StatusTypeDef result;

    if (oled_status != HAL_OK || bytes == NULL ||
        count == 0U || count > OLED_PAYLOAD_MAX) {
        return;
    }
    packet[0] = control;
    memcpy(&packet[1], bytes, count);
    result = HAL_I2C_Master_Transmit(&hi2c1, OLED_ADDRESS, packet,
                                   (uint16_t)(count + 1U), OLED_TIMEOUT_MS);
    if (result != HAL_OK) {
        oled_status = result;
    }
}

static void OLED_SetCursor(uint8_t page, uint8_t column)
{
    uint8_t commands[3];
    commands[0] = (uint8_t)(0xB0U | page);
    commands[1] = (uint8_t)(0x10U | (column >> 4));
    commands[2] = (uint8_t)(column & 0x0FU);
    OLED_Send(0x00U, commands, sizeof(commands));
}

void OLED_Clear(void)
{
    static const uint8_t zeros[32] = {0};
    uint8_t page;
    uint8_t block;

    for (page = 0U; page < 8U; ++page) {
        OLED_SetCursor(page, 0U);
        for (block = 0U; block < 4U; ++block) {
            OLED_Send(0x40U, zeros, sizeof(zeros));
        }
    }
}

void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char)
{
    uint8_t character = (uint8_t)Char;
    uint8_t page;
    uint8_t x;
    const uint8_t *glyph;

    if (Line < 1U || Line > 4U || Column < 1U || Column > 16U) {
        return;
    }
    if (character < 32U || character > 126U) {
        character = (uint8_t)'?';
    }
    glyph = OLED_F8x16[character - 32U];
    page = (uint8_t)((Line - 1U) * 2U);
    x = (uint8_t)((Column - 1U) * 8U);
    OLED_SetCursor(page, x);
    OLED_Send(0x40U, glyph, 8U);
    OLED_SetCursor((uint8_t)(page + 1U), x);
    OLED_Send(0x40U, &glyph[8], 8U);
}

void OLED_ShowString(uint8_t Line, uint8_t Column, const char *String)
{
    if (String == NULL || Line < 1U || Line > 4U ||
        Column < 1U || Column > 16U) {
        return;
    }
    while (*String != '\0' && Column <= 16U) {
        OLED_ShowChar(Line, Column, *String);
        ++String;
        ++Column;
    }
}

static void OLED_ShowUnsigned(uint8_t line, uint8_t column, uint32_t number,
                              uint8_t length, uint8_t base, uint8_t max_length)
{
    static const char digits[] = "0123456789ABCDEF";
    char text[17];
    uint8_t index;

    if (length == 0U || length > max_length) {
        return;
    }
    text[length] = '\0';
    for (index = length; index > 0U; --index) {
        text[index - 1U] = digits[number % base];
        number /= base;
    }
    OLED_ShowString(line, column, text);
}

void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
    OLED_ShowUnsigned(Line, Column, Number, Length, 10U, 10U);
}

void OLED_ShowSignedNum(uint8_t Line, uint8_t Column, int32_t Number, uint8_t Length)
{
    uint32_t magnitude;
    if (Line < 1U || Line > 4U || Column < 1U || Column > 15U ||
        Length == 0U || Length > 10U) {
        return;
    }
    if (Number < 0) {
        OLED_ShowChar(Line, Column, '-');
        magnitude = (uint32_t)(-(Number + 1)) + 1U;
    } else {
        OLED_ShowChar(Line, Column, '+');
        magnitude = (uint32_t)Number;
    }
    OLED_ShowNum(Line, (uint8_t)(Column + 1U), magnitude, Length);
}

void OLED_ShowHexNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
    OLED_ShowUnsigned(Line, Column, Number, Length, 16U, 8U);
}

void OLED_ShowBinNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
    OLED_ShowUnsigned(Line, Column, Number, Length, 2U, 16U);
}

void OLED_Init(void)
{
    /* Preserve the original 128x64 display orientation and contrast.
     * Explicitly select page addressing for OLED_SetCursor().
     */
    static const uint8_t commands[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
        0xA1, 0xC8, 0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1,
        0xDB, 0x30, 0xA4, 0xA6, 0x8D, 0x14, 0x20, 0x02, 0xAF
    };

    /* Call before starting FreeRTOS, with a working HAL millisecond timebase. */
    HAL_Delay(100U);
    oled_status = HAL_I2C_IsDeviceReady(&hi2c1, OLED_ADDRESS, 3U,
                                      OLED_TIMEOUT_MS);
    if (oled_status != HAL_OK) {
        return;
    }
    OLED_Send(0x00U, commands, sizeof(commands));
    OLED_Clear();
}
