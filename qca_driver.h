#pragma once

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "qca_7k.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define QCASPI_MOSI      GPIO_NUM_11
#define QCASPI_MISO      GPIO_NUM_13
#define QCASPI_SCLK      GPIO_NUM_12
#define QCASPI_CS        GPIO_NUM_10
#define QCASPI_RST       GPIO_NUM_14
#define QCASPI_INT       GPIO_NUM_9
#define QCASPI_CLK_SPEED 12000000

extern qcaspi_t qca;

void qca_ll_init(void);
void qca_send(void *data, size_t len);
void qca_network_thread(void *data);
