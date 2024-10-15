/*====================================================================*
 *
 *   Copyright (c) 2011, 2012, Qualcomm Atheros Communications Inc.
 *
 *   Permission to use, copy, modify, and/or distribute this software
 *   for any purpose with or without fee is hereby granted, provided
 *   that the above copyright notice and this permission notice appear
 *   in all copies.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 *   WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 *   WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL
 *   THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
 *   CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 *   LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 *   NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 *   CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 *--------------------------------------------------------------------*/

/*====================================================================*
 *
 *   qca_7k.c	-
 *
 *   This module implements the Qualcomm Atheros SPI protocol for
 *   kernel-based SPI device.
 *
 *--------------------------------------------------------------------*/

/*====================================================================*
 *   system header files;
 *--------------------------------------------------------------------*/

/*====================================================================*
 *   custom header files;
 *--------------------------------------------------------------------*/

#include "qca_7k.h"
#include "byte_order.h"

uint16_t qcaspi_read_register(qcaspi_t *qca, uint16_t reg)
{
    uint16_t rx_data[1] = {0};

    spi_transaction_t t = {0};

    t.cmd       = (QCA7K_SPI_READ | QCA7K_SPI_INTERNAL | reg);
    t.length    = 0;
    t.tx_buffer = NULL;
    t.rxlength  = 16;
    t.rx_buffer = rx_data;

    esp_err_t err = spi_device_transmit(qca->handle, &t);
    ESP_ERROR_CHECK(err);

    // ESP_LOGI("qcaspi_read_reg", "CMD:%04X Value:%04X", t.cmd, rx_data[0]);

    return __be16_to_cpu(rx_data[0]);
}

void qcaspi_write_register(qcaspi_t *qca, uint16_t reg, uint16_t value)
{
    uint16_t tx_cmd = __cpu_to_be16(value);

    spi_transaction_t t = {0};

    t.cmd       = (QCA7K_SPI_WRITE | QCA7K_SPI_INTERNAL | reg);
    t.length    = 16;
    t.tx_buffer = &tx_cmd;
    t.rxlength  = 0;
    t.rx_buffer = NULL;

    esp_err_t err = spi_device_transmit(qca->handle, &t);
    ESP_ERROR_CHECK(err);

    // ESP_LOGI("qcaspi_write_reg", "CMD:%04X Value:%04X", t.cmd, tx_cmd);
}

int qcaspi_tx_cmd(qcaspi_t *qca, uint16_t cmd)
{
    spi_transaction_t t = {0};

    t.cmd       = cmd;
    t.length    = 0;
    t.tx_buffer = NULL;
    t.rxlength  = 0;
    t.rx_buffer = NULL;

    esp_err_t err = spi_device_transmit(qca->handle, &t);
    ESP_ERROR_CHECK(err);

    // ESP_LOG_BUFFER_HEX("qcaspi_cmd_tx", &cmd, 2);

    return 0;
}
