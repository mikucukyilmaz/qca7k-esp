/*====================================================================*
 *
 *   qca_spi.c
 *
 *   This module implements the Qualcomm Atheros SPI protocol for
 *   kernel-based SPI device; it is essentially an Ethernet-to-SPI
 *   serial converter;
 *
 *--------------------------------------------------------------------*/

/*====================================================================*
 *   system header files;
 *--------------------------------------------------------------------*/
/* Standard includes. */
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/* QCA7k includes */
#include "byte_order.h"
#include "qca_7k.h"
#include "qca_framing.h"
#include "qca_spi.h"
#include <string.h>

static uint16_t available = 0;

static const char *TAG = "qca_spi";

static void start_spi_intr_handling(qcaspi_t *qca, uint16_t *intr_cause)
{
    *intr_cause = 0;
    qcaspi_write_register(qca, SPI_REG_INTR_ENABLE, 0);
    *intr_cause = qcaspi_read_register(qca, SPI_REG_INTR_CAUSE);
}

static void end_spi_intr_handling(qcaspi_t *qca, uint16_t intr_cause)
{
    uint16_t intr_enable = (SPI_INT_CPU_ON | SPI_INT_PKT_AVLBL | SPI_INT_RDBUF_ERR | SPI_INT_WRBUF_ERR);
    qcaspi_write_register(qca, SPI_REG_INTR_CAUSE, intr_cause);
    qcaspi_write_register(qca, SPI_REG_INTR_ENABLE, intr_enable);
}

uint16_t qcaspi_write_burst(qcaspi_t *qca, uint8_t *src, uint16_t len)
{
    QcaFrmCreateHeader(src, len);
    QcaFrmCreateFooter(src + QCAFRM_HEADER_LEN + len);

    spi_transaction_t t = {0};

    t.cmd         = (QCA7K_SPI_WRITE | QCA7K_SPI_EXTERNAL);
    t.length      = (len + QCAFRM_FRAME_OVERHEAD) * 8;
    t.tx_buffer   = src;
    t.rx_buffer   = NULL;
    t.rxlength    = 0;
    esp_err_t err = spi_device_transmit(qca->handle, &t);
    ESP_ERROR_CHECK(err);

    return len;
}

uint16_t qcaspi_read_blocking(qcaspi_t *qca, uint8_t *dst, uint16_t len)
{
    qcaspi_write_register(qca, SPI_REG_BFR_SIZE, len);

    spi_transaction_t t = {0};

    t.cmd         = (QCA7K_SPI_READ | QCA7K_SPI_EXTERNAL);
    t.length      = 0;
    t.tx_buffer   = NULL;
    t.rx_buffer   = dst;
    t.rxlength    = (len)*8;
    esp_err_t err = spi_device_transmit(qca->handle, &t);

    ESP_ERROR_CHECK(err);

    available -= len;

    return len;
}

uint16_t qcaspi_read_burst(qcaspi_t *qca, uint8_t *dst, uint16_t len)
{
    qcaspi_write_register(qca, SPI_REG_BFR_SIZE, len);

    spi_transaction_t t = {0};

    t.cmd       = (QCA7K_SPI_READ | QCA7K_SPI_EXTERNAL);
    t.length    = 0;
    t.tx_buffer = NULL;
    t.rx_buffer = dst;
    t.rxlength  = (len)*8;

    esp_err_t err = spi_device_transmit(qca->handle, &t);
    ESP_ERROR_CHECK(err);

    available -= len;

    return len;
}

uint16_t qcaspi_tx_frame(qcaspi_t *qca, NetworkBufferDescriptor_t *txBuffer)
{
    uint16_t writtenBytes = 0;

    uint8_t *pucData = txBuffer->pucEthernetBuffer;
    uint16_t len     = txBuffer->xDataLength;
    uint16_t pad_len;
    if (len < QCAFRM_ETHMINLEN)
    {
        ESP_LOGE(TAG, "QCAFRM_ETHMINLEN");
        pad_len = QCAFRM_ETHMINLEN - len;
        memset(pucData + len, 0, pad_len);
        len += pad_len;
    }

    qcaspi_write_register(qca, SPI_REG_BFR_SIZE, len + QCAFRM_FRAME_OVERHEAD);
    /* send ethernet packet via DMA to SPI */
    writtenBytes = qcaspi_write_burst(qca, pucData, len);
    return writtenBytes;
}

