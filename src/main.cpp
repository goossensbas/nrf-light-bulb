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

#define THREAD_JOINER_TIMEOUT 900 // 15 minutes in seconds

// Global flag to indicate Thread Joiner completion
static volatile bool is_thread_joiner_complete = false;

static const struct gpio_dt_spec button1 = GPIO_DT_SPEC_GET(DT_ALIAS(sw1), gpios);
static struct gpio_callback button1_cb_data;


LOG_MODULE_REGISTER(app, CONFIG_CHIP_APP_LOG_LEVEL);

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

void StartThreadJoiner()
{
    otInstance *instance = openthread_get_default_instance();
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
	const char *pskd = NULL; // No PSKd since you're using EUI64
    otError error = otJoinerStart(instance, pskd, NULL, NULL, NULL, NULL, NULL, JoinerCallback, NULL);

    if (error == OT_ERROR_NONE)
    {
        LOG_INF("Thread Joiner started successfully");
    }
    else
    {
        LOG_ERR("Failed to start Thread Joiner: %d", error);
    }
}

void button1_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    LOG_INF("Button 1 (sw1 alias) pressed, starting Thread Joiner...");
    StartThreadJoiner();
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

    LOG_INF("Button 1 configured successfully");
}


int main()
{
    // Initialize Button 1 for Thread Joiner
    init_buttons();	

	LOG_INF("Waiting for Thread Joiner to complete...");
    while (!is_thread_joiner_complete)
    {
        k_sleep(K_SECONDS(1)); // Polling to check if Joiner has completed
    }

	LOG_INF("Starting Matter application...");
    CHIP_ERROR err = AppTask::Instance().StartApp();


	LOG_ERR("Exited with code %" CHIP_ERROR_FORMAT, err.Format());
	return err == CHIP_NO_ERROR ? EXIT_SUCCESS : EXIT_FAILURE;
}
