/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2025 STMicroelectronics.
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
#include "usb_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include <stdlib.h> // For atoi
#include <usbd_cdc_if.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
// TM1637 Commands
#define TM1637_CMD_SET_DATA 0x40
#define TM1637_CMD_SET_ADDR 0xC0
#define TM1637_CMD_SET_DSIPLAY 0x80
#define TM1637_CMD_STOP_DSIPLAY 0x8A

// Indeks untuk karakter khusus di segment_map
#define SEG_IDX_BLANK 10
#define SEG_IDX_MINUS 11 // Indeks baru untuk karakter '-'

// Stepper speed control
#define MAX_SPEED_LEVEL 100 // speed maksimum yang diperbolehkan
#define MIN_SPEED_LEVEL 1   // speed minimum yang diperbolehkan
#define DEFAULT_SPEED_LEVEL 5
#define PULSE_TIMER_BASE_ARR 65535 // Corresponds to slowest speed (larger ARR = slower)
#define PULSE_TIMER_ARR_STEP 611   // Amount ARR decreases per speed level increase
// Define a minimum practical ARR value for your timer at the highest speed
#define MINIMUM_TIMER_ARR_VALUE 200 // Example: Allows for high frequency. Adjust as needed.

// Flash memory settings
#define FLASH_SETTINGS_PAGE_ADDR 0x0800FC00 // Example: Last 1KB page on 64KB STM32F103C8

// Default Accel/Decel step delays in milliseconds (time between speed level increments/decrements)
#define DEFAULT_ACCEL_DELAY_MS 50 // ms per speed level change during acceleration
#define DEFAULT_DECEL_DELAY_MS 50 // ms per speed level change during deceleration
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
TIM_HandleTypeDef htim2;

/* USER CODE BEGIN PV */
// Stepper motor state
volatile uint8_t g_motor_enabled_target = 0;                  // Target enabled state (to be saved/loaded)
volatile uint8_t g_motor_is_running = 0;                      // Actual running state (controlled by accel/decel)
volatile uint8_t g_motor_direction = 0;                       // 0 for CW, 1 for CCW
volatile uint32_t g_current_speed_level = 0;                  // Actual current speed level (0 means stopped)
volatile uint32_t g_target_speed_level = DEFAULT_SPEED_LEVEL; // Target speed for accel/decel

// Acceleration/Deceleration parameters
volatile uint32_t g_accel_step_delay_ms = DEFAULT_ACCEL_DELAY_MS;
volatile uint32_t g_decel_step_delay_ms = DEFAULT_DECEL_DELAY_MS;
volatile uint32_t g_last_accel_decel_tick = 0;

// TM1637 Display
extern TIM_HandleTypeDef htim2; // Timer for stepper pulses

// Button debounce
uint32_t g_last_btn_speed_up_time = 0;
uint32_t g_last_btn_speed_down_time = 0;
uint32_t g_last_btn_dir_time = 0;
uint32_t g_last_btn_enable_time = 0;
uint32_t g_last_btn_save_time = 0; // For save button
const uint32_t DEBOUNCE_DELAY_MS = 150;

// Perbarui tm1637_segment_map untuk menyertakan karakter minus
const uint8_t tm1637_segment_map[] = {
    0x3f, // 0
    0x06, // 1
    0x5b, // 2
    0x4f, // 3
    0x66, // 4
    0x6d, // 5
    0x7d, // 6
    0x07, // 7
    0x7f, // 8
    0x6f, // 9
    0x00, // 10: Karakter Kosong (Blank)
    0x40  // 11: Karakter Minus '-' (hanya segmen G)
};

// Settings structure for Flash
typedef struct
{
  uint32_t speed_level_target; // Target speed level when enabled
  uint32_t direction;
  uint32_t enabled_on_startup; // Should motor be enabled after loading?
  uint32_t accel_delay_ms;
  uint32_t decel_delay_ms;
  uint32_t checksum; // Simple checksum for data integrity
} StepperSettings_TypeDef;

StepperSettings_TypeDef g_persistent_settings;
uint8_t g_save_settings_flag = 0;

