/* Standard HID report encoding; GPL-2.0-only. */
#include "hid.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>

static const uint8_t keyboard_descriptor[] = {
    0x05,0x01,0x09,0x06,0xa1,0x01,0x85,HID_KEYBOARD,
    0x05,0x07,0x19,0xe0,0x29,0xe7,0x15,0x00,0x25,0x01,
    0x75,0x01,0x95,0x08,0x81,0x02,
    0x75,0x08,0x95,0x01,0x81,0x01, /* reserved byte */
    0x05,0x08,0x19,0x01,0x29,0x05,0x75,0x01,0x95,0x05,0x91,0x02,
    0x75,0x03,0x95,0x01,0x91,0x01, /* five keyboard LEDs + padding */
    0x05,0x07,0x19,0x00,0x29,0xdf,0x15,0x00,0x26,0xdf,0x00,
    0x75,0x08,0x95,0x06,0x81,0x00,0xc0
};
static const uint8_t mouse_descriptor[] = {
    0x05,0x01,0x09,0x02,0xa1,0x01,0x85,HID_MOUSE,0x09,0x01,0xa1,0x00,
    0x05,0x09,0x19,0x01,0x29,0x05,0x15,0x00,0x25,0x01,
    0x75,0x01,0x95,0x05,0x81,0x02,0x75,0x03,0x95,0x01,0x81,0x01,
    0x05,0x01,0x09,0x30,0x09,0x31,0x09,0x38,
    0x15,0x81,0x25,0x7f,0x75,0x08,0x95,0x03,0x81,0x06,
    0x05,0x0c,0x0a,0x38,0x02,0x95,0x01,0x81,0x06,0xc0,0xc0
};
/* Fixed consumer bitfield: playback, volume, navigation. */
static const uint16_t consumer_keys[] = {
    KEY_PLAYPAUSE,KEY_STOPCD,KEY_PREVIOUSSONG,KEY_NEXTSONG,
    KEY_MUTE,KEY_VOLUMEUP,KEY_VOLUMEDOWN,KEY_EJECTCD,
    KEY_PLAY,KEY_PAUSECD,KEY_REWIND,KEY_FASTFORWARD,
    KEY_HOMEPAGE,KEY_BACK,KEY_FORWARD,KEY_SEARCH
};
static const uint8_t consumer_descriptor[] = {
    0x05,0x0c,0x09,0x01,0xa1,0x01,0x85,HID_CONSUMER,
    0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x10,
    0x09,0xcd,0x09,0xb7,0x09,0xb6,0x09,0xb5,
    0x09,0xe2,0x09,0xe9,0x09,0xea,0x09,0xb8,
    0x09,0xb0,0x09,0xb1,0x09,0xb4,0x09,0xb3,
    0x0a,0x23,0x02,0x0a,0x24,0x02,0x0a,0x25,0x02,0x0a,0x21,0x02,
    0x81,0x02,0xc0
};

uint8_t *hid_descriptor(int mode, size_t *length)
{
    size_t n = 0;
    if (mode & HID_MODE_KEYBOARD) n += sizeof(keyboard_descriptor) + sizeof(consumer_descriptor);
    if (mode & HID_MODE_MOUSE) n += sizeof(mouse_descriptor);
    uint8_t *data = malloc(n ? n : 1);
    if (!data) return NULL;
    size_t offset = 0;
#define APPEND(part) do { memcpy(data + offset, part, sizeof(part)); offset += sizeof(part); } while (0)
    if (mode & HID_MODE_KEYBOARD) { APPEND(keyboard_descriptor); APPEND(consumer_descriptor); }
    if (mode & HID_MODE_MOUSE) APPEND(mouse_descriptor);
#undef APPEND
    *length = n;
    return data;
}

