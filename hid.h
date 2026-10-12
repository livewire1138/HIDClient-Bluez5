#ifndef HIDCLIENT_HID_H
#define HIDCLIENT_HID_H
#include <linux/input.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define HID_MAX_DEVICES 64
#define HID_KEYBOARD 1
#define HID_MOUSE 2
#define HID_CONSUMER 3
#define HID_MODE_KEYBOARD 1
#define HID_MODE_MOUSE 2
#define HID_MODE_COMBO 3

typedef int (*hid_sink)(const uint8_t *, size_t, void *);
struct hid_device {
    bool keys[KEY_MAX + 1];
    int motion[4], hires[2], remainder[2];
    bool has_hires[2], recovering;
    bool keyboard_dirty, consumer_dirty, mouse_dirty;
};
struct hid_state {
    struct hid_device devices[HID_MAX_DEVICES];
    int mode;
    bool boot, suspended, swap_ctrl_meta, needs_sync;
    uint8_t leds;
};
void hid_init(struct hid_state *, int, bool);
uint16_t hid_usage(unsigned int);
int hid_keycode(const char *);
int hid_event(struct hid_state *, size_t, const struct input_event *, hid_sink, void *);
int hid_snapshot(struct hid_state *, size_t, const unsigned long *, size_t, hid_sink, void *);
int hid_sync(struct hid_state *, hid_sink, void *);
int hid_release(struct hid_state *, hid_sink, void *);
int hid_control(struct hid_state *, const uint8_t *, size_t, bool, hid_sink, void *);
uint8_t *hid_descriptor(int mode, size_t *length);
#endif
