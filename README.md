# QCA700x ESP IDF Driver

## Receive Packets 
```
void qca_network_thread(void *data)
{
    NetworkBufferDescriptor_t *rxDesc;
    
    while (1)
    {
        // Receive Packets from QCA Rx Queue
        if (xQueueReceive(qca.rxQueue, (void *)&rxDesc, portMAX_DELAY) == pdPASS)
        {
            ESP_LOG_BUFFER_HEX("qca", rxDesc->pucEthernetBuffer, rxDesc->xDataLength);
        }
    }
}
```

## Send Packet 
Homeplug AV Packet for Testing 
```
void send_op_attr_req(void)
{
    printf("OP_ATTR.REQ ->\n");
    op_attr_req_t msg = {0};
    memcpy(msg.dest, SLAC_BROADCAST_MAC_ADDRESS, SLAC_ETH_ALEN);
    memcpy(msg.src, dummy_mac, SLAC_ETH_ALEN);
    msg.ethertype     = ETH_P_HOMEPLUG_GREENPHY;
    msg.mmv           = AV_1_0;
    msg.mmtype        = MMTYPE_OP_ATTR | MMTYPE_MODE_REQ;
    msg.vendor_mme[0] = 0x00;
    msg.vendor_mme[1] = 0xb0;
    msg.vendor_mme[2] = 0x52;
    msg.cookie        = 0x12345;
    msg.report_type   = 0;

    qca_send(&msg, sizeof(msg));
}
```
