#include "../hid.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static struct hid_state s;
struct capture { uint8_t data[1024][16]; size_t lengths[1024],count; };
static struct capture captured;
static int save(const uint8_t *p,size_t n,void *unused)
{
    (void)unused; assert(n<=16 && captured.count<1024);
    memcpy(captured.data[captured.count],p,n); captured.lengths[captured.count++]=n;
    return 0;
}
static void clear(void) { memset(&captured,0,sizeof(captured)); }
static void init(int mode) { hid_init(&s,mode,false); clear(); }
static void event(size_t device,unsigned type,unsigned code,int value)
{
    struct input_event e={.type=type,.code=code,.value=value};
    assert(hid_event(&s,device,&e,save,NULL)==0);
}
static void sync(size_t device) { event(device,EV_SYN,SYN_REPORT,0); }
static void key(size_t device,unsigned code,int value) { event(device,EV_KEY,code,value); }
static void control(const uint8_t *p,size_t n) { clear(); assert(hid_control(&s,p,n,false,save,NULL)==0); }
static void keyboard(void)
{
    init(HID_MODE_KEYBOARD); key(0,KEY_A,1); assert(!captured.count); sync(0);
    assert(captured.count==1 && captured.lengths[0]==10);
    assert(captured.data[0][0]==0xa1 && captured.data[0][1]==HID_KEYBOARD);
    assert(captured.data[0][2]==0 && captured.data[0][3]==0 && captured.data[0][4]==4);
    clear(); key(0,KEY_LEFTMETA,1); key(0,KEY_LEFTALT,1); sync(0);
    assert(captured.data[0][2]==12); /* Command + Option */
    clear(); key(0,KEY_A,0); sync(0); assert(captured.data[0][4]==0);
    clear(); key(0,KEY_A,2); sync(0); assert(!captured.count);
    assert(hid_usage(KEY_PAUSE)==72 && hid_usage(KEY_SCROLLLOCK)==71);
    assert(hid_usage(KEY_KATAKANAHIRAGANA)==136 && hid_usage(KEY_YEN)==137);
    s.swap_ctrl_meta=true; clear(); key(0,KEY_LEFTCTRL,1); sync(0);
    assert(captured.data[0][2]==13); /* left Meta becomes Ctrl; Ctrl becomes Meta */
    assert(hid_keycode("F24")==KEY_F24 && hid_keycode("none")== -1 && hid_keycode("bogus")== -2);
}
static void multiple_keyboards(void)
{
    init(HID_MODE_KEYBOARD); key(0,KEY_A,1); sync(0); key(1,KEY_A,1); sync(1);
    clear(); key(0,KEY_A,0); sync(0); assert(captured.data[0][4]==4);
    clear(); key(1,KEY_A,0); sync(1); assert(captured.data[0][4]==0);
    const unsigned keys[]={KEY_A,KEY_B,KEY_C,KEY_D,KEY_E,KEY_F,KEY_G};
    for (size_t i=0;i<7;++i) key(0,keys[i],1);
    clear(); sync(0); for (int i=4;i<10;++i) assert(captured.data[0][i]==1);
    clear(); key(0,KEY_G,0); sync(0); assert(captured.data[0][4]==4);
}
static void consumer(void)
{
    init(HID_MODE_KEYBOARD); key(0,KEY_VOLUMEUP,1); key(0,KEY_PLAYPAUSE,1); sync(0);
    assert(captured.count==1 && captured.lengths[0]==4);
    assert(captured.data[0][1]==HID_CONSUMER && captured.data[0][2]==33);
    clear(); key(0,KEY_VOLUMEUP,0); sync(0); assert(captured.data[0][2]==1);
}
static void mouse(void)
{
    init(HID_MODE_MOUSE); key(0,BTN_RIGHT,1); event(0,EV_REL,REL_X,500); event(0,EV_REL,REL_Y,-300);
    event(0,EV_REL,REL_WHEEL,2); event(0,EV_REL,REL_HWHEEL,-1); assert(!captured.count); sync(0);
    int x=0,y=0,wheel=0,pan=0; assert(captured.count==4);
    for (size_t i=0;i<captured.count;++i) {
        assert(captured.lengths[i]==7 && captured.data[i][1]==HID_MOUSE && captured.data[i][2]==2);
        x+=(int8_t)captured.data[i][3]; y+=(int8_t)captured.data[i][4];
        wheel+=(int8_t)captured.data[i][5]; pan+=(int8_t)captured.data[i][6];
    }
    assert(x==500 && y== -300 && wheel==2 && pan== -1);
    clear(); sync(0); assert(!captured.count);
}
static void high_resolution_wheel(void)
{
    init(HID_MODE_MOUSE); event(0,EV_REL,REL_WHEEL_HI_RES,60); sync(0);
    assert(captured.data[0][5]==0);
    clear(); event(0,EV_REL,REL_WHEEL_HI_RES,60); sync(0); assert(captured.data[0][5]==1);
    clear(); event(0,EV_REL,REL_WHEEL_HI_RES,120); event(0,EV_REL,REL_WHEEL,1); sync(0);
    assert(captured.count==1 && captured.data[0][5]==1); /* no double scroll */
}
static void protocol(void)
{
    init(HID_MODE_COMBO);
    const uint8_t get_protocol[]={0x60}; control(get_protocol,1);
    assert(captured.count==1 && captured.lengths[0]==2 && captured.data[0][0]==0xa0 && captured.data[0][1]==1);
    const uint8_t boot[]={0x70}; control(boot,1); assert(s.boot && captured.data[0][0]==0);
    clear(); event(0,EV_REL,REL_X,3); event(0,EV_REL,REL_WHEEL,1); key(0,BTN_EXTRA,1); sync(0);
    assert(captured.count==1 && captured.lengths[0]==5 && captured.data[0][2]==0 && captured.data[0][3]==3);
    const uint8_t normal[]={0x71}; control(normal,1); assert(!s.boot && captured.data[0][0]==0);
    const uint8_t invalid[]={0x72}; control(invalid,1); assert(captured.data[0][0]==3);
    const uint8_t get_report[]={0x41,HID_KEYBOARD}; control(get_report,2);
    assert(captured.lengths[0]==10 && captured.data[0][0]==0xa1);
    const uint8_t sized[]={0x49,HID_KEYBOARD,4,0}; control(sized,4); assert(captured.lengths[0]==5);
    const uint8_t unknown[]={0x41,99}; control(unknown,2); assert(captured.data[0][0]==2);
    const uint8_t leds[]={0x52,HID_KEYBOARD,7}; control(leds,3); assert(s.leds==7 && captured.data[0][0]==0);
    const uint8_t get_leds[]={0x42,HID_KEYBOARD}; control(get_leds,2);
    assert(captured.lengths[0]==3 && captured.data[0][0]==0xa2 && captured.data[0][2]==7);
    const uint8_t interrupt_leds[]={0xa2,HID_KEYBOARD,2}; clear();
    assert(!hid_control(&s,interrupt_leds,3,true,save,NULL)); assert(s.leds==2 && !captured.count);
    const uint8_t suspend[]={0x13}; control(suspend,1); assert(s.suspended && !captured.count);
    key(0,KEY_B,1); sync(0); assert(!captured.count);
    const uint8_t resume[]={0x14}; control(resume,1); assert(!s.suspended && s.needs_sync);
    clear(); assert(!hid_sync(&s,save,NULL)); assert(captured.data[0][4]==5);
    const uint8_t unplug[]={0x15}; assert(hid_control(&s,unplug,1,false,save,NULL)==1);
}
static void recovery(void)
{
    init(HID_MODE_COMBO); key(0,KEY_A,1); sync(0); clear();
    event(0,EV_SYN,SYN_DROPPED,0); key(0,KEY_B,1); assert(!captured.count);
    struct input_event syn={.type=EV_SYN,.code=SYN_REPORT}; assert(hid_event(&s,0,&syn,save,NULL)==1);
    unsigned long bits[(KEY_MAX+1+8*sizeof(unsigned long)-1)/(8*sizeof(unsigned long))]={0};
    bits[KEY_C/(8*sizeof(unsigned long))] |= 1ul << (KEY_C%(8*sizeof(unsigned long)));
    assert(!hid_snapshot(&s,0,bits,sizeof(bits)/sizeof(bits[0]),save,NULL));
    assert(!s.devices[0].recovering && captured.data[0][4]==6);
    clear(); assert(!hid_release(&s,save,NULL)); assert(captured.data[0][4]==0 && captured.count==3);
}
static void malformed(void)
{
    init(HID_MODE_COMBO);
    /* Every possible transaction header at short lengths must stay bounded. */
    for (unsigned header=0;header<256;++header) {
        for (size_t n=0;n<16;++n) {
            uint8_t packet[16]={0}; packet[0]=(uint8_t)header; packet[1]=HID_KEYBOARD;
            clear(); hid_control(&s,packet,n,false,save,NULL);
        }
    }
    size_t length; uint8_t *descriptor=hid_descriptor(HID_MODE_COMBO,&length);
    assert(descriptor && length>100); free(descriptor);
}
int main(void)
{
    keyboard(); multiple_keyboards(); consumer(); mouse(); high_resolution_wheel();
    protocol(); recovery(); malformed();
    puts("HID encoding, control, boot protocol, recovery and malformed-packet checks passed");
    return 0;
}