/* Explicit Linux keycode -> USB HID usage mapping (layout independent). */
static const uint8_t usages[KEY_MAX + 1] = {
    [KEY_A]=4,[KEY_B]=5,[KEY_C]=6,[KEY_D]=7,[KEY_E]=8,[KEY_F]=9,
    [KEY_G]=10,[KEY_H]=11,[KEY_I]=12,[KEY_J]=13,[KEY_K]=14,[KEY_L]=15,
    [KEY_M]=16,[KEY_N]=17,[KEY_O]=18,[KEY_P]=19,[KEY_Q]=20,[KEY_R]=21,
    [KEY_S]=22,[KEY_T]=23,[KEY_U]=24,[KEY_V]=25,[KEY_W]=26,[KEY_X]=27,[KEY_Y]=28,[KEY_Z]=29,
    [KEY_1]=30,[KEY_2]=31,[KEY_3]=32,[KEY_4]=33,[KEY_5]=34,[KEY_6]=35,[KEY_7]=36,[KEY_8]=37,[KEY_9]=38,[KEY_0]=39,
    [KEY_ENTER]=40,[KEY_ESC]=41,[KEY_BACKSPACE]=42,[KEY_TAB]=43,[KEY_SPACE]=44,
    [KEY_MINUS]=45,[KEY_EQUAL]=46,[KEY_LEFTBRACE]=47,[KEY_RIGHTBRACE]=48,[KEY_BACKSLASH]=49,
    [KEY_SEMICOLON]=51,[KEY_APOSTROPHE]=52,[KEY_GRAVE]=53,[KEY_COMMA]=54,[KEY_DOT]=55,[KEY_SLASH]=56,[KEY_CAPSLOCK]=57,
    [KEY_F1]=58,[KEY_F2]=59,[KEY_F3]=60,[KEY_F4]=61,[KEY_F5]=62,[KEY_F6]=63,
    [KEY_F7]=64,[KEY_F8]=65,[KEY_F9]=66,[KEY_F10]=67,[KEY_F11]=68,[KEY_F12]=69,
    [KEY_SYSRQ]=70,[KEY_SCROLLLOCK]=71,[KEY_PAUSE]=72,[KEY_INSERT]=73,[KEY_HOME]=74,[KEY_PAGEUP]=75,
    [KEY_DELETE]=76,[KEY_END]=77,[KEY_PAGEDOWN]=78,[KEY_RIGHT]=79,[KEY_LEFT]=80,[KEY_DOWN]=81,[KEY_UP]=82,
    [KEY_NUMLOCK]=83,[KEY_KPSLASH]=84,[KEY_KPASTERISK]=85,[KEY_KPMINUS]=86,[KEY_KPPLUS]=87,[KEY_KPENTER]=88,
    [KEY_KP1]=89,[KEY_KP2]=90,[KEY_KP3]=91,[KEY_KP4]=92,[KEY_KP5]=93,[KEY_KP6]=94,[KEY_KP7]=95,[KEY_KP8]=96,[KEY_KP9]=97,[KEY_KP0]=98,[KEY_KPDOT]=99,
    [KEY_102ND]=100,[KEY_COMPOSE]=101,[KEY_POWER]=102,[KEY_KPEQUAL]=103,
    [KEY_F13]=104,[KEY_F14]=105,[KEY_F15]=106,[KEY_F16]=107,[KEY_F17]=108,[KEY_F18]=109,
    [KEY_F19]=110,[KEY_F20]=111,[KEY_F21]=112,[KEY_F22]=113,[KEY_F23]=114,[KEY_F24]=115,
    [KEY_KPCOMMA]=133,[KEY_RO]=135,[KEY_KATAKANAHIRAGANA]=136,[KEY_YEN]=137,
    [KEY_HENKAN]=138,[KEY_MUHENKAN]=139,[KEY_KPJPCOMMA]=140,
    [KEY_HANGEUL]=144,[KEY_HANJA]=145,[KEY_KATAKANA]=146,[KEY_HIRAGANA]=147,[KEY_ZENKAKUHANKAKU]=148,
    [KEY_LEFTCTRL]=224,[KEY_LEFTSHIFT]=225,[KEY_LEFTALT]=226,[KEY_LEFTMETA]=227,
    [KEY_RIGHTCTRL]=228,[KEY_RIGHTSHIFT]=229,[KEY_RIGHTALT]=230,[KEY_RIGHTMETA]=231
};
uint16_t hid_usage(unsigned int code) { return code <= KEY_MAX ? usages[code] : 0; }
int hid_keycode(const char *name)
{
    if (!strcasecmp(name,"none")) return -1;
    if (!strcasecmp(name,"pause")) return KEY_PAUSE;
    if (!strcasecmp(name,"scrolllock")) return KEY_SCROLLLOCK;
    if (!strcasecmp(name,"esc")) return KEY_ESC;
    for (int i = 1; i <= 24; ++i) {
        char key[16]; snprintf(key,sizeof(key),"F%d",i);
        if (!strcasecmp(name,key)) {
            for (int code = 0; code <= KEY_MAX; ++code)
                if (usages[code] == (i <= 12 ? 57+i : 91+i)) return code;
        }
    }
    return -2;
}
void hid_init(struct hid_state *state, int mode, bool swap)
{
    memset(state,0,sizeof(*state)); state->mode=mode; state->swap_ctrl_meta=swap;
}
static bool pressed(const struct hid_state *s, unsigned code)
{
    for (size_t i=0;i<HID_MAX_DEVICES;++i) if (s->devices[i].keys[code]) return true;
    return false;
}
static size_t report(struct hid_state *s, int id, uint8_t *out)
{
    memset(out,0,16); out[0]=0xa1; out[1]=(uint8_t)id;
    if (id == HID_KEYBOARD && (s->mode & HID_MODE_KEYBOARD)) {
        bool seen[256]={0}; size_t keys=0;
        for (unsigned code=0;code<=KEY_MAX;++code) {
            unsigned u=hid_usage(code);
            if (!u || !pressed(s,code)) continue;
            if (s->swap_ctrl_meta) {
                if (u==224 || u==228) u+=3;
                else if (u==227 || u==231) u-=3;
            }
            if (u>=224) out[2] |= (uint8_t)(1u << (u-224));
            else seen[u]=true;
        }
        for (unsigned u=4;u<224;++u) if (seen[u]) {
            if (keys<6) out[4+keys]=(uint8_t)u;
            ++keys;
        }
        if (keys>6) memset(out+4,1,6); /* HID ErrorRollOver */
        return 10;
    }
    if (id == HID_MOUSE && (s->mode & HID_MODE_MOUSE)) {
        const unsigned buttons[]={BTN_LEFT,BTN_RIGHT,BTN_MIDDLE,BTN_SIDE,BTN_EXTRA};
        for (size_t i=0;i<5;++i) if (pressed(s,buttons[i])) out[2] |= (uint8_t)(1u<<i);
        if (s->boot) { out[2]&=7; return 5; }
        return 7;
    }
    if (id == HID_CONSUMER && (s->mode & HID_MODE_KEYBOARD) && !s->boot) {
        for (size_t i=0;i<16;++i) if (pressed(s,consumer_keys[i])) out[2+i/8] |= (uint8_t)(1u<<(i%8));
        return 4;
    }
    return 0;
}
static int emit(struct hid_state *s, int id, hid_sink sink, void *context)
{
    uint8_t packet[16]; size_t n=report(s,id,packet);
    return n && !s->suspended ? sink(packet,n,context) : 0;
}
static int bounded_add(int current,int amount)
{
    int64_t n=(int64_t)current+amount;
    return n>32767 ? 32767 : n< -32767 ? -32767 : (int)n;
}
static int flush(struct hid_state *s, size_t device, hid_sink sink, void *context)
{
    struct hid_device *d=&s->devices[device];
    if (d->keyboard_dirty && emit(s,HID_KEYBOARD,sink,context)) return -1;
    if (d->consumer_dirty && emit(s,HID_CONSUMER,sink,context)) return -1;
    for (int i=0;i<2;++i) {
        if (d->has_hires[i]) {
            int total=bounded_add(d->remainder[i],d->hires[i]);
            d->motion[i+2]=total/120; d->remainder[i]=total%120;
        }
    }
    if (d->mouse_dirty && (s->mode & HID_MODE_MOUSE) && !s->suspended) {
        bool more;
        do {
            uint8_t packet[16]; size_t n=report(s,HID_MOUSE,packet);
            more=false;
            for (int axis=0;axis<4;++axis) {
                int part=d->motion[axis];
                if (s->boot && axis>=2) { d->motion[axis]=0; continue; }
                if (part>127) part=127;
                if (part< -127) part=-127;
                packet[3+axis]=(uint8_t)(int8_t)part;
                d->motion[axis]-=part;
                if (d->motion[axis]) more=true;
            }
            if (sink(packet,n,context)) return -1;
        } while (more);
    }
    memset(d->motion,0,sizeof(d->motion)); memset(d->hires,0,sizeof(d->hires));
    memset(d->has_hires,0,sizeof(d->has_hires));
    d->keyboard_dirty=d->consumer_dirty=d->mouse_dirty=false;
    return 0;
}
int hid_event(struct hid_state *s, size_t device, const struct input_event *e, hid_sink sink, void *context)
{
    if (device>=HID_MAX_DEVICES) return -1;
    struct hid_device *d=&s->devices[device];
    if (e->type==EV_SYN && e->code==SYN_DROPPED) { d->recovering=true; return 0; }
    if (d->recovering) return e->type==EV_SYN && e->code==SYN_REPORT ? 1 : 0;
    if (e->type==EV_KEY && e->code<=KEY_MAX && e->value!=2) {
        d->keys[e->code]=e->value!=0;
        if (hid_usage(e->code)) d->keyboard_dirty=true;
        if (e->code>=BTN_LEFT && e->code<=BTN_EXTRA) d->mouse_dirty=true;
        for (size_t i=0;i<16;++i) if (consumer_keys[i]==e->code) d->consumer_dirty=true;
    }
    if (e->type==EV_REL) {
        d->mouse_dirty=true;
        int axis=-1;
        switch(e->code) {
            case REL_X: axis=0; break; case REL_Y: axis=1; break;
            case REL_WHEEL: axis=2; break; case REL_HWHEEL: axis=3; break;
            case REL_WHEEL_HI_RES: d->has_hires[0]=true; d->hires[0]=bounded_add(d->hires[0],e->value); break;
            case REL_HWHEEL_HI_RES: d->has_hires[1]=true; d->hires[1]=bounded_add(d->hires[1],e->value); break;
        }
        if (axis>=0) d->motion[axis]=bounded_add(d->motion[axis],e->value);
    }
    return e->type==EV_SYN && e->code==SYN_REPORT ? flush(s,device,sink,context) : 0;
}
int hid_snapshot(struct hid_state *s,size_t device,const unsigned long *bits,size_t count,hid_sink sink,void *context)
{
    if (device>=HID_MAX_DEVICES) return -1;
    memset(&s->devices[device],0,sizeof(s->devices[device]));
    for (unsigned code=0;code<=KEY_MAX;++code) {
        size_t word=code/(8*sizeof(*bits)); unsigned bit=code%(8*sizeof(*bits));
        if (word<count) s->devices[device].keys[code]=(bits[word] & (1ul<<bit))!=0;
    }
    s->devices[device].keyboard_dirty=s->devices[device].consumer_dirty=s->devices[device].mouse_dirty=true;
    return flush(s,device,sink,context);
}
int hid_sync(struct hid_state *s,hid_sink sink,void *context)
{
    int result=emit(s,HID_KEYBOARD,sink,context) || emit(s,HID_CONSUMER,sink,context) || emit(s,HID_MOUSE,sink,context);
    if (!result) s->needs_sync=false;
    return result ? -1 : 0;
}
int hid_release(struct hid_state *s,hid_sink sink,void *context)
{
    memset(s->devices,0,sizeof(s->devices));
    bool suspended=s->suspended; s->suspended=false;
    int result=emit(s,HID_KEYBOARD,sink,context) || emit(s,HID_CONSUMER,sink,context) || emit(s,HID_MOUSE,sink,context);
    s->suspended=suspended;
    return result ? -1 : 0;
}
static int handshake(uint8_t status,hid_sink sink,void *context) { return sink(&status,1,context); }
int hid_control(struct hid_state *s,const uint8_t *p,size_t n,bool interrupt,hid_sink sink,void *context)
{
    if (!n) return -1;
    unsigned type=p[0]>>4, parameter=p[0]&15;
    if (interrupt) {
        if (p[0]==0xa2 && n==3 && p[1]==HID_KEYBOARD && (s->mode & HID_MODE_KEYBOARD)) s->leds=p[2]&31;
        return 0; /* Interrupt output reports have no handshake. */
    }
    if (type==1) {
        if (n!=1) return handshake(4,sink,context);
        if (parameter==3) s->suspended=true;
        else if (parameter==4) { s->suspended=false; s->needs_sync=true; }
        else if (parameter==5) return HID_CONTROL_UNPLUG; /* Virtual cable unplug. */
        else return handshake(3,sink,context);
        return 0;
    }
    if (type==6 && parameter==0 && n==1) { uint8_t value[]={0xa0,s->boot ? 0 : 1}; return sink(value,2,context); }
    if (type==7 && parameter<=1 && n==1) { s->boot=parameter==0; s->needs_sync=true; return handshake(0,sink,context); }
    if (type==4) {
        unsigned report_type=parameter&3; bool sized=(parameter&8)!=0;
        if ((parameter&4) || n!=(sized ? 4u : 2u)) return handshake(4,sink,context);
        uint8_t packet[16]; size_t length=0;
        if (report_type==1) length=report(s,p[1],packet);
        else if (report_type==2 && p[1]==HID_KEYBOARD && (s->mode & HID_MODE_KEYBOARD)) {
            packet[0]=0xa2; packet[1]=HID_KEYBOARD; packet[2]=s->leds; length=3;
        } else return handshake(3,sink,context);
        if (!length) return handshake(2,sink,context);
        if (sized) {
            unsigned limit=p[2]|((unsigned)p[3]<<8);
            if (!limit) return handshake(4,sink,context);
            if (length>limit+1u) length=limit+1u;
        }
        return sink(packet,length,context);
    }
    if (type==5) {
        if (parameter!=2) return handshake(3,sink,context);
        if (n!=3) return handshake(4,sink,context);
        if (p[1]!=HID_KEYBOARD || !(s->mode & HID_MODE_KEYBOARD)) return handshake(2,sink,context);
        s->leds=p[2]&31; return handshake(0,sink,context);
    }
    return handshake(3,sink,context);
}