// USB VCP Buffer
extern uint8_t UserRxBufferFS[APP_RX_DATA_SIZE]; // Defined in usbd_cdc_if.c
extern uint8_t UserTxBufferFS[APP_TX_DATA_SIZE]; // Defined in usbd_cdc_if.c
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM2_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
// Basic delay function for TM1637 bit-banging
void tm1637_delay_us(uint32_t us)
{
  volatile uint32_t count = us * (SystemCoreClock / 1000000 / 5); // Adjust divisor for accuracy
  while (count--)
    ;
}

// GPIO wrappers for TM1637
void tm1637_clk_high(void) { HAL_GPIO_WritePin(TM1637_CLK_GPIO_Port, TM1637_CLK_Pin, GPIO_PIN_SET); }
void tm1637_clk_low(void) { HAL_GPIO_WritePin(TM1637_CLK_GPIO_Port, TM1637_CLK_Pin, GPIO_PIN_RESET); }
void tm1637_dio_high(void) { HAL_GPIO_WritePin(TM1637_DIO_GPIO_Port, TM1637_DIO_Pin, GPIO_PIN_SET); }
void tm1637_dio_low(void) { HAL_GPIO_WritePin(TM1637_DIO_GPIO_Port, TM1637_DIO_Pin, GPIO_PIN_RESET); }

void tm1637_start(void)
{
  tm1637_clk_high();
  tm1637_dio_high(); // Ensure DIO is high before CLK high to DIO low transition for start
  tm1637_delay_us(2);
  tm1637_dio_low();
  // tm1637_clk_low(); // Keep CLK low after start
  // tm1637_delay_us(5);
}

void tm1637_stop(void)
{
  tm1637_clk_low();
  tm1637_delay_us(2);
  tm1637_dio_low();
  tm1637_delay_us(2);
  tm1637_clk_high();
  tm1637_delay_us(2);
  tm1637_dio_high();
}

uint8_t tm1637_write_byte_ack(uint8_t byte)
{
  for (uint8_t i = 0; i < 8; i++)
  {
    tm1637_clk_low();
    // tm1637_delay_us(3);
    if (byte & 0x01)
    {
      tm1637_dio_high();
    }
    else
    {
      tm1637_dio_low();
    }
    tm1637_delay_us(3);
    byte >>= 1;
    tm1637_clk_high();
    tm1637_delay_us(3);
  }
  // Wait for ACK
  tm1637_clk_low();
  tm1637_delay_us(5);
  tm1637_clk_high();
  tm1637_delay_us(2);
  tm1637_clk_low();
  // tm1637_dio_high(); // Release DIO
  // uint8_t ack = HAL_GPIO_ReadPin(TM1637_DIO_GPIO_Port, TM1637_DIO_Pin); // Requires DIO to be input
  // return ack == 0; // ACK is low
  return 1; // Simplified: assume ACK ok
}

void tm1637_set_brightness(uint8_t brightness_0_to_7)
{
  tm1637_start();
  tm1637_write_byte_ack(TM1637_CMD_SET_DSIPLAY | (brightness_0_to_7 & 0x07) | 0x08); // 0x08 = display ON
  tm1637_stop();
}

void tm1637_display_segments_at_pos(uint8_t position_0_to_3, uint8_t segment_data)
{
  tm1637_start();
  tm1637_write_byte_ack(TM1637_CMD_SET_ADDR | (position_0_to_3 & 0x03));
  tm1637_write_byte_ack(segment_data);
  tm1637_stop();
}

// void tm1637_display_number(uint16_t number)
// {                                                                             // Displays 0-99 for speed
//   uint8_t digits[4] = {tm1637_segment_map[10], tm1637_segment_map[10], 0, 0}; // Blank, Blank, D1, D0

//   if (number > 99)
//     number = 99; // Cap at 99 for two-digit display

//   digits[2] = tm1637_segment_map[number / 10]; // Tens digit
//   digits[3] = tm1637_segment_map[number % 10]; // Units digit

//   // If number is less than 10, blank the tens digit (optional)
//   if (number < 10)
//   {
//     digits[2] = tm1637_segment_map[10]; // Blank
//   }

//   tm1637_start();
//   tm1637_write_byte_ack(TM1637_CMD_SET_DATA); // Data command: write data to display register, auto increment address
//   tm1637_stop();

