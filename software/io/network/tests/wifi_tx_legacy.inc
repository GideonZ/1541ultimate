// wifi_tx_packet as it was before the no-wait change (software/io/wifi/wifi.cc before this change,
// with the counters): the wait for a buffer is up to 1000 ticks (5 s), under the
// core lock of the caller. The red model of wifi_tx_nowait_test.cpp (-DWIFI_TX_LEGACY).
err_t wifi_tx_packet(void *driver, void *buffer, int length)
{
    if (wifi.getState() != eWifi_Connected) {
        return ERR_CONN;
    }
    
    // BUFARGS_NOREPLY(send_eth, CMD_SEND_PACKET, ERR_MEM) (the module does not answer frames), with
    // the wait for the buffer timed and counted: net_counters.wifi_tx_wait
    command_buf_t *buf;
    TickType_t waited = xTaskGetTickCount();
    BaseType_t gotbuf = esp32.uart->GetBuffer(&buf, 1000);
    net_counters_wifi_tx_wait(xTaskGetTickCount() - waited, gotbuf != pdFALSE);
    if (gotbuf == pdFALSE) { printf(no_wifi_buf); return ERR_MEM; }
    rpc_send_eth_req *args = (rpc_send_eth_req *)buf->data;
    args->hdr.command = CMD_SEND_PACKET;
    args->hdr.sequence = 0;
    args->hdr.thread = 0xFF;
    buf->size = sizeof(rpc_send_eth_req);

    uint32_t chunkSize = length;
    if (chunkSize > CMD_BUF_PAYLOAD) {
        chunkSize = CMD_BUF_PAYLOAD;
    }
    args->length = chunkSize;
    memcpy(&args->data, buffer, chunkSize);
    buf->size += (chunkSize - 4);

    esp32.uart->TransmitPacket(buf);
    //TRANSMIT(espcmd);
    //RETURN_ESP;
    return ERR_OK;
}
