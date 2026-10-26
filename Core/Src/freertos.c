/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
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
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "OLED.h"
#include "usart.h"
#include "tim.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define MOTOR_FAULT_NONE          0U
#define MOTOR_FAULT_NO_ENCODER    1U
#define MOTOR_FAULT_OVERCURRENT   2U
#define MOTOR_FAULT_INA_READ      3U

#define INA_READ_FAIL_TEST  0U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
static osMessageQId uartRxQueueHandle;
static uint8_t uartRxByte;

static volatile uint32_t uartRxDropCount = 0;
/* USER CODE END Variables */
osThreadId defaultTaskHandle;
osThreadId CounterTaskHandle;
osMessageQId countQueueHandle;

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
static void Motor_Stop(void);
static void Motor_RunTest(void);
/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void const * argument);
void StartCounterTask(void const * argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* GetIdleTaskMemory prototype (linked to static allocation support) */
void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize );

/* USER CODE BEGIN GET_IDLE_TASK_MEMORY */
static StaticTask_t xIdleTaskTCBBuffer;
static StackType_t xIdleStack[configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize )
{
  *ppxIdleTaskTCBBuffer = &xIdleTaskTCBBuffer;
  *ppxIdleTaskStackBuffer = &xIdleStack[0];
  *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
  /* place for user code */
}
/* USER CODE END GET_IDLE_TASK_MEMORY */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* definition and creation of countQueue */
  osMessageQDef(countQueue, 4, uint32_t);
  countQueueHandle = osMessageCreate(osMessageQ(countQueue), NULL);

  /* USER CODE BEGIN RTOS_QUEUES */
  osMessageQDef(uartRxQueue, 16, uint32_t);
  uartRxQueueHandle = osMessageCreate(osMessageQ(uartRxQueue), NULL);

  if (uartRxQueueHandle == NULL)
  {
    Error_Handler();
  }
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* definition and creation of defaultTask */
  osThreadDef(defaultTask, StartDefaultTask, osPriorityNormal, 0, 256);
  defaultTaskHandle = osThreadCreate(osThread(defaultTask), NULL);

  /* definition and creation of CounterTask */
  osThreadDef(CounterTask, StartCounterTask, osPriorityNormal, 0, 128);
  CounterTaskHandle = osThreadCreate(osThread(CounterTask), NULL);

  /* USER CODE BEGIN RTOS_THREADS */
  if (countQueueHandle == NULL ||
    defaultTaskHandle == NULL ||
    CounterTaskHandle == NULL)
{
  Error_Handler();
}
  /* USER CODE END RTOS_THREADS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void const * argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  osEvent event;
  uint8_t ch;
  uint8_t running = 0;
  uint32_t runStartTick = 0;
  uint16_t encoderCount;
  uint16_t lastEncoderCount = 0;
  uint16_t speedLastCount;
  uint32_t speedLastTick;
  uint32_t nowTick;
  uint32_t elapsedMs;
  int32_t countDelta;
  int32_t speedCps;
  uint16_t inaShuntRaw;
  int32_t inaShuntCount;
  int32_t inaCurrentmA;
  uint8_t motorFault = MOTOR_FAULT_NONE;
  uint16_t motionLastCount = 0;
  uint32_t lastMotionTick = 0;
  uint8_t inaReadOk;
  const int32_t targetCps = 1000;
  const int32_t basePwm = 60;

  const float kp = 0.02f;
  const float ki = 0.05f;

  int32_t speedError;
  int32_t pwmCompare = 80;

  float dt;
  float pTerm;
  float integralTerm = 0.0f;
  float integralNext;
  float output;
  
  /* 当前空载、PWM不超过20%工况的实验性电流报警阈值 */
  const int32_t currentTripmA = 20;

  uint8_t overCurrentHits = 0;

  OLED_ShowString(1, 1, "ENC: 00000      ");
  OLED_ShowString(2, 1, "MOTOR: STOP     ");
  OLED_ShowString(3, 1, "RX:   I:+00000mA");
  OLED_ShowString(4, 1, "00000 V:+00000  ");

  speedLastCount = (uint16_t)__HAL_TIM_GET_COUNTER(&htim2);
  speedLastTick = HAL_GetTick();

  if (HAL_UART_Receive_IT(&huart1, &uartRxByte, 1) != HAL_OK)
  {
    Error_Handler();
  }

  for (;;)
  {
    /* 保留原来的计数显示 */
    event = osMessageGet(countQueueHandle, 20);

    if (event.status == osEventMessage)
    {
      OLED_ShowNum(4, 1, event.value.v, 5);
    }

    /* 处理串口字符 */
    event = osMessageGet(uartRxQueueHandle, 0);

    if (event.status == osEventMessage)
    {
      ch = (uint8_t)event.value.v;

      if (ch >= 0x20U && ch <= 0x7EU)
      {
        OLED_ShowChar(3, 5, (char)ch);
      }

      if ((ch == 'R' || ch == 'r') &&
          running == 0 &&
          motorFault == MOTOR_FAULT_NONE)
      {
        pwmCompare = 80;
        integralTerm = 0.0f;
        overCurrentHits = 0;

        /* 建立测速起点 */
        speedLastCount =
            (uint16_t)__HAL_TIM_GET_COUNTER(&htim2);

        speedLastTick = HAL_GetTick();
        runStartTick = speedLastTick;

        /* 建立无反馈检测起点 */
        motionLastCount = speedLastCount;
        lastMotionTick = runStartTick;

        Motor_RunTest();
        running = 1;

        OLED_ShowString(2, 1, "RUN PWM:020%    ");
      }
      else if (ch == 'S' || ch == 's')
      {
        Motor_Stop();
        running = 0;
        integralTerm = 0.0f;
        pwmCompare = 0;

        /* S停止，同时允许人工清除故障 */
        motorFault = MOTOR_FAULT_NONE;

        OLED_ShowString(2, 1, "MOTOR: STOP     ");
      }
    }

    /* 非阻塞计时：到期后停止 */
    if (running != 0 &&
        (uint32_t)(HAL_GetTick() - runStartTick) >= 5000U)
    {
      Motor_Stop();
      running = 0;
      integralTerm = 0.0f;
      pwmCompare = 0;

      OLED_ShowString(2, 1, "MOTOR: STOP     ");
    }
    
    /* 记录本次采样时间和计数 */
    nowTick = HAL_GetTick();
    encoderCount = (uint16_t)__HAL_TIM_GET_COUNTER(&htim2);
    
    /* 只在电机运行状态下检测 */
    if (running != 0)
    {
      /* 计数发生变化，更新最近一次检测到运动的时间 */
      if (encoderCount != motionLastCount)
      {
        motionLastCount = encoderCount;
        lastMotionTick = nowTick;
      }

      /* 连续约500ms未检测到计数变化 */
      if ((uint32_t)(nowTick - lastMotionTick) >= 500U)
      {
        Motor_Stop();
        running = 0;
        integralTerm = 0.0f;
        pwmCompare = 0;

        /* 锁定故障，禁止R直接重启 */
        motorFault = MOTOR_FAULT_NO_ENCODER;

        OLED_ShowString(2, 1, "FAULT: NO ENC   ");
      }
    }

    /* 第一行继续显示累计计数 */
    if (encoderCount != lastEncoderCount)
    {
      OLED_ShowNum(1, 6, encoderCount, 5);
      lastEncoderCount = encoderCount;
    }

    /* 每隔约100ms计算一次速度 */
    elapsedMs = (uint32_t)(nowTick - speedLastTick);

    if (elapsedMs >= 100U)
    {
      countDelta = (int32_t)encoderCount
                 - (int32_t)speedLastCount;

      /* 处理16位计数器在0与65535之间的回绕 */
      if (countDelta > 32767)
      {
        countDelta -= 65536;
      }
      else if (countDelta < -32768)
      {
        countDelta += 65536;
      }

      /* 按实际时间间隔换算成计数/秒 */
      speedCps = (countDelta * 1000) / (int32_t)elapsedMs;

      speedLastCount = encoderCount;
      speedLastTick = nowTick;

      if (running != 0)
      {
        speedError = targetCps - speedCps;

        /* 使用实际采样间隔，单位：秒 */
        dt = (float)elapsedMs / 1000.0f;

        /* 比例修正 */
        pTerm = kp * (float)speedError;

        /* 先计算本次准备采用的积分补偿 */
        integralNext = integralTerm
                     + ki * (float)speedError * dt;

        /* 积分补偿限制在-20～+20个比较值计数 */
        if (integralNext > 20.0f)
        {
          integralNext = 20.0f;
        }
        else if (integralNext < -20.0f)
        {
          integralNext = -20.0f;
        }

        output = (float)basePwm + pTerm + integralNext;

        /*
         * 如果输出越界，而且误差会让它继续向外增加，
         * 则放弃本次积分更新，保留原来的积分值。
         */
        if ((output > 80.0f && speedError > 0) ||
            (output < 0.0f && speedError < 0))
        {
          integralNext = integralTerm;
        }

        integralTerm = integralNext;

        /* 用最终接受的积分值计算输出 */
        output = (float)basePwm + pTerm + integralTerm;

        /* 最终输出限制：0%～20% */
        if (output > 80.0f)
        {
          output = 80.0f;
        }
        else if (output < 0.0f)
        {
          output = 0.0f;
        }

        /* 非负输出四舍五入成整数比较值 */
        pwmCompare = (int32_t)(output + 0.5f);

        __HAL_TIM_SET_COMPARE(&htim3,
                              TIM_CHANNEL_1,
                              (uint32_t)pwmCompare);

        OLED_ShowNum(2, 9,
                     (uint32_t)pwmCompare / 4U,
                     3);
      }

      OLED_ShowSignedNum(4, 9, speedCps, 5);
      
      /* 先执行真实读取 */
      inaReadOk = INA_ReadRegister(0x40U, 0x01U, &inaShuntRaw);

      #if INA_READ_FAIL_TEST
      /* 进入运行状态约200ms后，模拟一次读取失败 */
      if (running != 0 &&
          (uint32_t)(nowTick - runStartTick) >= 200U)
      {
        inaReadOk = 0U;
      }
      #endif

      if (inaReadOk != 0U)
      {
        inaShuntCount = (int32_t)inaShuntRaw;

        /* 还原16位有符号数 */
        if ((inaShuntRaw & 0x8000U) != 0U)
        {
          inaShuntCount -= 65536;
        }

        inaCurrentmA = inaShuntCount / 4;

        OLED_ShowSignedNum(3, 9, inaCurrentmA, 5);
        OLED_ShowString(3, 15, "mA");

        /*
         * 仅运行时检测。
         * 启动后的前300ms暂不判断电流超限。
         */
        if (running != 0 &&
            (uint32_t)(nowTick - runStartTick) >= 300U)
        {
          if (inaCurrentmA >= currentTripmA)
          {
            if (overCurrentHits < 3U)
            {
              overCurrentHits++;
            }
          }
          else
          {
            /* 必须连续超限，中间恢复正常就重新计数 */
            overCurrentHits = 0;
          }

          if (overCurrentHits >= 3U)
          {
            Motor_Stop();
            running = 0;
            integralTerm = 0.0f;
            pwmCompare = 0;

            motorFault = MOTOR_FAULT_OVERCURRENT;
            overCurrentHits = 0;

            OLED_ShowString(2, 1, "FAULT: CURRENT  ");
          }
        }
        else
        {
          overCurrentHits = 0;
        }
      }
      else
      {
        OLED_ShowString(3, 9, "  ERR   ");
        overCurrentHits = 0;

        /* 运行期间失去电流监测，停止输出并锁定故障 */
        if (running != 0)
        {
          Motor_Stop();
          running = 0;
          integralTerm = 0.0f;
          pwmCompare = 0;

          motorFault = MOTOR_FAULT_INA_READ;

          OLED_ShowString(2, 1, "FAULT: INA I2C  ");
        }
      }
    }
  }
  /* USER CODE END StartDefaultTask */
}