//   tm1637_start();
//   tm1637_write_byte_ack(TM1637_CMD_SET_ADDR | 0x00); // Start at address 0 ( leftmost digit)
//   for (uint8_t i = 0; i < 4; i++)
//   {
//     tm1637_write_byte_ack(digits[i]);
//   }
//   tm1637_stop();
//   tm1637_set_brightness(7); // Ensure display is on with brightness
// }

// void tm1637_init(void)
// {
//   // GPIOs are configured by CubeMX
//   tm1637_set_brightness(7); // Set brightness and turn display on
//   tm1637_display_number(0); // Display 0 initially
// }

// FUNGSI BARU untuk menampilkan kecepatan dan arah
void tm1637_display_speed_direction(uint16_t speed, uint8_t motor_direction_flag)
{
  uint8_t display_segments[4]; // Array untuk menampung data segmen keempat digit

  // Digit 0 (paling kiri): Arah putaran
  // Asumsi: g_motor_direction = 1 berarti arah yang akan ditampilkan dengan '-'
  //         g_motor_direction = 0 berarti arah lain (kosong atau simbol lain)
  if (motor_direction_flag == 1)
  { // Jika arah perlu ditampilkan sebagai minus
    display_segments[0] = tm1637_segment_map[SEG_IDX_MINUS];
  }
  else
  {
    display_segments[0] = tm1637_segment_map[SEG_IDX_BLANK]; // Kosong untuk arah default
  }

  // Digit 1, 2, 3 (tiga digit paling kanan): Kecepatan
  // Batasi tampilan kecepatan hingga 999 jika MAX_SPEED_LEVEL Anda mendukungnya
  if (speed > 999)
  {
    speed = 999; // Tampilkan maksimal 999
  }

  // Ekstrak digit kecepatan (0-999)
  display_segments[1] = tm1637_segment_map[(speed / 100) % 10]; // Ratusan
  display_segments[2] = tm1637_segment_map[(speed / 10) % 10];  // Puluhan
  display_segments[3] = tm1637_segment_map[speed % 10];         // Satuan

  // Kirim data segmen ke TM1637
  tm1637_start();
  tm1637_write_byte_ack(TM1637_CMD_SET_DATA); // Perintah setting data: mode auto-increment address
  tm1637_stop();

  tm1637_start();
  tm1637_write_byte_ack(TM1637_CMD_SET_ADDR); // Set alamat awal ke digit paling kiri (0xC0)
  for (uint8_t i = 0; i < 4; i++)
  {
    tm1637_write_byte_ack(display_segments[i]);
  }
  tm1637_stop();

  tm1637_start();
  tm1637_write_byte_ack(TM1637_CMD_STOP_DSIPLAY);
  tm1637_stop();

  // Pastikan display menyala dengan brightness yang diinginkan (mungkin sudah di-set di tm1637_init)
  // tm1637_set_brightness(7); // Baris ini bisa ada atau tidak, tergantung preferensi
}

// Modifikasi tm1637_init untuk menggunakan fungsi display baru
void tm1637_init(void)
{
  // GPIOs are configured by CubeMX
  tm1637_set_brightness(7); // Set brightness and turn display on
  // Panggil fungsi display baru dengan nilai awal
  // Asumsikan arah awal adalah 0 (tidak minus) dan kecepatan awal 0
  tm1637_display_speed_direction(0, 0);
}

