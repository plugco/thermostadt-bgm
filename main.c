/***************************************************************************//**
 * @file
 * @brief Silicon Labs Thermometer Example Application
 * This Thermometer and OTA example allows the user to measure temperature
 * using the temperature sensor on the WSTK. The values can be read with the
 * Health Thermometer reader on the Blue Gecko smartphone app.
 *******************************************************************************
 * # License
 * <b>Copyright 2018 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inc. Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement. This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 ******************************************************************************/

/* Board Headers */
#include "init_mcu.h"
#include "init_board.h"
#include "init_app.h"
#include "ble-configuration.h"
#include "board_features.h"

/* Bluetooth stack headers */
#include "bg_types.h"
#include "native_gecko.h"
#include "infrastructure.h"

/* GATT database */
#include "gatt_db.h"

/* EM library (EMlib) */
#include "em_system.h"

/* Libraries containing default Gecko configuration values */
#include "em_emu.h"
#include "em_cmu.h"

/* Device initialization header */
#include "hal-config.h"

#ifdef FEATURE_BOARD_DETECTED
#if defined(HAL_CONFIG)
#include "bsphalconfig.h"
#else
#include "bspconfig.h"
#endif
#else
#error This sample app only works with a Silicon Labs Board
#endif

#ifdef FEATURE_I2C_SENSOR
#include "i2cspm.h"
#include "si7013.h"
#include "tempsens.h"
#endif

#include "stdio.h"
#include "graphics.h"

/***********************************************************************************************//**
 * @addtogroup Application
 * @{
 **************************************************************************************************/

/***********************************************************************************************//**
 * @addtogroup app
 * @{
 **************************************************************************************************/

/* Gecko configuration parameters (see gecko_configuration.h) */
#ifndef MAX_CONNECTIONS
#define MAX_CONNECTIONS 4
#endif
uint8_t bluetooth_stack_heap[DEFAULT_BLUETOOTH_HEAP(MAX_CONNECTIONS)];

static const gecko_configuration_t config = {
  .config_flags = 0,
#if defined(FEATURE_LFXO) || defined(PLFRCO_PRESENT)
  .sleep.flags = SLEEP_FLAGS_DEEP_SLEEP_ENABLE,
#else
  .sleep.flags = 0,
#endif
  .bluetooth.max_connections = MAX_CONNECTIONS,
  .bluetooth.heap = bluetooth_stack_heap,
  .bluetooth.heap_size = sizeof(bluetooth_stack_heap),
#if defined(FEATURE_LFXO)
  .bluetooth.sleep_clock_accuracy = 100, // ppm
#elif defined(PLFRCO_PRESENT)
  .bluetooth.sleep_clock_accuracy = 500, // ppm
#endif
  .gattdb = &bg_gattdb_data,
  .ota.flags = 0,
  .ota.device_name_len = 3,
  .ota.device_name_ptr = "OTA",
  .pa.config_enable = 1, // Set this to be a valid PA config
#if defined(FEATURE_PA_INPUT_FROM_VBAT)
  .pa.input = GECKO_RADIO_PA_INPUT_VBAT, // Configure PA input to VBAT
#else
  .pa.input = GECKO_RADIO_PA_INPUT_DCDC,
#endif // defined(FEATURE_PA_INPUT_FROM_VBAT)
  .rf.flags = GECKO_RF_CONFIG_ANTENNA,                 /* Enable antenna configuration. */
  .rf.antenna = GECKO_RF_ANTENNA,                      /* Select antenna path! */
};

/* Flag for indicating DFU Reset must be performed */
uint8_t boot_to_dfu = 0;

uint8_t outputs = 0;

uint32_t readUint32(uint16_t handle)
{
	struct gecko_msg_gatt_server_read_attribute_value_rsp_t * rsp = gecko_cmd_gatt_server_read_attribute_value(handle, 0);
	return *((uint32_t *) rsp->value.data);
}

void pinOutSetValue(GPIO_Port_TypeDef port, unsigned int pin, uint32_t value)
{
	if (value)
		GPIO_PinOutSet(port, pin);
	else
		GPIO_PinOutClear(port, pin);
}

#define HEATING 0x01
#define COOLING 0x02
#define MOISTENING 0x04
#define DRYING 0x08

/**
 * @brief Function for taking a single temperature measurement with the WSTK Relative Humidity and Temperature (RHT) sensor.
 */