/* USER CODE BEGIN Header_StartCounterTask */
/**
* @brief Function implementing the CounterTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartCounterTask */
void StartCounterTask(void const * argument)
{
  /* USER CODE BEGIN StartCounterTask */
  uint32_t count = 0;

  for (;;)
  {
    /* 每秒发送一次任务计数 */
    if (osMessagePut(countQueueHandle,
                     count,
                     osWaitForever) == osOK)
    {
      count++;
    }

    osDelay(1000);
  }
  /* USER CODE END StartCounterTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
static void Motor_Stop(void)
{
  /* 先禁止驱动输出 */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_RESET);

  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 0);

  HAL_GPIO_WritePin(GPIOB,
                    GPIO_PIN_0 | GPIO_PIN_1,
                    GPIO_PIN_RESET);
}

static void Motor_RunTest(void)
{
  /* 在停止状态下设置方向和占空比 */
  Motor_Stop();

  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);

  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 80);

  /* 最后使能驱动 */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_SET);
}
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        /* 中断里不能等待：队列满时记录丢弃次数 */
        if (osMessagePut(uartRxQueueHandle,
                         (uint32_t)uartRxByte,
                         0) != osOK)
        {
            uartRxDropCount++;
        }

        /* 本次接收已经结束，重新开启下一个字节的接收 */
        HAL_UART_Receive_IT(&huart1, &uartRxByte, 1);
    }
}
/* USER CODE END Application */