void stepper_update_speed_timer(void)
{
  // This function now uses g_current_speed_level which is gradually changed
  if (!g_motor_is_running || g_current_speed_level < MIN_SPEED_LEVEL)
  {
    HAL_TIM_Base_Stop_IT(&htim2);
    if (g_current_speed_level == 0 && g_motor_is_running)
    {
      g_motor_is_running = 0;                                                // Motor has effectively stopped
      HAL_GPIO_WritePin(ENABLE_PIN_GPIO_Port, ENABLE_PIN_Pin, GPIO_PIN_SET); // Disable driver if fully stopped
      // printf("Motor fully stopped (ARR update).\n"); // Already printed in accel/decel logic
    }
    return;
  }

  HAL_TIM_Base_Stop_IT(&htim2); // Stop timer before changing period

  uint32_t new_arr_value;
  if (g_current_speed_level <= MIN_SPEED_LEVEL)
  {
    new_arr_value = PULSE_TIMER_BASE_ARR; // ARR for the slowest defined speed
  }
  else
  {
    // Calculate ARR: higher speed level means smaller ARR (faster pulses)
    // Ensure the subtraction doesn't go too far with very high speed_levels
    uint32_t arr_reduction = (g_current_speed_level - MIN_SPEED_LEVEL) * PULSE_TIMER_ARR_STEP;

    if (arr_reduction >= PULSE_TIMER_BASE_ARR)
    { // Prevents underflow if reduction is too large
      new_arr_value = MINIMUM_TIMER_ARR_VALUE;
    }
    else
    {
      new_arr_value = PULSE_TIMER_BASE_ARR - arr_reduction;
    }
  }

  // Corrected Sanity check for ARR value
  if (new_arr_value < MINIMUM_TIMER_ARR_VALUE)
  {
    new_arr_value = MINIMUM_TIMER_ARR_VALUE; // Cap at minimum ARR (highest frequency)
  }
  if (new_arr_value > PULSE_TIMER_BASE_ARR)
  {                                       // Should not happen with current logic if MIN_SPEED_LEVEL is handled
    new_arr_value = PULSE_TIMER_BASE_ARR; // Cap at maximum ARR (slowest frequency)
  }

  __HAL_TIM_SET_AUTORELOAD(&htim2, new_arr_value);
  __HAL_TIM_SET_COUNTER(&htim2, 0); // Reset counter to apply new period immediately
  HAL_TIM_Base_Start_IT(&htim2);
}

// No direct stepper_enable/disable_motor. Instead, change g_motor_enabled_target.
// The accel/decel logic will handle actual start/stop.

void stepper_set_direction(uint8_t dir)
{
  HAL_GPIO_WritePin(DIR_PIN_GPIO_Port, DIR_PIN_Pin, (dir == 0) ? GPIO_PIN_RESET : GPIO_PIN_SET);
  g_motor_direction = dir;
  printf("Target Direction set to: %s\n", dir == 0 ? "CW" : "CCW");
}

// Accel/Decel handler (call this in the main loop)
void handle_acceleration_deceleration(void)
{
  uint32_t current_tick = HAL_GetTick();

  if (g_motor_enabled_target && g_current_speed_level < g_target_speed_level)
  {
    // Accelerate
    if (current_tick - g_last_accel_decel_tick >= g_accel_step_delay_ms)
    {
      if (!g_motor_is_running)
      {                                                                          // First step of enabling
        HAL_GPIO_WritePin(ENABLE_PIN_GPIO_Port, ENABLE_PIN_Pin, GPIO_PIN_RESET); // Enable driver
        g_motor_is_running = 1;
        printf("Motor starting (enabling driver)...\n");
      }
      g_current_speed_level++;
      stepper_update_speed_timer();
      g_last_accel_decel_tick = current_tick;
      // printf("Accel: Current Speed Level: %lu\n", g_current_speed_level);
    }
  }
  else if ((!g_motor_enabled_target && g_current_speed_level > 0) || (g_motor_enabled_target && g_current_speed_level > g_target_speed_level))
  {
    // Decelerate (either to stop or to a lower target speed)
    if (current_tick - g_last_accel_decel_tick >= g_decel_step_delay_ms)
    {
      g_current_speed_level--;
      stepper_update_speed_timer(); // This will stop timer if g_current_speed_level reaches 0
      g_last_accel_decel_tick = current_tick;
      // printf("Decel: Current Speed Level: %lu\n", g_current_speed_level);
      if (g_current_speed_level == 0 && !g_motor_enabled_target)
      {
        // If we were decelerating to stop, and now at 0
        // stepper_update_speed_timer would have called HAL_TIM_Base_Stop_IT
        // and also set g_motor_is_running = 0 and disabled EN pin.
      }
    }
  }
  else if (g_motor_enabled_target && g_current_speed_level == 0 && g_target_speed_level >= MIN_SPEED_LEVEL && !g_motor_is_running)
  {
    // Special case: Motor is enabled, target speed > 0, but current speed is 0 (e.g. just enabled)
    // Force first step of acceleration
    if (current_tick - g_last_accel_decel_tick >= g_accel_step_delay_ms)
    {
      HAL_GPIO_WritePin(ENABLE_PIN_GPIO_Port, ENABLE_PIN_Pin, GPIO_PIN_RESET); // Enable driver
      g_motor_is_running = 1;
      g_current_speed_level++;
      stepper_update_speed_timer();
      g_last_accel_decel_tick = current_tick;
      printf("Motor starting up, Accel: Current Speed Level: %lu\n", g_current_speed_level);
    }
  }

  // If target speed is 0 but motor_enabled_target is true, it implies we want to hold position (if driver supports)
  // For now, this logic assumes target_speed_level is MIN_SPEED_LEVEL if enabled.
  // Or, if g_target_speed_level is set to 0 explicitly, it will decelerate to 0.
  if (g_motor_enabled_target && g_target_speed_level == 0 && g_current_speed_level > 0)
  {
    // Decelerate to stop if target is 0 but motor is "enabled" (meaning it was running)
    if (current_tick - g_last_accel_decel_tick >= g_decel_step_delay_ms)
    {
      g_current_speed_level--;
      stepper_update_speed_timer();
      g_last_accel_decel_tick = current_tick;
    }
  }
}