void temperatureMeasure()
{
  int32_t tempData;
  uint32_t rhData = 0;
  char s[32];

  const uint32_t humidity_high_hyst = readUint32(gattdb_thermostadt_humidity_hyst_high);
  const uint32_t humidity_set = readUint32(gattdb_thermostadt_humidity_set);
  const uint32_t humidity_low_hyst = readUint32(gattdb_thermostadt_humidity_hyst_low);
  const int32_t temperature_high_hyst = readUint32(gattdb_thermostadt_temperature_hyst_high);
  const int32_t temperature_set = readUint32(gattdb_thermostadt_temperature_set);
  const int32_t temperature_low_hyst = readUint32(gattdb_thermostadt_temperature_hyst_low);

#ifdef FEATURE_I2C_SENSOR
  /* Sensor relative humidity and temperature measurement returns 0 on success, nonzero otherwise */
  if (Si7013_MeasureRHAndTemp(I2C0, SI7021_ADDR, &rhData, &tempData) != 0)
#endif
  {
    tempData = -100000;
    rhData = 0;
  }

  if (tempData <= temperature_set - temperature_low_hyst)
	  outputs |= HEATING;
  else if (tempData >= temperature_set)
	  outputs &= ~HEATING;

  if (tempData >= temperature_set + temperature_high_hyst)
	  outputs |= COOLING;
  else if (tempData <= temperature_set)
	  outputs &= ~COOLING;

  if (rhData <= humidity_set - humidity_low_hyst)
	  outputs |= MOISTENING;
  else if (rhData >= humidity_set)
	  outputs &= ~MOISTENING;

  if (rhData >= humidity_set + humidity_high_hyst)
	  outputs |= DRYING;
  else if (rhData <= humidity_set)
	  outputs &= ~DRYING;

  pinOutSetValue(gpioPortF, 4, outputs & HEATING);
  pinOutSetValue(gpioPortF, 5, outputs & COOLING);
  pinOutSetValue(gpioPortF, 6, outputs & MOISTENING);
  pinOutSetValue(gpioPortF, 7, outputs & DRYING);
  gecko_cmd_gatt_server_write_attribute_value(gattdb_thermostadt_outputs, 0, 1, &outputs);

  GRAPHICS_Clear();

  GRAPHICS_AppendString("Thermostadt 1.0\n");
  GRAPHICS_AppendString("\n");

  sprintf(s, "Temp  %ld.%03ldC\n", tempData / 1000, tempData % 1000);
  GRAPHICS_AppendString(s);
  sprintf(s, "Humid %ld.%03ld%%\n", rhData / 1000, rhData % 1000);
  GRAPHICS_AppendString(s);
  GRAPHICS_AppendString("\n");

  if (outputs & HEATING)
	  GRAPHICS_AppendString("Heating\n");
  else if (outputs & COOLING)
	  GRAPHICS_AppendString("Cooling\n");
  else
	  GRAPHICS_AppendString("\n");

  if (outputs & MOISTENING)
	  GRAPHICS_AppendString("Humidifying\n");
  else if (outputs & DRYING)
	  GRAPHICS_AppendString("Dehumidifying\n");
  else
	  GRAPHICS_AppendString("\n");

  GRAPHICS_Update();

  gecko_cmd_gatt_server_write_attribute_value(gattdb_thermostadt_temperature, 0, 4, (uint8_t *) &tempData);
  gecko_cmd_gatt_server_write_attribute_value(gattdb_thermostadt_humidity, 0, 4, (uint8_t *) &rhData);
}

/**
 * @brief  Main function
 */