int qcaspi_transmit(qcaspi_t *qca)
{
    NetworkBufferDescriptor_t *txBuffer;

    /* read the available space in bytes from QCA7k */
    uint16_t wrbuf_available = qcaspi_read_register(qca, SPI_REG_WRBUF_SPC_AVA);

    if (xQueuePeek(qca->txQueue, &txBuffer, 0) == pdPASS)
    {
        /* check whether there is enough space in the QCA7k buffer to hold
         * the next packet */
        if (wrbuf_available < (txBuffer->xDataLength + QCAFRM_FRAME_OVERHEAD))
        {
            ESP_LOGE(TAG, "Not Enough Space");
            return -1;
        }

        /* receive and process the next packet */
        if (xQueueReceive(qca->txQueue, &txBuffer, 0) == pdPASS)
        {
            uint16_t writtenBytes = qcaspi_tx_frame(qca, txBuffer);
            free(txBuffer->pucEthernetBuffer);
            free(txBuffer);
            wrbuf_available -= (writtenBytes + QCAFRM_FRAME_OVERHEAD);
            qca->stats.tx_packets++;
            qca->stats.tx_bytes += writtenBytes;
        }
    }

    return 0;
}

void qcaspi_process_rx_buffer(qcaspi_t *qca)
{
    int32_t ret;
    qca->rx_buffer_pos = 0;
    while (qca->rx_buffer_pos < qca->rx_buffer_len)
    {
        if (qca->rx_desc == NULL)
        {
            ESP_LOGE(TAG, "Null rx Desc");
        }

        ret = QcaFrmFsmDecode(&qca->lFrmHdl, qca->rx_buffer[qca->rx_buffer_pos], qca->rx_desc->pucEthernetBuffer);
        switch (ret)
        {
        case QCAFRM_GATHER:
        case QCAFRM_NOHEAD:
            break;
        case QCAFRM_NOTAIL:
            qca->stats.rx_errors++;
            qca->stats.rx_dropped++;
            break;
        case QCAFRM_INVLEN:
            qca->stats.rx_errors++;
            qca->stats.rx_dropped++;
            break;
        default:
            qca->rx_desc->xDataLength = ret;
            break;
        }
        qca->rx_buffer_pos++;
    }
}

int qcaspi_receive(qcaspi_t *qca)
{
    available = qcaspi_read_register(qca, SPI_REG_RDBUF_BYTE_AVA);

    if (qca->rx_desc == NULL)
    {
        qca->rx_desc                    = malloc(sizeof(NetworkBufferDescriptor_t));
        qca->rx_desc->pucEthernetBuffer = malloc(QCAFRM_ETHMAXLEN);
        qca->rx_desc->xDataLength       = 0;
    }

    // printf("Available:%d\n", available);
    while (available >= QcaFrmBytesRequired(&qca->lFrmHdl))
    {
        switch (QcaFrmGetAction(&qca->lFrmHdl))
        {
        case QCAFRM_FIND_HEADER:
            /* Read data of the size of one header. */
            qca->rx_buffer_len = qcaspi_read_blocking(qca, qca->rx_buffer, QcaFrmBytesRequired(&qca->lFrmHdl));
            qcaspi_process_rx_buffer(qca);
            break;

        case QCAFRM_COPY_FRAME:
            /* Start DMA read to copy the frame into the ethernet buffer. */
            qca->lFrmHdl.state -= qcaspi_read_burst(qca, qca->rx_desc->pucEthernetBuffer + qca->lFrmHdl.offset,
                                                    (qca->lFrmHdl.len - qca->lFrmHdl.offset));
            break;

        case QCAFRM_CHECK_FOOTER:
            /* Read footer. */
            qca->rx_buffer_len = qcaspi_read_blocking(qca, qca->rx_buffer, QcaFrmBytesRequired(&qca->lFrmHdl));
            qcaspi_process_rx_buffer(qca);
            break;

        case QCAFRM_FRAME_COMPLETE:
            qca->stats.rx_packets++;
            qca->stats.rx_bytes += qca->rx_desc->xDataLength;

            if (xQueueSend(qca->rxQueue, &qca->rx_desc, 0) != pdPASS)
            {
                ESP_LOGE("qca_spi", "Rx Frame[%d] Send to Queue Failed", qca->rx_desc->xDataLength);
                qca->stats.rx_dropped++;
            }

            qca->rx_desc = NULL;

            if (qca->rx_desc == NULL)
            {
                qca->rx_desc                    = malloc(sizeof(NetworkBufferDescriptor_t));
                qca->rx_desc->pucEthernetBuffer = malloc(QCAFRM_ETHMAXLEN);
                qca->rx_desc->xDataLength       = 0;
            }

            /* Reset the frame handle, so a new header will be read */
            qca->lFrmHdl.state = QCAFRM_WAIT_AA1;
            break;
        }
    }

    if (available >= QcaFrmBytesRequired(&qca->lFrmHdl))
    {
        ESP_LOGI(TAG, " Could not receive all frames. %d", available);
        /* Could not receive all frames. */
        return -1;
    }
    return 0;
}

