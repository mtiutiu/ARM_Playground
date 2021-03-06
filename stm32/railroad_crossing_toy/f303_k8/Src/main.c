/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  ** This notice applies to any and all portions of this file
  * that are not between comment pairs USER CODE BEGIN and
  * USER CODE END. Other portions of this file, whether 
  * inserted by the user or by software development tools
  * are owned by their respective copyright owners.
  *
  * COPYRIGHT(c) 2018 STMicroelectronics
  *
  * Redistribution and use in source and binary forms, with or without modification,
  * are permitted provided that the following conditions are met:
  *   1. Redistributions of source code must retain the above copyright notice,
  *      this list of conditions and the following disclaimer.
  *   2. Redistributions in binary form must reproduce the above copyright notice,
  *      this list of conditions and the following disclaimer in the documentation
  *      and/or other materials provided with the distribution.
  *   3. Neither the name of STMicroelectronics nor the names of its contributors
  *      may be used to endorse or promote products derived from this software
  *      without specific prior written permission.
  *
  * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
  * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "bell.h"
#include "irmp.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define FALSE     	0
#define TRUE      	1
#define OFF       	0
#define ON        	1
#define UP        	0
#define DOWN      	1
#define GO_UP     	-1
#define GO_DOWN    	1
#define ANGLE_PWM_COMPARE_MIN                   90
#define BARRIER_START_ANGLE                      0
#define BARRIER_STOP_ANGLE                      74
#define BARRIER_ANGLE_STEP_INTERVAL_MS         20UL
#define RUNNING 	0
#define STOPPED 	1
#define IR_KEY_ADDRESS  48896
#define LIGHTS_TOGGLE_INTERVAL_MS 	 	800UL
#define IDLE_SLEEP_TIMEOUT_MS			5000UL
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
#ifdef APP_DEBUG
#define DEBUG_APP(debug_line) \
  do { \
    char* message = debug_line; \
    HAL_UART_Transmit(&huart2, (uint8_t*)message, strlen(message), HAL_MAX_DELAY); \
  } while(0)
#endif

#define SERVO_POWER_UP()				HAL_GPIO_WritePin(Servo_Power_GPIO_Port, Servo_Power_Pin, GPIO_PIN_SET)
#define SERVO_POWER_DOWN()				HAL_GPIO_WritePin(Servo_Power_GPIO_Port, Servo_Power_Pin, GPIO_PIN_RESET)
#define LEFT_LIGHT_OFF()				HAL_GPIO_WritePin(Left_Led_GPIO_Port, Left_Led_Pin, GPIO_PIN_RESET)
#define RIGHT_LIGHT_OFF()				HAL_GPIO_WritePin(Right_Led_GPIO_Port, Right_Led_Pin, GPIO_PIN_RESET)
#define TOGGLE_LEFT_LIGHT()				HAL_GPIO_TogglePin(Left_Led_GPIO_Port, Left_Led_Pin)
#define TOGGLE_RIGHT_LIGHT()			HAL_GPIO_TogglePin(Right_Led_GPIO_Port, Right_Led_Pin)
#define GET_LEFT_LIGHT_STATE()			HAL_GPIO_ReadPin(Left_Led_GPIO_Port, Left_Led_Pin)
#define GET_RIGHT_LIGHT_STATE()			HAL_GPIO_ReadPin(Right_Led_GPIO_Port, Right_Led_Pin)
#define SET_LEFT_LIGHT_STATE(state)		HAL_GPIO_WritePin(Left_Led_GPIO_Port, Left_Led_Pin, state)
#define SET_RIGHT_LIGHT_STATE(state)	HAL_GPIO_WritePin(Right_Led_GPIO_Port, Right_Led_Pin, state)
#define LIGHTS_OFF()	\
	LEFT_LIGHT_OFF();	\
	RIGHT_LIGHT_OFF()
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* Private variables ---------------------------------------------------------*/
static uint8_t barrier_pwm_state = STOPPED;
static uint8_t barrier_state = UP;
static int8_t barrier_direction = GO_UP;
static uint8_t audio_tick = FALSE;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
/* Private function prototypes -----------------------------------------------*/

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
	// this callback runs with a frequency of 160KHz for PWM Audio output
	static uint32_t freq_divider = 0;

	if (htim->Instance == htim2.Instance) {
		// IR & Audio sampling frequency must be ~16KHz
		if (++freq_divider >= 10) {
			irmp_ISR();
			audio_tick = TRUE;
			freq_divider = 0;
		}
	}
}