int main(void)
{
  // Initialize device
  initMcu();
  // Initialize board
  initBoard();
  // Initialize application
  initApp();
  initVcomEnable();
  // init LCD
  GRAPHICS_Init();
  // Initialize stack
  gecko_init(&config);

  GPIO_PinOutClear(gpioPortF, 4);
  GPIO_PinOutClear(gpioPortF, 5);
  GPIO_PinOutClear(gpioPortD, 11);
  GPIO_PinOutClear(gpioPortD, 12);
  GPIO_PinModeSet(gpioPortF, 4, gpioModePushPull, 0);
  GPIO_PinModeSet(gpioPortF, 5, gpioModePushPull, 0);
  GPIO_PinModeSet(gpioPortD, 11, gpioModePushPull, 0);
  GPIO_PinModeSet(gpioPortD, 12, gpioModePushPull, 0);

  GPIO_PinOutSet(gpioPortF, 6);
  GPIO_PinModeSet(gpioPortF, 6, gpioModeInputPull, 1);
  GPIO_PinOutSet(gpioPortF, 6);

  #ifdef FEATURE_I2C_SENSOR
  // Initialize the Temperature Sensor
  Si7013_Detect(I2C0, SI7021_ADDR, NULL);
  #endif
  while (1) {
    /* Event pointer for handling events */
    struct gecko_cmd_packet* evt;

    /* Check for stack event. */
    evt = gecko_wait_event();

    /* Handle events */
    switch (BGLIB_MSG_ID(evt->header)) {
      /* This boot event is generated when the system boots up after reset.
       * Do not call any stack commands before receiving the boot event.
       * Here the system is set to start advertising immediately after boot procedure. */
      case gecko_evt_system_boot_id:
        /* Set advertising parameters. 100ms advertisement interval.
         * The first two parameters are minimum and maximum advertising interval, both in
         * units of (milliseconds * 1.6). */
        gecko_cmd_le_gap_set_advertise_timing(0, 160, 160, 0, 0);

        /* Start general advertising and enable connections. */
        gecko_cmd_le_gap_start_advertising(0, le_gap_general_discoverable, le_gap_connectable_scannable);

        /* Start the repeating timer. The 1st parameter '32768'
         * tells the timer to run for 1 second (32.768 kHz oscillator), the 2nd parameter is
         * the timer handle and the 3rd parameter '0' tells the timer to repeat continuously until
         * stopped manually.*/
        gecko_cmd_hardware_set_soft_timer(32768, 0, 0);

        gecko_cmd_sm_configure(2, 3);
        gecko_cmd_sm_set_bondable_mode(1);
        if (!GPIO_PinInGet(gpioPortF, 6))
        {
        	gecko_cmd_sm_delete_bondings();
        }
        break;

      /* This event is generated when a connected client has either
       * 1) changed a Characteristic Client Configuration, meaning that they have enabled
       * or disabled Notifications or Indications, or
       * 2) sent a confirmation upon a successful reception of the indication. */
//      case gecko_evt_gatt_server_characteristic_status_id:
//        /* Check that the characteristic in question is temperature - its ID is defined
//         * in gatt.xml as "temperature_measurement". Also check that status_flags = 1, meaning that
//         * the characteristic client configuration was changed (notifications or indications
//         * enabled or disabled). */
//        if ((evt->data.evt_gatt_server_characteristic_status.characteristic == gattdb_temperature_measurement)
//            && (evt->data.evt_gatt_server_characteristic_status.status_flags == 0x01)) {
//          if (evt->data.evt_gatt_server_characteristic_status.client_config_flags == 0x02) {
//            /* Indications have been turned ON - start the repeating timer. The 1st parameter '32768'
//             * tells the timer to run for 1 second (32.768 kHz oscillator), the 2nd parameter is
//             * the timer handle and the 3rd parameter '0' tells the timer to repeat continuously until
//             * stopped manually.*/
//            gecko_cmd_hardware_set_soft_timer(32768, 0, 0);
//          } else if (evt->data.evt_gatt_server_characteristic_status.client_config_flags == 0x00) {
//            /* Indications have been turned OFF - stop the timer. */
//            gecko_cmd_hardware_set_soft_timer(0, 0, 0);
//          }
//        }
//        break;

      /* This event is generated when the software timer has ticked. In this example the temperature
       * is read after every 1 second and then the indication of that is sent to the listening client. */
      case gecko_evt_hardware_soft_timer_id:
        /* Measure the temperature as defined in the function temperatureMeasure() */
        temperatureMeasure();

        break;

      case gecko_evt_le_connection_closed_id:
        /* Check if need to boot to dfu mode */
        if (boot_to_dfu) {
          /* Enter to DFU OTA mode */
          gecko_cmd_system_reset(2);
        } else {
          /* Stop timer in case client disconnected before indications were turned off */
//          gecko_cmd_hardware_set_soft_timer(0, 0, 0);
          /* Restart advertising after client has disconnected */
          gecko_cmd_le_gap_start_advertising(0, le_gap_general_discoverable, le_gap_connectable_scannable);

          gecko_cmd_sm_configure(0b00110, 3);
          gecko_cmd_sm_set_bondable_mode(1);
        }
        break;

      /* Events related to OTA upgrading
         ----------------------------------------------------------------------------- */

      /* Checks if the user-type OTA Control Characteristic was written.
       * If written, boots the device into Device Firmware Upgrade (DFU) mode. */
      case gecko_evt_gatt_server_user_write_request_id:
        if (evt->data.evt_gatt_server_user_write_request.characteristic == gattdb_ota_control) {
          /* Set flag to enter to OTA mode */
          boot_to_dfu = 1;
          /* Send response to Write Request */
          gecko_cmd_gatt_server_send_user_write_response(
            evt->data.evt_gatt_server_user_write_request.connection,
            gattdb_ota_control,
            bg_err_success);

          /* Close connection to enter to DFU OTA mode */
          gecko_cmd_le_connection_close(evt->data.evt_gatt_server_user_write_request.connection);
        }
        break;

      case gecko_evt_sm_confirm_bonding_id:
    	  gecko_cmd_sm_bonding_confirm(evt->data.evt_sm_confirm_bonding.connection, 1);
    	  break;

      default:
        break;
    }
  }
}

/** @} (end addtogroup app) */
/** @} (end addtogroup Application) */
