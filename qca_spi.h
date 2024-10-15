/*====================================================================*
 *
 *   qca_ar7k.h
 *
 *   Qualcomm Atheros SPI register definition.
 *
 *   This module is designed to define the Qualcomm Atheros SPI register
 *   placeholders;
 *
 *--------------------------------------------------------------------*/

#ifndef QCA_SPI_HEADER
#define QCA_SPI_HEADER

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "sdkconfig.h"

/* FreeRTOS includes. */
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/* QCA7k includes */
#include "qca_framing.h"

#define GREENPHY_SYNC_HIGH_CHECK_TIME_MS 15000
#define GREENPHY_SYNC_LOW_CHECK_TIME_MS  1000

#define QCASPI_GOOD_SIGNATURE 0xAA55

/* sync related constants */
#define QCASPI_SYNC_UNKNOWN    0
#define QCASPI_SYNC_CPUON      1
#define QCASPI_SYNC_READY      2
#define QCASPI_SYNC_RESET      3
#define QCASPI_SYNC_SOFT_RESET 4
#define QCASPI_SYNC_HARD_RESET 5
#define QCASPI_SYNC_WAIT_RESET 6
#define QCASPI_SYNC_UPDATE     7

#define QCASPI_RESET_TIMEOUT 500

/* Task notification constants */
#define QCAGP_INT_FLAG (1 << 0)
#define QCAGP_RX_FLAG  (1 << 1) /* RX is passed as interrupt, too */
#define QCAGP_TX_FLAG  (1 << 2)

/* Max amount of bytes read in one run */
#define QCASPI_BURST_LEN (QCASPI_HW_BUF_LEN + 4)

#define QCASPI_TX_BUFFER_LEN 500
#define QCASPI_RX_BUFFER_LEN 1200

/* RX and TX stats */
typedef struct {
    uint32_t rx_errors;
    uint32_t rx_dropped;
    uint32_t rx_packets;
    uint32_t rx_bytes;
    uint32_t tx_errors;
    uint32_t tx_dropped;
    uint32_t tx_packets;
    uint32_t tx_bytes;
    uint32_t device_reset;
    uint32_t read_buf_err;
    uint32_t write_buf_err;
} qca_stats_t;

typedef struct {
    uint8_t *pucEthernetBuffer; /**< Pointer to the start of the Ethernet frame. */
    size_t xDataLength; /**< Starts by holding the total Ethernet frame length, then the UDP/TCP payload length. */
} NetworkBufferDescriptor_t;

typedef struct {
    spi_device_handle_t handle;
    TaskHandle_t task_handle;
    uint8_t sync;

    esp_netif_driver_base_t netif_base;

    QueueHandle_t txQueue;
    QueueHandle_t rxQueue;

    NetworkBufferDescriptor_t *rx_desc;

    uint8_t rx_buffer[QCAFRM_TOTAL_HEADER_LEN];
    uint16_t rx_buffer_size;
    uint16_t rx_buffer_pos;
    uint16_t rx_buffer_len;
    QcaFrmHdl lFrmHdl;

    qca_stats_t stats;
} qcaspi_t;

void qcaspi_spi_thread(void *data);

#endif