static void Audio_Task(void) {
	static uint32_t sound_sample_index = 0;

	if (audio_tick) {
		if (barrier_direction == GO_DOWN || barrier_state == DOWN) {
			if (++sound_sample_index >= SND_DATA_SAMPLES) {
				sound_sample_index = 0;
			}
			__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3,
					bell_sound_data[sound_sample_index]);
		}

		if (barrier_state == UP) {
			sound_sample_index = 0;
			__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, 0);
		}
		audio_tick = FALSE;
	}
}

static void IR_Task(void) {
	IRMP_DATA irmp_data;

	if (irmp_get_data(&irmp_data)) {
#ifdef APP_DEBUG
		char ir_message[128];
		//DEBUG_APP("GOT IR SIGNAL\r\n");
		sprintf(ir_message, "IR Command: %d Address: %d\r\n", irmp_data.command,
				irmp_data.address);
		DEBUG_APP(ir_message);
#endif
		if (irmp_data.address == IR_KEY_ADDRESS) {
			if (barrier_state == UP) {
				barrier_direction = GO_DOWN;
			}

			if (barrier_state == DOWN) {
				barrier_direction = GO_UP;
			}
		}
	}
}

static void Lights_Task(void) {
	static uint32_t last_tick = 0;

	if (barrier_direction == GO_DOWN || barrier_state == DOWN) {
		if ((HAL_GetTick() - last_tick) >= LIGHTS_TOGGLE_INTERVAL_MS) {
			TOGGLE_RIGHT_LIGHT();
			SET_LEFT_LIGHT_STATE(!GET_RIGHT_LIGHT_STATE());
			last_tick = HAL_GetTick();
		}
	}

	if (barrier_state == UP) {
		LIGHTS_OFF();
	}
}

static void Servo_Barrier_Task(void) {
	// 1 -> 1,5ms <=> 0 -> 90 degrees
	static uint32_t last_tick = 0;
	uint16_t current_angle = __HAL_TIM_GET_COMPARE(&htim3, TIM_CHANNEL_1) - ANGLE_PWM_COMPARE_MIN;

	if ((barrier_direction == GO_DOWN) && (current_angle >= BARRIER_STOP_ANGLE)) {
		barrier_state = DOWN;
		HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_1);
		barrier_pwm_state = STOPPED;
		return;
	}

	if ((barrier_direction == GO_UP) && (current_angle <= BARRIER_START_ANGLE)) {
		barrier_state = UP;
		HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_1);
		barrier_pwm_state = STOPPED;
		return;
	}

	if (barrier_pwm_state == STOPPED) {
		HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
		barrier_pwm_state = RUNNING;
	}

	if (HAL_GetTick() - last_tick >= BARRIER_ANGLE_STEP_INTERVAL_MS) {
		__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, barrier_direction + __HAL_TIM_GET_COMPARE(&htim3, TIM_CHANNEL_1));
		last_tick = HAL_GetTick();
	}
}

static void Sleep_Task(void) {
	static uint32_t last_sleep_timestamp;

	if (barrier_state != UP) {
		// reset timestamp when not in resting state
		last_sleep_timestamp = HAL_GetTick();
		return;
	}

	if ((barrier_state == UP) && (HAL_GetTick() - last_sleep_timestamp) >= IDLE_SLEEP_TIMEOUT_MS) {
		// HAL enter sleep
		LIGHTS_OFF();
		//SERVO_POWER_DOWN();
		HAL_TIM_Base_Stop_IT(&htim2);
		HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_3);
		HAL_SuspendTick();
#ifdef APP_DEBUG
		DEBUG_APP("Entering sleep ...\r\n");
#endif
		HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
		SystemClock_Config();
		HAL_ResumeTick();
		//SERVO_POWER_UP();
		HAL_TIM_Base_Start_IT(&htim2);
		HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3);
		last_sleep_timestamp = HAL_GetTick();
#ifdef APP_DEBUG
		DEBUG_APP("Waking up from sleep ...\r\n");
#endif
	}
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

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
#ifdef APP_DEBUG
  MX_USART2_UART_Init();
#endif
  MX_TIM2_Init();
  MX_TIM3_Init();
  /* USER CODE BEGIN 2 */
	//HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
	barrier_pwm_state = STOPPED;
	HAL_TIM_Base_Start_IT(&htim2);
	HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3);
	irmp_init(); // initialize irmp
#ifdef APP_DEBUG
	DEBUG_APP("Started ...\r\n");
#endif
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
	while (1) {

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
		IR_Task();
		Lights_Task();
		Audio_Task();
		Servo_Barrier_Task();
		Sleep_Task();
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

  /**Initializes the CPU, AHB and APB busses clocks 
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL16;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /**Initializes the CPU, AHB and APB busses clocks 
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
	/* User can add his own implementation to report the HAL error return state */
	while (1) {
	}
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(char *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
	/* User can add his own implementation to report the file name and line number,
	 tex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
