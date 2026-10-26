/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"
#include "i2c.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "OLED.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void MX_FREERTOS_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* 用CPU周期计数器产生约10us延时 */
static void INA_SoftDelay(void)
{
  uint32_t start = DWT->CYCCNT;
  uint32_t cycles = SystemCoreClock / 100000U;

  while ((uint32_t)(DWT->CYCCNT - start) < cycles)
  {
  }
}

/* 释放SCL，等待线路实际变高；超时返回0 */
static uint8_t INA_SCL_High(void)
{
  uint32_t startTick = HAL_GetTick();

  HAL_GPIO_WritePin(INA_SCL_GPIO_Port,
                    INA_SCL_Pin, GPIO_PIN_SET);

  while (HAL_GPIO_ReadPin(INA_SCL_GPIO_Port,
                          INA_SCL_Pin) == GPIO_PIN_RESET)
  {
    if ((uint32_t)(HAL_GetTick() - startTick) >= 2U)
    {
      return 0;
    }
  }

  INA_SoftDelay();
  return 1;
}

/* 发送七位地址，收到ACK返回1，否则返回0 */
static uint8_t INA_CheckAddress(uint8_t address)
{
  uint8_t data = (uint8_t)(address << 1);
  uint8_t i;
  uint8_t ack = 0;

  /* 总线空闲：两根线都释放 */
  HAL_GPIO_WritePin(INA_SDA_GPIO_Port,
                    INA_SDA_Pin, GPIO_PIN_SET);

  if (INA_SCL_High() == 0U)
  {
    return 0;
  }

  if (HAL_GPIO_ReadPin(INA_SDA_GPIO_Port,
                       INA_SDA_Pin) == GPIO_PIN_RESET)
  {
    return 0;
  }

  /* START：SCL高时，SDA由高变低 */
  HAL_GPIO_WritePin(INA_SDA_GPIO_Port,
                    INA_SDA_Pin, GPIO_PIN_RESET);
  INA_SoftDelay();

  HAL_GPIO_WritePin(INA_SCL_GPIO_Port,
                    INA_SCL_Pin, GPIO_PIN_RESET);
  INA_SoftDelay();

  /* 发送地址字节，最高位先发，最后一位为写方向0 */
  for (i = 0; i < 8U; i++)
  {
    HAL_GPIO_WritePin(INA_SDA_GPIO_Port,
                      INA_SDA_Pin,
                      (data & 0x80U) ? GPIO_PIN_SET
                                     : GPIO_PIN_RESET);
    INA_SoftDelay();

    if (INA_SCL_High() == 0U)
    {
      goto stop;
    }

    HAL_GPIO_WritePin(INA_SCL_GPIO_Port,
                      INA_SCL_Pin, GPIO_PIN_RESET);
    INA_SoftDelay();

    data = (uint8_t)(data << 1);
  }

  /* 第9个时钟：释放SDA，让从机发送ACK */
  HAL_GPIO_WritePin(INA_SDA_GPIO_Port,
                    INA_SDA_Pin, GPIO_PIN_SET);
  INA_SoftDelay();

  if (INA_SCL_High() == 0U)
  {
    goto stop;
  }

  ack = (HAL_GPIO_ReadPin(INA_SDA_GPIO_Port,
                          INA_SDA_Pin) == GPIO_PIN_RESET)
        ? 1U : 0U;

  HAL_GPIO_WritePin(INA_SCL_GPIO_Port,
                    INA_SCL_Pin, GPIO_PIN_RESET);
  INA_SoftDelay();

stop:
  /* STOP：SCL高时，SDA由低变高 */
  HAL_GPIO_WritePin(INA_SDA_GPIO_Port,
                    INA_SDA_Pin, GPIO_PIN_RESET);
  INA_SoftDelay();

  if (INA_SCL_High() == 0U)
  {
    HAL_GPIO_WritePin(INA_SDA_GPIO_Port,
                      INA_SDA_Pin, GPIO_PIN_SET);
    return 0;
  }

  HAL_GPIO_WritePin(INA_SDA_GPIO_Port,
                    INA_SDA_Pin, GPIO_PIN_SET);
  INA_SoftDelay();

  return ack;
}

