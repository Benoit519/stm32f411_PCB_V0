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
#include "mcp23017.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

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
ADC_HandleTypeDef hadc1;

I2C_HandleTypeDef hi2c1;

I2S_HandleTypeDef hi2s1;
DMA_HandleTypeDef hdma_spi1_tx;

PCD_HandleTypeDef hpcd_USB_OTG_FS;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_I2S1_Init(void);
static void MX_I2C1_Init(void);
static void MX_USB_OTG_FS_PCD_Init(void);
static void MX_ADC1_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* -------------------------------------------------------------------------- */
/* Hardware / UI logic: periodic scan (from previous optimized version)         */
/* -------------------------------------------------------------------------- */
uint8_t mcpValueA20 = 0;
uint8_t mcpValueB20 = 0;
uint8_t mcpValueA21 = 0;
uint8_t mcpValueB21 = 0;
uint8_t mcpValueA22 = 0;
uint8_t mcpValueB22 = 0;
uint8_t mcpValueA23 = 0;
uint8_t mcpValueB23 = 0;
volatile uint16_t pressure = 0;
volatile uint8_t note_active = 0;

MCP23017_HandleTypeDef hmcp20;
MCP23017_HandleTypeDef hmcp21;
MCP23017_HandleTypeDef hmcp22;
MCP23017_HandleTypeDef hmcp23;

int toBinary(uint8_t a, uint8_t port) {
    uint8_t i;
    int compteur = 0;
    for (i = 0x80; i != 0; i >>= 1) {
        if ((a & i)) {
            if (port == 0) {
                return 7 - (7 - compteur);
            }
            else {
                return (7 - compteur);
            }
        }
        else {
            compteur = compteur + 1;
        }
    }
    return -1;
}

static void UI_ScanAndDispatch(void)
{
    mcpValueA20 = mcp23017_read_gpio_int(&hmcp20, MCP23017_PORTA);
    mcpValueB20 = mcp23017_read_gpio_int(&hmcp20, MCP23017_PORTB);
    mcpValueA21 = mcp23017_read_gpio_int(&hmcp21, MCP23017_PORTA);
    mcpValueB21 = mcp23017_read_gpio_int(&hmcp21, MCP23017_PORTB);
    mcpValueA22 = mcp23017_read_gpio_int(&hmcp22, MCP23017_PORTA);
    mcpValueB22 = mcp23017_read_gpio_int(&hmcp22, MCP23017_PORTB);
    mcpValueA23 = mcp23017_read_gpio_int(&hmcp23, MCP23017_PORTA);
    mcpValueB23 = mcp23017_read_gpio_int(&hmcp23, MCP23017_PORTB);

    int8_t raw_idxA20 = (int8_t)toBinary(mcpValueA20, 0);
    int8_t raw_idxA21 = (int8_t)toBinary(mcpValueA21, 0);
    int8_t raw_idxA22 = (int8_t)toBinary(mcpValueA22, 0);
    int8_t raw_idxA23 = (int8_t)toBinary(mcpValueA23, 0);

    int8_t raw_idxB20 = (int8_t)toBinary(mcpValueB20, 1);
    int8_t raw_idxB21 = (int8_t)toBinary(mcpValueB21, 1);
    int8_t raw_idxB22 = (int8_t)toBinary(mcpValueB22, 1);
    int8_t raw_idxB23 = (int8_t)toBinary(mcpValueB23, 1);

    if ((raw_idxA20 == -1) &&
        (raw_idxB20 == -1) &&
        (raw_idxA21 == -1) &&
        (raw_idxB21 == -1) &&
        (raw_idxA22 == -1) &&
        (raw_idxB22 == -1) &&
        (raw_idxA23 == -1) &&
        (raw_idxB23 == -1))
    {
        note_active = 0;
    }
    else
    {
        note_active = 1;
    }
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc == &hadc1)
    {
        pressure = HAL_ADC_GetValue(hadc);
    }
}

void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
    (void)hi2s;
if ((HAL_ADC_GetState(&hadc1) & HAL_ADC_STATE_REG_BUSY) == 0)
{
    HAL_ADC_Start_IT(&hadc1);
}

    render_audio_block((int16_t *)&bufferDMA[0], HALF_BUFFER_SIZE);
}