void handle_buttons(void)
{
  uint32_t current_tick = HAL_GetTick();

  // Speed Up Button (PB6)
  if (HAL_GPIO_ReadPin(BTN_SPEED_UP_GPIO_Port, BTN_SPEED_UP_Pin) == GPIO_PIN_RESET)
  {
    if (current_tick - g_last_btn_speed_up_time > DEBOUNCE_DELAY_MS)
    {
      if (g_target_speed_level < MAX_SPEED_LEVEL)
      {
        g_target_speed_level++;
        printf("Target Speed Increased: %lu\n", g_target_speed_level);
      }
      g_last_btn_speed_up_time = current_tick;
    }
  }

  // Speed Down Button (PB7)
  if (HAL_GPIO_ReadPin(BTN_SPEED_DOWN_GPIO_Port, BTN_SPEED_DOWN_Pin) == GPIO_PIN_RESET)
  {
    if (current_tick - g_last_btn_speed_down_time > DEBOUNCE_DELAY_MS)
    {
      // Allow decreasing target speed to 0, which means "stop"
      if (g_target_speed_level > 0)
      { // Can go down to 0
        g_target_speed_level--;
        printf("Target Speed Decreased: %lu\n", g_target_speed_level);
      }
      if (g_target_speed_level == 0 && g_motor_enabled_target)
      {
        // If motor is enabled and target speed becomes 0, initiate stop
        printf("Target speed set to 0, motor will decelerate and stop.\n");
      }
      g_last_btn_speed_down_time = current_tick;
    }
  }

  // Direction Button (PB8)
  if (HAL_GPIO_ReadPin(BTN_DIR_GPIO_Port, BTN_DIR_Pin) == GPIO_PIN_RESET)
  {
    if (current_tick - g_last_btn_dir_time > DEBOUNCE_DELAY_MS)
    {
      stepper_set_direction(!g_motor_direction); // Toggles target direction
      g_last_btn_dir_time = current_tick;
    }
  }

  // Enable/Disable Button (PB9) - This now sets the *target* enabled state
  if (HAL_GPIO_ReadPin(BTN_ENABLE_GPIO_Port, BTN_ENABLE_Pin) == GPIO_PIN_RESET)
  {
    if (current_tick - g_last_btn_enable_time > DEBOUNCE_DELAY_MS)
    {
      g_motor_enabled_target = !g_motor_enabled_target;
      if (g_motor_enabled_target)
      {
        printf("Motor Enable Target: ON\n");
        if (g_target_speed_level == 0)
          g_target_speed_level = MIN_SPEED_LEVEL; // If enabling, ensure a minimum run speed
      }
      else
      {
        printf("Motor Enable Target: OFF (will decelerate to stop)\n");
        // Deceleration to 0 will be handled by handle_acceleration_deceleration
      }
      g_last_btn_enable_time = current_tick;
    }
  }

  // Save Settings Button (PA0)
  if (HAL_GPIO_ReadPin(BTN_SAVE_SETTINGS_GPIO_Port, BTN_SAVE_SETTINGS_Pin) == GPIO_PIN_SET)
  {
    if (current_tick - g_last_btn_save_time > DEBOUNCE_DELAY_MS)
    {
      g_save_settings_flag = 1; // Set flag to save in main loop
      printf("Save settings button pressed.\n");
      g_last_btn_save_time = current_tick;
    }
  }
}

