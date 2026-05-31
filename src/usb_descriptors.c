/* * USB Descriptors for Composite Device (CDC + HID Keyboard) with NKRO
 * Built for Raspberry Pi Pico / TinyUSB
 */
 
#include "tusb.h"
#include <string.h>
 
#define NKRO_KEYCODE_BYTES  16
#define NKRO_REPORT_SIZE    (1 + 1 + NKRO_KEYCODE_BYTES)   // 18
 
// --------------------------------------------------------------------+
// Device Descriptor
// --------------------------------------------------------------------+
tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
 
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
 
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
 
    .idVendor           = 0x2E8A,
    .idProduct          = 0x000A,
    .bcdDevice          = 0x0100,
 
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
 
    .bNumConfigurations = 0x01
};
 
// Invoked when received GET DEVICE DESCRIPTOR
uint8_t const * tud_descriptor_device_cb(void) {
    return (uint8_t const *) &desc_device;
}
 
// --------------------------------------------------------------------+
// HID Report Descriptor (NKRO)
// --------------------------------------------------------------------+
//
// Report structure (18 bytes total):
//   [0]     modifier   – 8 x 1-bit fields for Ctrl/Shift/Alt/GUI L+R
//   [1]     reserved   – constant padding byte
//   [2..17] key bitmap – 128 bits, one bit per keycode 0x00–0x7F
//
uint8_t const desc_hid_report[] = {
    HID_USAGE_PAGE ( HID_USAGE_PAGE_DESKTOP     ),
    HID_USAGE      ( HID_USAGE_DESKTOP_KEYBOARD ),
    HID_COLLECTION ( HID_COLLECTION_APPLICATION ),
 
        // --- Modifier byte: 8 x 1-bit variable fields ---
        HID_USAGE_PAGE  ( HID_USAGE_PAGE_KEYBOARD ),
        HID_USAGE_MIN   ( 224                     ),   // Left Control  (0xE0)
        HID_USAGE_MAX   ( 231                     ),   // Right GUI     (0xE7)
        HID_LOGICAL_MIN ( 0                       ),
        HID_LOGICAL_MAX ( 1                       ),
        HID_REPORT_COUNT( 8                       ),
        HID_REPORT_SIZE ( 1                       ),
        HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE ),
 
        // --- Reserved padding byte (constant) ---
        HID_REPORT_COUNT( 1  ),
        HID_REPORT_SIZE ( 8  ),
        HID_INPUT       ( HID_CONSTANT ),
 
        // --- 128-key bitmap (keycodes 0x00 – 0x7F) ---
        HID_USAGE_PAGE  ( HID_USAGE_PAGE_KEYBOARD ),
        HID_USAGE_MIN   ( 0                       ),
        HID_USAGE_MAX   ( 127                     ),
        HID_LOGICAL_MIN ( 0                       ),
        HID_LOGICAL_MAX ( 1                       ),
        0x96, 0x80, 0x00,                              // FIXED: Forces 128 as a positive 2-byte integer
        HID_REPORT_SIZE ( 1                       ),
        HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE ), 
    HID_COLLECTION_END
};
 
// Invoked when received GET HID REPORT DESCRIPTOR
uint8_t const * tud_hid_descriptor_report_cb(uint8_t instance) {
    (void) instance;
    return desc_hid_report;
}
 
// --------------------------------------------------------------------+
// Configuration Descriptor
// --------------------------------------------------------------------+
enum {
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_HID,
    ITF_NUM_TOTAL
};
 
#define CONFIG_TOTAL_LEN    (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_HID_DESC_LEN)
 
// Endpoint configurations
#define EPNUM_CDC_NOTIF   0x81
#define EPNUM_CDC_OUT     0x02
#define EPNUM_CDC_IN      0x82
#define EPNUM_HID         0x83
 
uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
 
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8,
                       EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
 
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 5, HID_ITF_PROTOCOL_NONE,
                   sizeof(desc_hid_report), EPNUM_HID,
                   CFG_TUD_HID_EP_BUFSIZE, 10)
};
 
uint8_t const * tud_descriptor_configuration_cb(uint8_t index) {
    (void) index;
    return desc_configuration;
}
 
// --------------------------------------------------------------------+
// String Descriptors
// --------------------------------------------------------------------+
char const* string_desc_arr [] = {
    (const char[]) { 0x09, 0x04 },
    "@BMWAdam",
    "BMWAdam's keyboard",
    "0000001",
    "Pico CDC",
    "V1"
};
 
static uint16_t _desc_str[32];
 
uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void) langid;
 
    uint8_t chr_count;
 
    if (index == 0) {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else {
        if (!(index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))) return NULL;
 
        const char* str = string_desc_arr[index];
 
        chr_count = (uint8_t) strlen(str);
        if (chr_count > 31) chr_count = 31;
 
        for (uint8_t i = 0; i < chr_count; i++) {
            _desc_str[1 + i] = str[i];
        }
    }
 
    _desc_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
 
    return _desc_str;
}
 
