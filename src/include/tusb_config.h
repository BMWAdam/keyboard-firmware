#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
 extern "C" {
#endif

// --------------------------------------------------------------------+
// Board Specific Configuration
// --------------------------------------------------------------------+
#define CFG_TUSB_OS               OPT_OS_FREERTOS

// --------------------------------------------------------------------+
// Device Configuration
// --------------------------------------------------------------------+

// ADD THIS LINE: Enable Device stack
#define CFG_TUD_ENABLED           1

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE    64
#endif

// ------------- Class Enabled -------------
#define CFG_TUD_CDC               1
#define CFG_TUD_HID               1

// Disable unused classes
#define CFG_TUD_MSC               0
#define CFG_TUD_MIDI              0
#define CFG_TUD_VENDOR            0

// --------------------------------------------------------------------+
// Class Specific Details
// --------------------------------------------------------------------+

// --- CDC ---
#define CFG_TUD_CDC_RX_BUFSIZE    64
#define CFG_TUD_CDC_TX_BUFSIZE    64
#define CFG_TUD_CDC_EP_BUFSIZE    64

// --- HID ---
#define CFG_TUD_HID_EP_BUFSIZE    32

#ifdef __cplusplus
 }
#endif

#endif /* _TUSB_CONFIG_H_ */