uint32_t calculate_checksum(StepperSettings_TypeDef *settings)
{
  uint8_t *p_data = (uint8_t *)settings;
  uint32_t chk = 0;
  // Calculate checksum over all fields except the checksum field itself
  for (uint16_t i = 0; i < (sizeof(StepperSettings_TypeDef) - sizeof(uint32_t)); i++)
  {
    chk += p_data[i];
  }
  return chk;
}

void load_settings_from_flash(void)
{
  printf("Loading settings from Flash...\n");
  StepperSettings_TypeDef temp_settings;
  memcpy(&temp_settings, (void *)FLASH_SETTINGS_PAGE_ADDR, sizeof(StepperSettings_TypeDef));

  if (calculate_checksum(&temp_settings) == temp_settings.checksum)
  {
    g_persistent_settings = temp_settings;
    // Apply loaded settings
    g_target_speed_level = g_persistent_settings.speed_level_target;
    if (g_target_speed_level == 0)
      g_target_speed_level = MIN_SPEED_LEVEL; // Ensure a valid running speed if enabled
    if (g_target_speed_level > MAX_SPEED_LEVEL)
      g_target_speed_level = MAX_SPEED_LEVEL;

    g_motor_direction = g_persistent_settings.direction;
    g_motor_enabled_target = g_persistent_settings.enabled_on_startup; // This is the target state
    g_accel_step_delay_ms = g_persistent_settings.accel_delay_ms;
    g_decel_step_delay_ms = g_persistent_settings.decel_delay_ms;

    // Sanitize loaded accel/decel delays
    if (g_accel_step_delay_ms < 10)
      g_accel_step_delay_ms = 10;
    if (g_decel_step_delay_ms < 10)
      g_decel_step_delay_ms = 10;

    printf("Settings loaded: SpeedTgt=%lu, Dir=%d, EnStartup=%d, Accel=%lums, Decel=%lums\n",
           g_target_speed_level, g_motor_direction, g_motor_enabled_target,
           g_accel_step_delay_ms, g_decel_step_delay_ms);
  }
  else
  {
    printf("Flash checksum error or no valid settings. Loading defaults.\n");
    // Load default settings if checksum fails or first run
    g_target_speed_level = DEFAULT_SPEED_LEVEL;
    g_motor_direction = 0;
    g_motor_enabled_target = 0; // Default to disabled
    g_accel_step_delay_ms = DEFAULT_ACCEL_DELAY_MS;
    g_decel_step_delay_ms = DEFAULT_DECEL_DELAY_MS;
    // Optionally save these defaults back to flash
    // g_save_settings_flag = 1; // To save defaults on next opportunity
  }
  // Initialize current speed to 0, actual running state will be handled by enable logic
  g_current_speed_level = 0;
}