void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *hi2s)
{
    (void)hi2s;
if ((HAL_ADC_GetState(&hadc1) & HAL_ADC_STATE_REG_BUSY) == 0)
{
    HAL_ADC_Start_IT(&hadc1);
}

    render_audio_block((int16_t *)&bufferDMA[HALF_BUFFER_SIZE], HALF_BUFFER_SIZE);
}

void render_audio_block(int16_t *buffer, uint32_t samples)
{
    if (!note_active)
    {
        for (uint32_t i = 0; i < samples; i++)
        {
            buffer[i] = 0;
        }
        return;
    }

    /* Lecture de la dernière valeur ADC */
    uint16_t adc = pressure;

    /* Normalisation entre 0 et 1 */
    float p = (float)adc * (1.0f / 4095.0f);

    /* Exemple : fréquence de 100 à 1000 Hz */
    float frequency = 100.0f + p * 900.0f;

    phase_inc = (2.0f * (float)M_PI * frequency) / SAMPLE_RATE;

    for (uint32_t i = 0; i < samples; i++)
    {
        buffer[i] = (int16_t)(AMPLITUDE * sinf(phase));

        phase += phase_inc;

        if (phase >= 2.0f * (float)M_PI)
            phase -= 2.0f * (float)M_PI;
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
  MX_DMA_Init();
  MX_I2S1_Init();
  MX_I2C1_Init();
  MX_USB_OTG_FS_PCD_Init();
  MX_ADC1_Init();
  /* USER CODE BEGIN 2 */
    mcp23017_init(&hmcp20, &hi2c1, MCP23017_ADDRESS_20);
    mcp23017_iodir(&hmcp20, MCP23017_PORTA, MCP23017_IODIR_ALL_INPUT);
    mcp23017_iodir(&hmcp20, MCP23017_PORTB, MCP23017_IODIR_ALL_INPUT);

    mcp23017_init(&hmcp21, &hi2c1, MCP23017_ADDRESS_27);
    mcp23017_iodir(&hmcp21, MCP23017_PORTA, MCP23017_IODIR_ALL_INPUT);
    mcp23017_iodir(&hmcp21, MCP23017_PORTB, MCP23017_IODIR_ALL_INPUT);

    mcp23017_init(&hmcp22, &hi2c1, MCP23017_ADDRESS_22);
    mcp23017_iodir(&hmcp22, MCP23017_PORTA, MCP23017_IODIR_ALL_INPUT);
    mcp23017_iodir(&hmcp22, MCP23017_PORTB, MCP23017_IODIR_ALL_INPUT);

    mcp23017_init(&hmcp23, &hi2c1, MCP23017_ADDRESS_23);
    mcp23017_iodir(&hmcp23, MCP23017_PORTA, MCP23017_IODIR_ALL_INPUT);
    mcp23017_iodir(&hmcp23, MCP23017_PORTB, MCP23017_IODIR_ALL_INPUT);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
UI_ScanAndDispatch();

for (int i = 0; i < BUFFER_SIZE; i++)
{
    bufferDMA[i] = 0;
}

HAL_I2S_Transmit_DMA(&hi2s1,
                     (uint16_t*)bufferDMA,
                     BUFFER_SIZE);
while (1)
{
    UI_ScanAndDispatch();

    HAL_Delay(5);   // scan toutes les 5 ms (~200 Hz)
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
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};


    __HAL_RCC_PWR_CLK_ENABLE();

    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);


    /*
       PLL principal
       HSE 8MHz

       VCO = 8 / 8 * 336 = 336MHz
       SYSCLK = 336 / 4 = 84MHz
    */

    RCC_OscInitStruct.OscillatorType =
        RCC_OSCILLATORTYPE_HSE;

    RCC_OscInitStruct.HSEState =
        RCC_HSE_ON;

    RCC_OscInitStruct.PLL.PLLState =
        RCC_PLL_ON;

    RCC_OscInitStruct.PLL.PLLSource =
        RCC_PLLSOURCE_HSE;

    RCC_OscInitStruct.PLL.PLLM = 8;
    RCC_OscInitStruct.PLL.PLLN = 336;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
    RCC_OscInitStruct.PLL.PLLQ = 7;


    if(HAL_RCC_OscConfig(&RCC_OscInitStruct)!=HAL_OK)
        Error_Handler();


    /*
       PLLI2S

       8 / 8 * 192 = 192MHz
       192 / 5 = 38.4MHz I2S clock

       Le prescaler I2S donnera 44.1kHz
    */

    PeriphClkInitStruct.PeriphClockSelection =
        RCC_PERIPHCLK_I2S;

    PeriphClkInitStruct.PLLI2S.PLLI2SN = 192;
    PeriphClkInitStruct.PLLI2S.PLLI2SR = 5;


    if(HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct)!=HAL_OK)
        Error_Handler();



    RCC_ClkInitStruct.ClockType =
        RCC_CLOCKTYPE_SYSCLK |
        RCC_CLOCKTYPE_HCLK |
        RCC_CLOCKTYPE_PCLK1 |
        RCC_CLOCKTYPE_PCLK2;


    RCC_ClkInitStruct.SYSCLKSource =
        RCC_SYSCLKSOURCE_PLLCLK;

    RCC_ClkInitStruct.AHBCLKDivider =
        RCC_SYSCLK_DIV1;

    RCC_ClkInitStruct.APB1CLKDivider =
        RCC_HCLK_DIV2;

    RCC_ClkInitStruct.APB2CLKDivider =
        RCC_HCLK_DIV1;


    if(HAL_RCC_ClockConfig(
        &RCC_ClkInitStruct,
        FLASH_LATENCY_2)!=HAL_OK)
    {
        Error_Handler();
    }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV2;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_1;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_84CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

 HAL_NVIC_SetPriority(ADC_IRQn, 1, 0);
 HAL_NVIC_EnableIRQ(ADC_IRQn);
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief I2S1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2S1_Init(void)
{

    hi2s1.Instance = SPI1;


    hi2s1.Init.Mode =
        I2S_MODE_MASTER_TX;


    hi2s1.Init.Standard =
        I2S_STANDARD_PHILIPS;


    hi2s1.Init.DataFormat =
        I2S_DATAFORMAT_16B;


    /*
       Pas de MCLK pour UDA1334A
       sauf si ta carte l'utilise
    */

    hi2s1.Init.MCLKOutput =
        I2S_MCLKOUTPUT_DISABLE;


    hi2s1.Init.AudioFreq =
        I2S_AUDIOFREQ_44K;


    hi2s1.Init.CPOL =
        I2S_CPOL_LOW;


    hi2s1.Init.ClockSource =
        I2S_CLOCK_PLL;


    hi2s1.Init.FullDuplexMode =
        I2S_FULLDUPLEXMODE_DISABLE;


    if(HAL_I2S_Init(&hi2s1)!=HAL_OK)
    {
        Error_Handler();
    }
}