void qcaspi_flush_txq(qcaspi_t *qca)
{
    NetworkBufferDescriptor_t *txBuffer = NULL;
    while (xQueueReceive(qca->txQueue, &txBuffer, 0)) { ESP_LOGI("qcaspi", "flush_txq"); }
}

void qcaspi_qca7k_sync(qcaspi_t *qca, int event)
{
    uint32_t signature;
    uint32_t spi_config;
    uint32_t wrbuf_space;
    static uint32_t reset_count;

    if (event != QCASPI_SYNC_UPDATE)
        qca->sync = event;

    while (1)
    {
        switch (qca->sync)
        {
        case QCASPI_SYNC_CPUON:
            ESP_LOGI(TAG, "QCASPI_SYNC_RESET");
            /* Read signature twice, if not valid go back to unknown state. */
            signature = qcaspi_read_register(qca, SPI_REG_SIGNATURE);
            signature = qcaspi_read_register(qca, SPI_REG_SIGNATURE);
            if (signature != QCASPI_GOOD_SIGNATURE)
            {
                qca->sync = QCASPI_SYNC_HARD_RESET;
            }
            else
            {
                /* ensure that the WRBUF is empty */
                wrbuf_space = qcaspi_read_register(qca, SPI_REG_WRBUF_SPC_AVA);
                if (wrbuf_space != QCASPI_HW_BUF_LEN)
                {
                    qca->sync = QCASPI_SYNC_SOFT_RESET;
                }
                else
                {
                    qca->sync = QCASPI_SYNC_READY;
                    return;
                }
            }
            break;

        case QCASPI_SYNC_UNKNOWN:
        case QCASPI_SYNC_RESET:
            ESP_LOGI(TAG, "QCASPI_SYNC_RESET");
            signature = qcaspi_read_register(qca, SPI_REG_SIGNATURE);
            if (signature == QCASPI_GOOD_SIGNATURE)
            {
                /* signature correct, do a soft reset*/
                qca->sync = QCASPI_SYNC_SOFT_RESET;
            }
            else
            {
                /* could not read signature, do a hard reset */
                qca->sync = QCASPI_SYNC_HARD_RESET;
            }
            break;

        case QCASPI_SYNC_SOFT_RESET:
            spi_config = qcaspi_read_register(qca, SPI_REG_SPI_CONFIG);
            qcaspi_write_register(qca, SPI_REG_SPI_CONFIG, spi_config | QCASPI_SLAVE_RESET_BIT);

            qca->sync   = QCASPI_SYNC_WAIT_RESET;
            reset_count = 0;
            return;

        case QCASPI_SYNC_HARD_RESET:

            ESP_LOGI(TAG, "QCASPI_SYNC_HARD_RESET");
            /* reset is normally active low, so reset ... */
            // Chip_GPIO_SetPinOutLow(LPC_GPIO, GREENPHY_RESET_GPIO_PORT, GREENPHY_RESET_GPIO_PIN);
            // gpio_set_level(QCASPI_RST, 0);
            /*  ... for 100 ms ... */
            vTaskDelay(100);
            /* ... and release QCA7k from reset */
            // gpio_set_level(QCASPI_RST, 1);
            // Chip_GPIO_SetPinOutHigh(LPC_GPIO, GREENPHY_RESET_GPIO_PORT, GREENPHY_RESET_GPIO_PIN);

            qca->sync   = QCASPI_SYNC_WAIT_RESET;
            reset_count = 0;
            return;

        case QCASPI_SYNC_WAIT_RESET:
            /* still awaiting reset, increase counter */
            ++reset_count;

            if (reset_count >= QCASPI_RESET_TIMEOUT)
            {
                /* reset did not seem to take place, try again */
                qca->sync = QCASPI_SYNC_RESET;
            }
            break;

        case QCASPI_SYNC_READY:
        default:
            signature = qcaspi_read_register(qca, SPI_REG_SIGNATURE);
            /* if signature is correct, sync is still ready*/
            if (signature == QCASPI_GOOD_SIGNATURE)
            {
                return;
            }
            /* could not read signature, do a hard reset */
            qca->sync = QCASPI_SYNC_HARD_RESET;
            break;
        }
    }
}