void save_settings_to_flash(void)
{
  printf("Preparing to save settings to Flash...\n");
  HAL_FLASH_Unlock();

  FLASH_EraseInitTypeDef EraseInitStruct;
  uint32_t PageError = 0;
  EraseInitStruct.TypeErase = FLASH_TYPEERASE_PAGES;
  EraseInitStruct.PageAddress = FLASH_SETTINGS_PAGE_ADDR;
  EraseInitStruct.NbPages = 1;

  if (HAL_FLASHEx_Erase(&EraseInitStruct, &PageError) != HAL_OK)
  {
    printf("Flash erase failed! Error: %lu\n", PageError);
    HAL_FLASH_Lock();
    return;
  }
  printf("Flash page erased.\n");

  // Prepare settings to save
  g_persistent_settings.speed_level_target = g_target_speed_level;
  g_persistent_settings.direction = g_motor_direction;
  g_persistent_settings.enabled_on_startup = g_motor_enabled_target;
  g_persistent_settings.accel_delay_ms = g_accel_step_delay_ms;
  g_persistent_settings.decel_delay_ms = g_decel_step_delay_ms;
  g_persistent_settings.checksum = calculate_checksum(&g_persistent_settings);

  // Program word by word (uint32_t)
  uint32_t *p_data = (uint32_t *)&g_persistent_settings;
  uint16_t num_words = sizeof(StepperSettings_TypeDef) / sizeof(uint32_t);
  if (sizeof(StepperSettings_TypeDef) % sizeof(uint32_t) != 0)
  {
    // If not perfectly divisible, may need to handle last few bytes differently or pad struct
    // For simplicity, ensure StepperSettings_TypeDef size is a multiple of 4 bytes.
  }

  for (uint16_t i = 0; i < num_words; i++)
  {
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, FLASH_SETTINGS_PAGE_ADDR + (i * 4), p_data[i]) != HAL_OK)
    {
      printf("Flash program failed at word %u!\n", i);
      HAL_FLASH_Lock();
      return;
    }
  }

  HAL_FLASH_Lock();
  printf("Settings saved to Flash: SpeedTgt=%lu, Dir=%lu, EnStartup=%lu, Accel=%lums, Decel=%lums, Chksum=%lu\n",
         g_persistent_settings.speed_level_target, g_persistent_settings.direction,
         g_persistent_settings.enabled_on_startup, g_persistent_settings.accel_delay_ms,
         g_persistent_settings.decel_delay_ms, g_persistent_settings.checksum);
  g_save_settings_flag = 0; // Clear flag
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
  MX_USB_DEVICE_Init();
  MX_TIM2_Init();
  /* USER CODE BEGIN 2 */
  printf("Stepper Motor Control Initializing...\n");

  load_settings_from_flash(); // Load saved settings

  stepper_set_direction(g_motor_direction); // Apply loaded/default direction

  // If loaded settings indicate motor should be enabled on startup:
  if (g_persistent_settings.enabled_on_startup)
  {
    g_motor_enabled_target = 1;
    // Accel/decel logic will start it up to g_target_speed_level
    printf("Motor target set to ENABLED on startup. Target speed: %lu\n", g_target_speed_level);
  }
  else
  {
    g_motor_enabled_target = 0;
    g_current_speed_level = 0;                                             // Ensure it starts at 0 if not enabled on startup
    HAL_GPIO_WritePin(ENABLE_PIN_GPIO_Port, ENABLE_PIN_Pin, GPIO_PIN_SET); // Ensure driver is disabled
    g_motor_is_running = 0;
    printf("Motor target set to DISABLED on startup.\n");
  }
  g_last_accel_decel_tick = HAL_GetTick();

  printf("System Initialized. Current Speed: %lu, Target Speed: %lu\n", g_current_speed_level, g_target_speed_level);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    handle_buttons();
    handle_acceleration_deceleration();

    if (g_save_settings_flag)
    {
      save_settings_to_flash(); // Perform save operation
      g_save_settings_flag = 0; // Reset flag
    }

    if (g_motor_is_running)
    {
      tm1637_display_speed_direction(g_current_speed_level, g_motor_direction);
      // tm1637_display_number(g_current_speed_level);
      HAL_GPIO_WritePin(LED_BLUEPILL_GPIO_Port, LED_BLUEPILL_Pin, GPIO_PIN_SET); // turn on led
    }
    else
    {
      tm1637_display_speed_direction(g_target_speed_level, g_motor_direction);
      // tm1637_display_number(g_current_speed_level);
      HAL_GPIO_WritePin(LED_BLUEPILL_GPIO_Port, LED_BLUEPILL_Pin, GPIO_PIN_RESET);
    }

    //    HAL_Delay(10); // Main loop delay - Accel/Decel timing is based on HAL_GetTick()

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
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
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
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB;
  PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_PLL_DIV1_5;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 2;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 65535;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, DIR_PIN_Pin|ENABLE_PIN_Pin|PULSE_PIN_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, LED_BLUEPILL_Pin|TM1637_CLK_Pin|TM1637_DIO_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : BTN_SAVE_SETTINGS_Pin */
  GPIO_InitStruct.Pin = BTN_SAVE_SETTINGS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(BTN_SAVE_SETTINGS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : DIR_PIN_Pin ENABLE_PIN_Pin PULSE_PIN_Pin */
  GPIO_InitStruct.Pin = DIR_PIN_Pin|ENABLE_PIN_Pin|PULSE_PIN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : LED_BLUEPILL_Pin */
  GPIO_InitStruct.Pin = LED_BLUEPILL_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_BLUEPILL_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : TM1637_CLK_Pin TM1637_DIO_Pin */
  GPIO_InitStruct.Pin = TM1637_CLK_Pin|TM1637_DIO_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : BTN_SPEED_UP_Pin BTN_SPEED_DOWN_Pin BTN_DIR_Pin BTN_ENABLE_Pin */
  GPIO_InitStruct.Pin = BTN_SPEED_UP_Pin|BTN_SPEED_DOWN_Pin|BTN_DIR_Pin|BTN_ENABLE_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM2)
  {
    if (g_motor_is_running && g_current_speed_level >= MIN_SPEED_LEVEL) // Check if motor is actually running
    {
      HAL_GPIO_TogglePin(PULSE_PIN_GPIO_Port, PULSE_PIN_Pin); // Use defines from main.h
    }
  }
}