/**
  * @brief USB_OTG_FS Initialization Function
  * @param None
  * @retval None
  */
static void MX_USB_OTG_FS_PCD_Init(void)
{

  /* USER CODE BEGIN USB_OTG_FS_Init 0 */

  /* USER CODE END USB_OTG_FS_Init 0 */

  /* USER CODE BEGIN USB_OTG_FS_Init 1 */

  /* USER CODE END USB_OTG_FS_Init 1 */
  hpcd_USB_OTG_FS.Instance = USB_OTG_FS;
  hpcd_USB_OTG_FS.Init.dev_endpoints = 4;
  hpcd_USB_OTG_FS.Init.speed = PCD_SPEED_FULL;
  hpcd_USB_OTG_FS.Init.dma_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.phy_itface = PCD_PHY_EMBEDDED;
  hpcd_USB_OTG_FS.Init.Sof_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.low_power_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.lpm_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.vbus_sensing_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.use_dedicated_ep1 = DISABLE;
  if (HAL_PCD_Init(&hpcd_USB_OTG_FS) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USB_OTG_FS_Init 2 */

  /* USER CODE END USB_OTG_FS_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA2_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA2_Stream2_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Stream2_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream2_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pins : PC13 PC0 PC1 PC2
                           PC3 */
  GPIO_InitStruct.Pin = GPIO_PIN_13|GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2
                          |GPIO_PIN_3;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : PA2 PA3 */
  GPIO_InitStruct.Pin = GPIO_PIN_2|GPIO_PIN_3;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : PB12 */
  GPIO_InitStruct.Pin = GPIO_PIN_12;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : PA8 PA10 */
  GPIO_InitStruct.Pin = GPIO_PIN_8|GPIO_PIN_10;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : PC10 */
  GPIO_InitStruct.Pin = GPIO_PIN_10;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : PB5 */
  GPIO_InitStruct.Pin = GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

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
    __disable_irq();
    while (1)
    {
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
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
    /* User can add his own implementation to report the file name and line number,
       ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