void qcaspi_spi_thread(void *data)
{
    ESP_LOGI("qca_spi", "Thread Started.");

    qcaspi_t *qca = (qcaspi_t *)data;

    uint16_t intr_cause;
    uint32_t ulNotificationValue;
    TickType_t xSyncRemTime = pdMS_TO_TICKS(GREENPHY_SYNC_LOW_CHECK_TIME_MS);

    for (;;)
    {
        /* Take notification
         * 0 timeout
         * 1 interrupt (including receive)
         * 2 receive (not used, handled by interrupt)
         * 4 transmit
         * */
        if ((qca->sync == QCASPI_SYNC_READY))
        {
            xSyncRemTime = pdMS_TO_TICKS(GREENPHY_SYNC_HIGH_CHECK_TIME_MS);
        }
        else
        {
            xSyncRemTime = pdMS_TO_TICKS(GREENPHY_SYNC_LOW_CHECK_TIME_MS);
        }

        ulNotificationValue = ulTaskNotifyTake(pdTRUE, xSyncRemTime);

        if (!ulNotificationValue)
        {
            /* We got a timeout, check if we need to restart sync. */
            qcaspi_qca7k_sync(qca, QCASPI_SYNC_UPDATE);
            /* Not synced. Awaiting reset, or sync unknown. */
            if (qca->sync != QCASPI_SYNC_READY)
            {
                ESP_LOGI(TAG, "Sync Update Failed.");
                qcaspi_flush_txq(qca);
                continue;
            }
        }

        if (ulNotificationValue & QCAGP_INT_FLAG)
        {
            // gpio_intr_enable(QCASPI_INT);

            /* We got an interrupt. */
            start_spi_intr_handling(qca, &intr_cause);
            // ESP_LOGI(TAG, "We got IRQ. %04X", intr_cause);

            if (intr_cause & SPI_INT_CPU_ON)
            {
                ESP_LOGI(TAG, "CPU On.");

                qcaspi_qca7k_sync(qca, QCASPI_SYNC_CPUON);
                qca->stats.device_reset++;

                /* If not synced, wait reset. */
                if (qca->sync != QCASPI_SYNC_READY)
                    continue;
            }

            if (intr_cause & (SPI_INT_RDBUF_ERR))
            {
                ESP_LOGI(TAG, "RDBUF_ERR.");
                qca->stats.read_buf_err++;
                qcaspi_qca7k_sync(qca, QCASPI_SYNC_RESET);
                continue;
            }

            if (intr_cause & (SPI_INT_WRBUF_ERR))
            {
                ESP_LOGI(TAG, "WRBUF_ERR.");
                qca->stats.write_buf_err++;
                qcaspi_qca7k_sync(qca, QCASPI_SYNC_RESET);
                continue;
            }

            if (qca->sync == QCASPI_SYNC_READY)
            {
                if (intr_cause & SPI_INT_PKT_AVLBL)
                {
                    if (qcaspi_receive(qca) == 0)
                    {
                        /* All packets received. */
                    }
                }
            }

            end_spi_intr_handling(qca, intr_cause);
        }

        if (qca->sync == QCASPI_SYNC_READY)
        {
            if (uxQueueMessagesWaiting(qca->txQueue))
            {
                if (qcaspi_transmit(qca) != 0)
                {
                    //
                }
            }
        }
    }
}