// USB CDC Receive Callback
// This function will be defined as a weak symbol in usbd_cdc_if.c
// You need to implement it here or in usbd_cdc_if.c (outside weak definition).
// For simplicity, let's assume you put it in main.c
// Make sure you have the prototype for CDC_Transmit_FS if you use it.
// extern int8_t CDC_Transmit_FS(uint8_t* Buf, uint16_t Len);

void CDC_On_Receive(uint8_t *Buf, uint32_t Len)
{
  Buf[Len] = '\0'; // Null-terminate the received data
  printf("USB RX: %s\n", (char *)Buf);

  char cmd_buffer[64]; // Local buffer for strtok
  strncpy(cmd_buffer, (char *)Buf, sizeof(cmd_buffer) - 1);
  cmd_buffer[sizeof(cmd_buffer) - 1] = '\0';

  char *token = strtok(cmd_buffer, "=");
  if (token == NULL)
    return;

  if (strcmp(token, "$ACCEL") == 0)
  {
    char *value_str = strtok(NULL, "=");
    if (value_str != NULL)
    {
      uint32_t val = atoi(value_str);
      if (val >= 10 && val <= 10000)
      { // Min 10ms, Max 10s delay per step
        g_accel_step_delay_ms = val;
        printf("Accel delay set to: %lu ms\n", g_accel_step_delay_ms);
      }
      else
      {
        printf("Invalid accel value. Range: 10-10000\n");
      }
    }
  }
  else if (strcmp(token, "$DECEL") == 0)
  {
    char *value_str = strtok(NULL, "=");
    if (value_str != NULL)
    {
      uint32_t val = atoi(value_str);
      if (val >= 10 && val <= 10000)
      {
        g_decel_step_delay_ms = val;
        printf("Decel delay set to: %lu ms\n", g_decel_step_delay_ms);
      }
      else
      {
        printf("Invalid decel value. Range: 10-10000\n");
      }
    }
  }
  else if (strcmp(token, "$$") == 0)
  { // Command to print current settings
    char tx_buf[200];
    snprintf(tx_buf, sizeof(tx_buf),
             "TargetSpeed:%lu, CurrentSpeed:%lu, Dir:%u, EnabledTarget:%u, Accel:%lums, Decel:%lums\r\n",
             g_target_speed_level, g_current_speed_level, g_motor_direction, g_motor_enabled_target,
             g_accel_step_delay_ms, g_decel_step_delay_ms);
    CDC_Transmit_FS((uint8_t *)tx_buf, strlen(tx_buf));
  }
  else if (strcmp(token, "$SAVE") == 0)
  { // Command to trigger save
    g_save_settings_flag = 1;
    printf("Save requested via USB.\n");
    CDC_Transmit_FS((uint8_t *)"Settings will be saved.\r\n", strlen("Settings will be saved.\r\n"));
  }
  // Add other USB commands as needed
}

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