/* 发送一个字节，收到ACK返回1 */
static uint8_t INA_SendByte(uint8_t data)
{
  uint8_t i;
  uint8_t ack;

  for (i = 0; i < 8U; i++)
  {
    HAL_GPIO_WritePin(INA_SDA_GPIO_Port, INA_SDA_Pin,
                      (data & 0x80U) ? GPIO_PIN_SET
                                     : GPIO_PIN_RESET);
    INA_SoftDelay();

    if (INA_SCL_High() == 0U)
    {
      return 0;
    }

    HAL_GPIO_WritePin(INA_SCL_GPIO_Port,
                      INA_SCL_Pin, GPIO_PIN_RESET);
    INA_SoftDelay();

    data = (uint8_t)(data << 1);
  }

  /* 释放SDA，读取第9个时钟上的ACK */
  HAL_GPIO_WritePin(INA_SDA_GPIO_Port,
                    INA_SDA_Pin, GPIO_PIN_SET);
  INA_SoftDelay();

  if (INA_SCL_High() == 0U)
  {
    return 0;
  }

  ack = (HAL_GPIO_ReadPin(INA_SDA_GPIO_Port,
                          INA_SDA_Pin) == GPIO_PIN_RESET)
        ? 1U : 0U;

  HAL_GPIO_WritePin(INA_SCL_GPIO_Port,
                    INA_SCL_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(INA_SDA_GPIO_Port,
                    INA_SDA_Pin, GPIO_PIN_SET);
  INA_SoftDelay();

  return ack;
}

/* 读取一个16位寄存器；成功返回1，失败返回0 */
 uint8_t INA_ReadRegister(uint8_t address,
                                uint8_t reg,
                                uint16_t *value)
{
  uint8_t bit;
  uint8_t success = 0;
  uint16_t data = 0;

  if (value == NULL)
  {
    return 0;
  }

  /* 检查空闲总线 */
  HAL_GPIO_WritePin(INA_SDA_GPIO_Port,
                    INA_SDA_Pin, GPIO_PIN_SET);

  if (INA_SCL_High() == 0U)
  {
    goto stop;
  }

  if (HAL_GPIO_ReadPin(INA_SDA_GPIO_Port,
                       INA_SDA_Pin) == GPIO_PIN_RESET)
  {
    goto stop;
  }

  /* START */
  HAL_GPIO_WritePin(INA_SDA_GPIO_Port,
                    INA_SDA_Pin, GPIO_PIN_RESET);
  INA_SoftDelay();
  HAL_GPIO_WritePin(INA_SCL_GPIO_Port,
                    INA_SCL_Pin, GPIO_PIN_RESET);
  INA_SoftDelay();

  /* 写方向地址，然后发送寄存器地址 */
  if (INA_SendByte((uint8_t)(address << 1)) == 0U ||
      INA_SendByte(reg) == 0U)
  {
    goto stop;
  }

  /* 重复START */
  HAL_GPIO_WritePin(INA_SDA_GPIO_Port,
                    INA_SDA_Pin, GPIO_PIN_SET);
  INA_SoftDelay();

  if (INA_SCL_High() == 0U)
  {
    goto stop;
  }

  HAL_GPIO_WritePin(INA_SDA_GPIO_Port,
                    INA_SDA_Pin, GPIO_PIN_RESET);
  INA_SoftDelay();
  HAL_GPIO_WritePin(INA_SCL_GPIO_Port,
                    INA_SCL_Pin, GPIO_PIN_RESET);
  INA_SoftDelay();

  /* 读方向地址 */
  if (INA_SendByte((uint8_t)((address << 1) | 1U)) == 0U)
  {
    goto stop;
  }

  /* 接收16位数据：高字节在前 */
  HAL_GPIO_WritePin(INA_SDA_GPIO_Port,
                    INA_SDA_Pin, GPIO_PIN_SET);
  INA_SoftDelay();

  for (bit = 0; bit < 16U; bit++)
  {
    if (INA_SCL_High() == 0U)
    {
      goto stop;
    }

    data = (uint16_t)((data << 1) |
           ((HAL_GPIO_ReadPin(INA_SDA_GPIO_Port,
                              INA_SDA_Pin) == GPIO_PIN_SET)
            ? 1U : 0U));

    HAL_GPIO_WritePin(INA_SCL_GPIO_Port,
                      INA_SCL_Pin, GPIO_PIN_RESET);
    INA_SoftDelay();

    /* 高字节后发ACK，低字节后发NACK */
    if (bit == 7U || bit == 15U)
    {
      HAL_GPIO_WritePin(INA_SDA_GPIO_Port, INA_SDA_Pin,
                        (bit == 7U) ? GPIO_PIN_RESET
                                    : GPIO_PIN_SET);
      INA_SoftDelay();

      if (INA_SCL_High() == 0U)
      {
        goto stop;
      }

      HAL_GPIO_WritePin(INA_SCL_GPIO_Port,
                        INA_SCL_Pin, GPIO_PIN_RESET);
      INA_SoftDelay();
      HAL_GPIO_WritePin(INA_SDA_GPIO_Port,
                        INA_SDA_Pin, GPIO_PIN_SET);
      INA_SoftDelay();
    }
  }

  success = 1;

stop:
  /* 成功或失败都尝试产生STOP并释放线路 */
  HAL_GPIO_WritePin(INA_SCL_GPIO_Port,
                    INA_SCL_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(INA_SDA_GPIO_Port,
                    INA_SDA_Pin, GPIO_PIN_RESET);
  INA_SoftDelay();

  if (INA_SCL_High() == 0U)
  {
    HAL_GPIO_WritePin(INA_SDA_GPIO_Port,
                      INA_SDA_Pin, GPIO_PIN_SET);
    return 0;
  }

  HAL_GPIO_WritePin(INA_SDA_GPIO_Port,
                    INA_SDA_Pin, GPIO_PIN_SET);
  INA_SoftDelay();

  if (success != 0U)
  {
    *value = data;
  }

  return success;
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  uint8_t inaAddress;
  uint16_t inaMfr = 0;
  uint16_t inaDie = 0;
  uint16_t inaBusRaw = 0;
  uint32_t inaBusmV = 0;
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_USART1_UART_Init();
  MX_TIM3_Init();
  MX_TIM2_Init();
  /* USER CODE BEGIN 2 */
  /* 编码器计数器清零 */
  __HAL_TIM_SET_COUNTER(&htim2, 0);

  /* 开启编码器的两个输入通道 */
  if (HAL_TIM_Encoder_Start(&htim2, TIM_CHANNEL_ALL) != HAL_OK)
  {
    Error_Handler();
  }
  /* 驱动保持待机，禁止电机输出 */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_RESET);

  /* 两个方向控制脚初始为低 */
  HAL_GPIO_WritePin(GPIOB,
                    GPIO_PIN_0 | GPIO_PIN_1,
                    GPIO_PIN_RESET);

  /* 初始占空比为0% */
  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 0);

  /* 启动PWM通道，但保持0%占空比 */
  if (HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }  
  OLED_Init();

  if (OLED_GetStatus() != HAL_OK)
  {
	  Error_Handler();
  }

  /* 开启CPU周期计数器 */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  HAL_Delay(1);

  /* 确认周期计数器确实在运行，避免短延时死等 */
  if (DWT->CYCCNT == 0U)
  {
    OLED_ShowString(1, 1, "INA: DWT ERROR  ");
  }
  else
  {
    for (inaAddress = 0x40U;
         inaAddress <= 0x4FU;
         inaAddress++)
    {
      if (INA_CheckAddress(inaAddress) != 0U)
      {
        break;
      }
    }

    if (inaAddress <= 0x4FU)
    {
      OLED_ShowString(1, 1, "INA ADDR: 0x    ");
      OLED_ShowHexNum(1, 13, inaAddress, 2);

      if (INA_ReadRegister(inaAddress, 0xFEU, &inaMfr) != 0U &&
          INA_ReadRegister(inaAddress, 0xFFU, &inaDie) != 0U)
      {
        OLED_ShowString(2, 1, "MFR: 0x         ");
        OLED_ShowHexNum(2, 8, inaMfr, 4);

        OLED_ShowString(3, 1, "DIE: 0x         ");
        OLED_ShowHexNum(3, 8, inaDie, 4);
        /* 读取总线电压，每个计数为1.25mV */
        if (INA_ReadRegister(inaAddress, 0x02U, &inaBusRaw) != 0U)
        {
          /* 使用整数运算，换算为毫伏 */
          inaBusmV = ((uint32_t)inaBusRaw * 125U) / 100U;

          OLED_ShowString(4, 1, "BUS: 00000mV    ");
          OLED_ShowNum(4, 6, inaBusmV, 5);
        }
        else
        {
          OLED_ShowString(4, 1, "BUS READ FAILED ");
        }
      }
      else
      {
        OLED_ShowString(2, 1, "INA READ FAILED ");
        OLED_ShowString(3, 1, "                ");
      }
    }
    else
    {
      OLED_ShowString(1, 1, "INA: NOT FOUND  ");
    }
  }

  /* 保留结果3秒，然后继续原程序 */
  HAL_Delay(3000);
  OLED_Clear();
  OLED_ShowString(1, 1, "HAL OLED OK");
  OLED_ShowNum(2, 1, 12345, 5);
  if (HAL_UART_Transmit(&huart1,
                      (uint8_t *)"UART OK\r\n",
                      9,
                      100) != HAL_OK)
  {
      Error_Handler();
  }
  /* USER CODE END 2 */

  /* Call init function for freertos objects (in cmsis_os2.c) */
  MX_FREERTOS_Init();

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM1 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM1)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
