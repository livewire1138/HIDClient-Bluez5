/*
 * hidclient - Bluetooth HID keyboard/mouse emulation
 * Copyright 2004-2006 Anselm Martin Hoffmeister
 * BlueZ port and original implementation by Anselm Martin Hoffmeister.
 * Updates in 2012 by Peter G; later BlueZ 5 and portability updates.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, strictly version 2 only.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include "hid.h"
#include "bluez.h"
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <bluetooth/l2cap.h>

#define MAXEVDEVS HID_MAX_DEVICES
#define PSMHIDCTL 0x11
#define PSMHIDINT 0x13
static volatile sig_atomic_t stopping;
static int eventdevs[MAXEVDEVS];
static char *device_paths[MAXEVDEVS];
static size_t device_count;
static struct hid_state state;
static int exit_key=KEY_F12, disconnect_key=-1;
static bool grab_input, debug_events;
static bdaddr_t local_address;
static size_t buffered[MAXEVDEVS];
static uint8_t input_buffer[MAXEVDEVS][sizeof(struct input_event)];

static int send_packet(const uint8_t *data,size_t length,void *context)
{
    int fd=*(int *)context;
    if (fd<0) return 0;
    struct iovec iov={(void *)data,length};
    struct msghdr message={0}; message.msg_iov=&iov; message.msg_iovlen=1;
    for (;;) {
        ssize_t sent=sendmsg(fd,&message,MSG_NOSIGNAL | MSG_DONTWAIT);
        if (sent==(ssize_t)length) return 0;
        if (sent<0 && errno==EINTR && !stopping) continue;
        if (sent<0 && (errno==EAGAIN || errno==EWOULDBLOCK)) {
            struct pollfd writable={fd,POLLOUT,0};
            if (!stopping && poll(&writable,1,100)>0 && (writable.revents & POLLOUT)) {
                sent=sendmsg(fd,&message,MSG_NOSIGNAL | MSG_DONTWAIT);
                if (sent==(ssize_t)length) return 0;
            }
        }
        return -1;
    }
}
static int add_device(const char *path)
{
    if (!*path || device_count==MAXEVDEVS) return -1;
    device_paths[device_count]=strdup(path);
    if (!device_paths[device_count]) return -1;
    ++device_count; return 0;
}
static void closeevents(void)
{
    for (size_t i=0;i<MAXEVDEVS;++i) {
        if (eventdevs[i]>=0) close(eventdevs[i]);
        eventdevs[i]=-1;
    }
}
static int set_grabs(int enable)
{
    for (size_t i=0;i<device_count;++i) {
        if (ioctl(eventdevs[i],EVIOCGRAB,enable)<0 && enable) {
            fprintf(stderr,"Cannot grab %s: %s\n",device_paths[i],strerror(errno));
            for (size_t j=0;j<i;++j) ioctl(eventdevs[j],EVIOCGRAB,0);
            return -1;
        }
    }
    return 0;
}
static int initevents(const char *fifo)
{
    if (fifo) {
        struct stat status;
        if (lstat(fifo,&status)<0) {
            if (errno!=ENOENT || mkfifo(fifo,0600)<0) { perror("FIFO"); return -1; }
        } else if (!S_ISFIFO(status.st_mode)) { fprintf(stderr,"Expected a FIFO: %s\n",fifo); return -1; }
        eventdevs[0]=open(fifo,O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
        if (eventdevs[0]<0) { perror("FIFO"); return -1; }
        device_count=1; return 0;
    }
    for (size_t i=0;i<device_count;++i) {
        struct input_id id; struct stat current,previous;
        eventdevs[i]=open(device_paths[i],O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (eventdevs[i]<0 || ioctl(eventdevs[i],EVIOCGID,&id)<0 || fstat(eventdevs[i],&current)<0) {
            fprintf(stderr,"Cannot open evdev %s: %s\n",device_paths[i],strerror(errno)); return -1;
        }
        for (size_t j=0;j<i;++j) {
            if (fstat(eventdevs[j],&previous)<0) return -1;
            if (current.st_rdev==previous.st_rdev) { fprintf(stderr,"Duplicate input node: %s\n",device_paths[i]); return -1; }
        }
    }
    return 0;
}
static int list_devices(void)
{
    glob_t devices={0};
    if (glob("/dev/input/event*",0,NULL,&devices)==0) {
        for (size_t i=0;i<devices.gl_pathc;++i) {
            char name[256]={0}; int fd=open(devices.gl_pathv[i],O_RDONLY | O_CLOEXEC);
            if (fd<0) { printf("%s [unavailable: %s]\n",devices.gl_pathv[i],strerror(errno)); continue; }
            if (ioctl(fd,EVIOCGNAME(sizeof(name)),name)>=0) printf("%s '%s'\n",devices.gl_pathv[i],name);
            close(fd);
        }
    }
    globfree(&devices); return 0;
}
static bool key_pressed(unsigned code)
{
    for (size_t i=0;i<device_count;++i) if (!state.devices[i].recovering && state.devices[i].keys[code]) return true;
    return false;
}
static int read_event(size_t device,int output)
{
    ssize_t n=read(eventdevs[device],input_buffer[device]+buffered[device],sizeof(struct input_event)-buffered[device]);
    if (n<0 && (errno==EAGAIN || errno==EINTR)) return 0;
    if (n<=0) { fprintf(stderr,"Input device closed or failed\n"); return -2; }
    buffered[device]+=(size_t)n;
    if (buffered[device]<sizeof(struct input_event)) return 0;
    struct input_event event;
    memcpy(&event,input_buffer[device],sizeof(event)); buffered[device]=0;
    if (debug_events) { printf("EVENT %u %u %d\n",event.type,event.code,event.value); fflush(stdout); }
    if (!state.devices[device].recovering && event.type==EV_KEY && event.value==1 &&
        (key_pressed(KEY_LEFTCTRL) || key_pressed(KEY_RIGHTCTRL)) &&
        (key_pressed(KEY_LEFTALT) || key_pressed(KEY_RIGHTALT))) {
        if ((int)event.code==exit_key) { stopping=1; return 0; }
        if ((int)event.code==disconnect_key) return -1;
    }
    int result=hid_event(&state,device,&event,send_packet,&output);
    if (result==1) {
        unsigned long keys[(KEY_MAX+1+8*sizeof(unsigned long)-1)/(8*sizeof(unsigned long))]={0};
        if (ioctl(eventdevs[device],EVIOCGKEY(sizeof(keys)),keys)<0) {
            fprintf(stderr,"Cannot recover lost input state (FIFO has no evdev snapshot)\n"); return -2;
        }
        return hid_snapshot(&state,device,keys,sizeof(keys)/sizeof(keys[0]),send_packet,&output);
    }
    return result;
}
static int btbind(int fd,unsigned short port)
{
    struct sockaddr_l2 address={0}; address.l2_family=AF_BLUETOOTH;
    bacpy(&address.l2_bdaddr,&local_address); address.l2_psm=htobs(port);
    if (bind(fd,(struct sockaddr *)&address,sizeof(address))<0) { perror("Bluetooth bind (is the input plugin disabled?)"); return -1; }
    struct bt_security security={.level=BT_SECURITY_MEDIUM};
    if (setsockopt(fd,SOL_BLUETOOTH,BT_SECURITY,&security,sizeof(security))<0) { perror("Bluetooth authentication"); return -1; }
    return 0;
}
static int64_t monotonic_ms(void)
{
    struct timespec now; clock_gettime(CLOCK_MONOTONIC,&now);
    return (int64_t)now.tv_sec*1000+now.tv_nsec/1000000;
}
static void onsignal(int number) { (void)number; stopping=1; }
static void help(void)
{
    puts("hidclient - standard Bluetooth Classic HID peripheral (Linux sender)\n"
         "  --device PATH       Select evdev input (repeatable; by-id preferred)\n"
         "  -eNUM / -l          Select eventNUM / list inputs\n"
         "  --all               Explicitly select all inputs (up to 64)\n"
         "  --grab, -x          Exclusively grab input while connected\n"
         "  --mode MODE         keyboard, mouse, combo (default combo)\n"
         "  --adapter hciN      Select the local powered Bluetooth adapter\n"
         "  --name NAME         SDP service name (does not rename the adapter)\n"
         "  --host MAC          Accept only this host\n"
         "  --swap-ctrl-meta    Swap Control and Command/Windows modifiers\n"
         "  --exit-key KEY      Ctrl+Alt+KEY exits (default F12; none disables)\n"
         "  --disconnect-key KEY  Ctrl+Alt+KEY disconnects (default none)\n"
         "                      KEY: F1..F24, Pause, ScrollLock, Esc, none\n"
         "  -fPATH              Read native Linux input_event records from FIFO\n"
         "  -s, --skipsdp       Debug: skip D-Bus registration; binds any adapter\n"
         "  --print-sdp         Print SDP XML offline\n"
         "  --print-descriptor  Print binary report descriptor offline\n"
         "  -d                  Log input events\n"
         "Plain Pause/ScrollLock are normal keys. SIGINT/SIGTERM/SIGHUP stop.");
}
int main(int argc,char **argv)
{
    int listeners[2]={-1,-1},channels[2]={-1,-1};
    int result=1,mode=HID_MODE_COMBO;
    bool skipsdp=false,all=false,swap=false,print_sdp=false,print_descriptor=false;
    const char *fifo=NULL,*adapter=NULL,*name="Bluetooth HID";
    bool restricted=false; bdaddr_t host,peer;
    int64_t partial_since=0;
    struct bluez_session bluez={0};
    for (size_t i=0;i<MAXEVDEVS;++i) eventdevs[i]=-1;
    for (int i=1;i<argc;++i) {
        const char *arg=argv[i];
        if (!strcmp(arg,"--help") || !strcmp(arg,"-h") || !strcmp(arg,"-?")) { help(); result=0; goto cleanup; }
        else if (!strcmp(arg,"-l")) { result=list_devices(); goto cleanup; }
        else if (!strcmp(arg,"--device") && i+1<argc) { if (add_device(argv[++i])) goto usage; }
        else if (!strncmp(arg,"-e",2)) {
            char *end; errno=0; long index=strtol(arg+2,&end,10); char path[64];
            if (errno || end==arg+2 || *end || index<0 || index>65535) goto usage;
            snprintf(path,sizeof(path),"/dev/input/event%ld",index); if (add_device(path)) goto usage;
        } else if (!strcmp(arg,"--mode") && i+1<argc) {
            const char *value=argv[++i];
            if (!strcmp(value,"keyboard")) mode=HID_MODE_KEYBOARD;
            else if (!strcmp(value,"mouse")) mode=HID_MODE_MOUSE;
            else if (!strcmp(value,"combo")) mode=HID_MODE_COMBO;
            else goto usage;
        } else if (!strcmp(arg,"--adapter") && i+1<argc) {
            adapter=argv[++i];
            if (strncmp(adapter,"hci",3) || !adapter[3] || strspn(adapter+3,"0123456789")!=strlen(adapter+3)) goto usage;
        } else if (!strcmp(arg,"--name") && i+1<argc) {
            name=argv[++i]; if (!*name || strlen(name)>128 || !g_utf8_validate(name,-1,NULL)) goto usage;
        } else if (!strcmp(arg,"--host") && i+1<argc) {
            const char *mac=argv[++i]; if (strlen(mac)!=17 || str2ba(mac,&host)) goto usage; restricted=true;
        } else if (!strcmp(arg,"--connect")) goto usage;
        else if (!strcmp(arg,"--swap-ctrl-meta")) swap=true;
        else if (!strcmp(arg,"--exit-key") && i+1<argc) { exit_key=hid_keycode(argv[++i]); if (exit_key== -2) goto usage; }
        else if (!strcmp(arg,"--disconnect-key") && i+1<argc) { disconnect_key=hid_keycode(argv[++i]); if (disconnect_key== -2) goto usage; }
        else if (!strcmp(arg,"--grab") || !strcmp(arg,"-x")) grab_input=true;
        else if (!strcmp(arg,"--all")) all=true;
        else if (!strcmp(arg,"-s") || !strcmp(arg,"--skipsdp")) skipsdp=true;
        else if (!strcmp(arg,"-d")) debug_events=true;
        else if (!strcmp(arg,"--print-sdp")) print_sdp=true;
        else if (!strcmp(arg,"--print-descriptor")) print_descriptor=true;
        else if (!strncmp(arg,"-f",2) && arg[2]) fifo=arg+2;
        else goto usage;
    }
    if (exit_key>=0 && exit_key==disconnect_key) goto usage;
    if (print_sdp || print_descriptor) {
        if (print_sdp) { char *xml=bluez_service_record(name,mode); if (!xml) goto cleanup; puts(xml); g_free(xml); }
        else { size_t n; uint8_t *data=hid_descriptor(mode,&n); if (!data) goto cleanup; result=fwrite(data,1,n,stdout)==n ? 0 : 1; free(data); goto cleanup; }
        result=0; goto cleanup;
    }
    if ((fifo && (device_count || all || grab_input)) || (all && device_count) ||
        (skipsdp && adapter)) goto usage;
    if (all) {
        glob_t devices={0};
        if (glob("/dev/input/event*",0,NULL,&devices)==0) {
            for (size_t i=0;i<devices.gl_pathc;++i) if (add_device(devices.gl_pathv[i])) { globfree(&devices); goto usage; }
        }
        globfree(&devices);
    }
    if (!fifo && !device_count) { fprintf(stderr,"Select --device PATH, -eNUM, or explicitly --all.\n"); goto cleanup; }
    struct sigaction action={0}; action.sa_handler=onsignal; sigemptyset(&action.sa_mask);
    sigaction(SIGINT,&action,NULL); sigaction(SIGTERM,&action,NULL); sigaction(SIGHUP,&action,NULL);
    hid_init(&state,mode,swap);
    if (initevents(fifo)) goto cleanup;
    if (!skipsdp && bluez_open(&bluez,adapter,&local_address)) goto cleanup;
    for (int i=0;i<2;++i) {
        listeners[i]=socket(AF_BLUETOOTH,SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC,BTPROTO_L2CAP);
        if (listeners[i]<0 || btbind(listeners[i],i ? PSMHIDINT : PSMHIDCTL) || listen(listeners[i],1)<0) goto cleanup;
    }
    /* Advertise only after both transport sockets are ready. */
    if (!skipsdp && bluez_register(&bluez,name,mode)) goto cleanup;
    puts("HID client ready; Ctrl+C or the configured Ctrl+Alt exit chord stops it."); fflush(stdout);
    result=0;
    while (!stopping) {
        if (!skipsdp) {
            bluez_dispatch(&bluez);
            if (bluez.lost) { fprintf(stderr,"BlueZ connection/profile lost; restart hidclient\n"); result=1; break; }
        }
        struct pollfd fds[MAXEVDEVS+4]; int count=4; size_t devices[MAXEVDEVS];
        for (int i=0;i<2;++i) { fds[i]=(struct pollfd){listeners[i],POLLIN,0}; fds[i+2]=(struct pollfd){channels[i],POLLIN,0}; }
        for (size_t i=0;i<device_count;++i) { devices[count-4]=i; fds[count++]=(struct pollfd){eventdevs[i],POLLIN,0}; }
        int ready=poll(fds,count,100);
        if (ready<0) { if (errno==EINTR) continue; perror("poll"); result=1; break; }
        bool disconnect=bluez.disconnect; bluez.disconnect=FALSE;
        for (int i=0;i<2;++i) {
            if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) { result=1; stopping=1; break; }
            if (!(fds[i].revents & POLLIN)) continue;
            struct sockaddr_l2 remote={0}; socklen_t length=sizeof(remote);
            int fd=accept(listeners[i],(struct sockaddr *)&remote,&length);
            if (fd<0) continue;
            fcntl(fd,F_SETFD,FD_CLOEXEC); fcntl(fd,F_SETFL,O_NONBLOCK);
            if (channels[i]>=0 || (restricted && bacmp(&host,&remote.l2_bdaddr)) ||
                (channels[1-i]>=0 && bacmp(&peer,&remote.l2_bdaddr))) { close(fd); continue; }
            if (channels[1-i]<0) { bacpy(&peer,&remote.l2_bdaddr); partial_since=monotonic_ms(); }
            channels[i]=fd;
            if (channels[0]>=0 && channels[1]>=0) {
                if (grab_input && set_grabs(1)) { result=1; stopping=1; break; }
                if (!fifo) {
                    for (size_t device=0;device<device_count;++device) {
                        unsigned long keys[(KEY_MAX+1+8*sizeof(unsigned long)-1)/(8*sizeof(unsigned long))]={0};
                        int discard=-1;
                        if (ioctl(eventdevs[device],EVIOCGKEY(sizeof(keys)),keys)<0 ||
                            hid_snapshot(&state,device,keys,sizeof(keys)/sizeof(keys[0]),send_packet,&discard)) {
                            fprintf(stderr,"Cannot snapshot input state at connection\n");
                            result=1; stopping=1; break;
                        }
                    }
                    if (stopping) break;
                }
                /* Reset host protocol state for each new connection. */
                state.boot=false; state.suspended=false; state.leds=0; state.needs_sync=true;
            }
        }
        for (int i=0;i<2;++i) {
            if (fds[i+2].revents & (POLLERR | POLLHUP | POLLNVAL)) disconnect=true;
            if (fds[i+2].revents & POLLIN) {
                uint8_t packet[256]; ssize_t n=recv(channels[i],packet,sizeof(packet),MSG_DONTWAIT | MSG_TRUNC);
                if (n==0 || n>(ssize_t)sizeof(packet) || (n<0 && errno!=EAGAIN && errno!=EINTR)) disconnect=true;
                else if (n>0 && hid_control(&state,packet,(size_t)n,i==1,send_packet,&channels[i])!=0) disconnect=true;
            }
        }
        if ((channels[0]>=0)!=(channels[1]>=0)) {
            if (monotonic_ms()-partial_since>=5000) disconnect=true;
        }
        if (state.needs_sync && !state.suspended && channels[0]>=0 && channels[1]>=0 && !disconnect)
            if (hid_sync(&state,send_packet,&channels[1])) disconnect=true;
        int output=channels[0]>=0 && channels[1]>=0 && !disconnect ? channels[1] : -1;
        for (int i=4;i<count;++i) {
            if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) { fprintf(stderr,"Input device disconnected\n"); result=1; stopping=1; break; }
            if (fds[i].revents & POLLIN) {
                int parsed=read_event(devices[i-4],output);
                if (parsed<0) disconnect=true;
                if (parsed< -1) { result=1; stopping=1; break; }
            }
        }
        if (disconnect) {
            hid_release(&state,send_packet,&channels[1]);
            if (grab_input) set_grabs(0);
            for (int i=0;i<2;++i) { if (channels[i]>=0) close(channels[i]); channels[i]=-1; }
            hid_init(&state,mode,swap);
        }
    }
    goto cleanup;
usage:
    fprintf(stderr,"Invalid arguments; use --help.\n");
cleanup:
    hid_release(&state,send_packet,&channels[1]);
    for (int i=0;i<2;++i) { if (channels[i]>=0) close(channels[i]); if (listeners[i]>=0) close(listeners[i]); }
    closeevents(); bluez_close(&bluez);
    for (size_t i=0;i<device_count;++i) free(device_paths[i]);
    return result;
}
