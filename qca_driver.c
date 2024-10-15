#include "qca_driver.h"

static const char *TAG = "qca-driver";

/* The struct to hold all QCA7k and SPI related information */
qcaspi_t qca = {0};

extern void qcaspi_spi_thread(void *data);
extern void qca_network_thread(void *data);
static void qca_reset(void);
static void qca_wait_sync(void);

void qca_send(void *data, size_t len)
{
    if (len < QCAFRM_ETHMINLEN)
        len = QCAFRM_ETHMINLEN;

    NetworkBufferDescriptor_t *txDesc = NULL;
    txDesc                            = malloc(sizeof(NetworkBufferDescriptor_t));
    txDesc->pucEthernetBuffer         = malloc(len + QCAFRM_FRAME_OVERHEAD);
    txDesc->xDataLength               = len;
    memcpy(txDesc->pucEthernetBuffer + QCAFRM_HEADER_LEN, data, len);

    if (qca.task_handle == NULL)
        ESP_LOGE(TAG, "Task Handle NULL");

    xQueueSend(qca.txQueue, &txDesc, 0);
    xTaskNotify(qca.task_handle, QCAGP_TX_FLAG, eSetBits);
}

static void IRAM_ATTR qca_irq_handler(void *arg)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xTaskNotifyFromISR(qca.task_handle, QCAGP_INT_FLAG, eSetBits, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void qca_ll_init(void)
{
    spi_bus_config_t qca_bus = {
        .miso_io_num     = QCASPI_MISO,
        .mosi_io_num     = QCASPI_MOSI,
        .sclk_io_num     = QCASPI_SCLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 2048,
    };

    spi_device_interface_config_t qca_dev = {
        .command_bits   = 16,
        .clock_speed_hz = QCASPI_CLK_SPEED,
        .mode           = 3,
        .spics_io_num   = QCASPI_CS,
        .queue_size     = 20,
        .flags          = SPI_DEVICE_HALFDUPLEX,
    };

    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &qca_bus, SPI_DMA_CH_AUTO));
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &qca_dev, &qca.handle));

    gpio_config_t io_conf = {0};
    io_conf.intr_type     = GPIO_INTR_POSEDGE;
    io_conf.pin_bit_mask  = 1ULL << (QCASPI_INT);
    io_conf.mode          = GPIO_MODE_INPUT;
    io_conf.pull_up_en    = 0;
    io_conf.pull_down_en  = 0;

    gpio_config(&io_conf);

    gpio_install_isr_service(0);

    qca.sync    = QCASPI_SYNC_UNKNOWN;
    qca.txQueue = xQueueCreate(25, sizeof(NetworkBufferDescriptor_t *));
    qca.rxQueue = xQueueCreate(25, sizeof(NetworkBufferDescriptor_t *));
    QcaFrmFsmInit(&qca.lFrmHdl);

    /* QCA7000 reset pin setup */
    gpio_reset_pin(QCASPI_RST);
    gpio_set_direction(QCASPI_RST, GPIO_MODE_OUTPUT);
    qca_reset();

    ESP_LOGI(TAG, "QCA Driver Init Success.");
    xTaskCreatePinnedToCore(qcaspi_spi_thread, "qca_spi", 4096, &qca, tskIDLE_PRIORITY + 10, &qca.task_handle,
                            APP_CPU_NUM);
    xTaskCreatePinnedToCore(qca_network_thread, "qca_network", 4096, &qca, tskIDLE_PRIORITY + 8, NULL, APP_CPU_NUM);

    gpio_isr_handler_add(QCASPI_INT, qca_irq_handler, NULL);
    /* Wait for sync. */
    qca_wait_sync();
    ESP_LOGI(TAG, "QCA Driver Sync.");
}

void qca_reset(void)
{
    gpio_set_level(QCASPI_RST, 0);
    vTaskDelay(100);
    gpio_set_level(QCASPI_RST, 1);
}

void qca_wait_sync(void)
{
    while (qca.sync != QCASPI_SYNC_READY) { vTaskDelay(pdMS_TO_TICKS(500)); }
}
