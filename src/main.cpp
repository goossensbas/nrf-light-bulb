/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

 #include "app_task.h"

 #include <zephyr/logging/log.h>
 #include <zephyr/kernel.h>
 #include <zephyr/devicetree.h>
 #include <zephyr/drivers/gpio.h>
 #include <openthread/thread.h>
 #include <openthread/joiner.h>
 #include <openthread/platform/radio.h> // For otPlatRadioGetIeeeEui64
 #include <openthread/dataset.h>
 
 #define THREAD_JOINER_TIMEOUT 900 // 15 minutes in seconds
 #define THREAD_JOINER_DISCOVERY_TIMEOUT 900 // 15 minutes in seconds
 
 static const struct gpio_dt_spec button1 = GPIO_DT_SPEC_GET(DT_ALIAS(sw1), gpios);
 static struct gpio_callback button1_cb_data;
 
 static const struct gpio_dt_spec button2 = GPIO_DT_SPEC_GET(DT_ALIAS(sw2), gpios);
 static struct gpio_callback button2_cb_data;
 
 // Global flag to indicate Thread Joiner completion
 static volatile bool is_thread_joiner_complete = false;
 
 otInstance *instance;
 // Work item for starting the Thread Joiner
 struct k_work joiner_start_work;
 
 //function proto's
 void StartThreadJoiner(otInstance *instance);
 void JoinerCallback(otError result, void *context);
 
 LOG_MODULE_REGISTER(app, CONFIG_CHIP_APP_LOG_LEVEL);
 
 // Joiner work handler
 void joiner_start_work_handler(struct k_work *work)
 {
     LOG_INF("Thread Joiner Work: Starting Joiner...");
     StartThreadJoiner(instance);
 }
 
 void StartThreadJoiner(otInstance *instance)
 {
     if (instance == NULL)
     {
         LOG_ERR("Failed to get OpenThread instance");
         return;
     }
 
     // Retrieve the EUI64
     uint8_t eui64[OT_EXT_ADDRESS_SIZE];
     otPlatRadioGetIeeeEui64(instance, eui64);
 
     // Print the EUI64 to the console
     LOG_INF("Device EUI64: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
             eui64[0], eui64[1], eui64[2], eui64[3],
             eui64[4], eui64[5], eui64[6], eui64[7]);
 
     // Start the Thread Joiner
     const char *pskd = "J01NME"; 
     otError error = otJoinerStart(instance, pskd, NULL, NULL, NULL, NULL, NULL, JoinerCallback, NULL);
 
     if (error == OT_ERROR_NONE)
     {
         LOG_INF("Thread Joiner started successfully with passphrase: %s", pskd);
     }
     else
     {
         LOG_ERR("Failed to start Thread Joiner: %d", error);
     }
 }
 
 // Callback for Thread Joiner completion
 void JoinerCallback(otError result, void *context)
 {
     if (result == OT_ERROR_NONE)
     {
         LOG_INF("Thread Joiner completed successfully");
         is_thread_joiner_complete = true;
     }
     else
     {
         LOG_ERR("Thread Joiner failed: %d", result);
     }
 }
 
 void ClearThreadDataset(otInstance *instance) {
     otOperationalDataset dataset = {};
     otError error = otDatasetSetActive(instance, &dataset);
 
     if (error == OT_ERROR_NONE) {
         LOG_INF("Thread dataset cleared successfully");
     } else {
         LOG_ERR("Failed to clear Thread dataset: %d", error);
     }
 }
 
 void button1_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
 {
     LOG_INF("Button 1 (sw1 alias) pressed, scheduling Thread Joiner...");
     k_work_submit(&joiner_start_work);
 }
 
 struct k_work reset_work;
 
 void reset_work_handler(struct k_work *work) {
     LOG_INF("Performing full reset...");
     
     // Clear Thread dataset
     ClearThreadDataset(instance);
 
     // Perform Matter factory reset
     chip::DeviceLayer::ConfigurationMgr().InitiateFactoryReset();
 
     // Reset the device
     NVIC_SystemReset();
 }
 
 // Callback for Button 2 to reset everything
 void button2_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
 {
     LOG_INF("Button 2 pressed, scheduling full reset...");
     k_work_submit(&reset_work);
 }
 
 void init_reset_work() {
     k_work_init(&reset_work, reset_work_handler);
 }
 
 bool hasThreadDataset(otInstance *instance) {
     otOperationalDataset dataset;
     otError error = otDatasetGetActive(instance, &dataset);
 
     if (error == OT_ERROR_NONE) {
         LOG_INF("Active Thread dataset exists.");
         return true; // Dataset exists
     } else {
         LOG_INF("No active Thread dataset found.");
         return false; // No dataset
     }
 }
 
 void init_buttons()
 {
     if (!device_is_ready(button1.port)) {
         LOG_ERR("Button 1 GPIO port not ready");
         return;
     }
 
     gpio_pin_configure_dt(&button1, GPIO_INPUT);
     gpio_pin_interrupt_configure_dt(&button1, GPIO_INT_EDGE_TO_ACTIVE);
     gpio_init_callback(&button1_cb_data, button1_pressed, BIT(button1.pin));
     gpio_add_callback(button1.port, &button1_cb_data);
 
     k_work_init(&joiner_start_work, joiner_start_work_handler);
 
     LOG_INF("Button 1 configured successfully");
     
 }
 
 // Function to initialize Button 2
 void init_reset_button()
 {
     if (!device_is_ready(button2.port)) {
         LOG_ERR("Button 2 GPIO port not ready");
         return;
     }
 
     gpio_pin_configure_dt(&button2, GPIO_INPUT);
     gpio_pin_interrupt_configure_dt(&button2, GPIO_INT_EDGE_TO_ACTIVE);
     gpio_init_callback(&button2_cb_data, button2_pressed, BIT(button2.pin));
     gpio_add_callback(button2.port, &button2_cb_data);
 
     LOG_INF("Button 2 configured successfully");
 }
 
 int main()
 {
     // Initialize the reset button
     init_reset_button();
 
     instance = openthread_get_default_instance();
     if (instance == NULL) {
         LOG_ERR("Failed to retrieve OpenThread instance");
         return 0;
     }
     //start the IP interface
     otIp6SetEnabled(instance, true);
 
     // Check for existing Thread dataset
     if (hasThreadDataset(instance)) {
         LOG_INF("Thread network is already configured. Skipping joiner.");
     } else {
         // Initialize Button 1 for Thread Joiner
         init_buttons(); 
 
         LOG_INF("Waiting for Thread Joiner to complete...");
         while (!is_thread_joiner_complete) {
             k_sleep(K_SECONDS(1));
         }
     }
 
     // Thread Joiner completed successfully
     LOG_INF("Starting Thread network...");
     otError error;
 
     // Start OpenThread
     error = otThreadSetEnabled(instance, true);
     if (error == OT_ERROR_NONE)
     {
         LOG_INF("Thread network started successfully");
     }
     else
     {
         LOG_ERR("Failed to start Thread network: %d", error);
         return 0;
     }
     
     LOG_INF("Starting Matter application...");
     CHIP_ERROR err = AppTask::Instance().StartApp();
 
 
     LOG_ERR("Exited with code %" CHIP_ERROR_FORMAT, err.Format());
     return err == CHIP_NO_ERROR ? EXIT_SUCCESS : EXIT_FAILURE;
 }
 