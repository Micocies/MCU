#ifndef FAKE_USB_H
#define FAKE_USB_H

#include <stdint.h>

#include "usb_stream.h"

#ifdef __cplusplus
extern "C" {
#endif

#define USBD_OK 0U
#define USBD_BUSY 1U
#define USBD_FAIL 2U

void fake_usb_reset(void);
void fake_usb_set_transmit_status(uint8_t status);
void fake_usb_complete_tx(void);
uint32_t fake_usb_get_transmit_count(void);
uint16_t fake_usb_get_last_len(void);
const uint8_t *fake_usb_get_last_data(void);
const sample_packet_t *fake_usb_get_last_packets(void);
const frame_packet_t *fake_usb_get_last_frame(void);

uint8_t CDC_Transmit_FS(uint8_t *Buf, uint16_t Len);

#ifdef __cplusplus
}
#endif

#endif /* FAKE_USB_H */
