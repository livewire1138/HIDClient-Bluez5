#include <sys/socket.h>
#include <sys/wait.h>
#include <assert.h>
#include <stdio.h>
static int local_socket(int,int,int);
static int local_bind(int,const struct sockaddr *,socklen_t);
static int local_listen(int,int);
static int local_accept(int,struct sockaddr *,socklen_t *);
static int local_setsockopt(int,int,int,const void *,socklen_t);
#define socket local_socket
#define bind local_bind
#define listen local_listen
#define accept local_accept
#define setsockopt local_setsockopt
#define main hidclient_main
#include "../hidclient.c"
#undef main
#undef socket
#undef bind
#undef listen
#undef accept
#undef setsockopt

/* Pipe readiness + preconnected packet channels exercise main's actual poll
 * loop without a Bluetooth radio, network permissions, or a running daemon. */
static int triggers[2][2],transports[2][2],next_listener;
static int local_socket(int family,int type,int protocol)
{
    (void)type; assert(family==AF_BLUETOOTH && protocol==BTPROTO_L2CAP && next_listener<2);
    return dup(triggers[next_listener++][0]);
}
static int local_bind(int fd,const struct sockaddr *address,socklen_t length)
{ (void)fd;(void)address;(void)length; return 0; }
static int local_listen(int fd,int backlog) { (void)fd;(void)backlog; return 0; }
static int local_setsockopt(int fd,int level,int option,const void *value,socklen_t length)
{
    (void)fd;
    assert(level==SOL_BLUETOOTH && option==BT_SECURITY && length==sizeof(struct bt_security));
    assert(((const struct bt_security *)value)->level==BT_SECURITY_MEDIUM);
    return 0;
}
static int local_accept(int fd,struct sockaddr *address,socklen_t *length)
{
    char index; if (read(fd,&index,1)!=1) return -1;
    memset(address,0,*length); return dup(transports[(int)index][0]);
}
static void await_exit(pid_t pid,int expected)
{
    int status;
    for (int i=0;i<300;++i) {
        if (waitpid(pid,&status,WNOHANG)==pid) {
            assert(WIFEXITED(status) && WEXITSTATUS(status)==expected); return;
        }
        usleep(10000);
    }
    kill(pid,SIGKILL); waitpid(pid,&status,0); assert(!"shutdown timed out");
}
static void write_event(int fd,unsigned type,unsigned code,int value,bool split)
{
    struct input_event event={.type=type,.code=code,.value=value};
    if (split) {
        assert(write(fd,&event,5)==5); usleep(10000);
        assert(write(fd,(char *)&event+5,sizeof(event)-5)==sizeof(event)-5);
    } else assert(write(fd,&event,sizeof(event))==sizeof(event));
}
static void wait_for_key(int fd,uint8_t usage)
{
    for (int i=0;i<20;++i) {
        struct pollfd ready={fd,POLLIN,0}; assert(poll(&ready,1,2000)==1);
        uint8_t packet[16]; ssize_t n=recv(fd,packet,sizeof(packet),0); assert(n>0);
        if (n==10 && packet[1]==HID_KEYBOARD && packet[4]==usage) return;
    }
    assert(!"expected keyboard report missing");
}
static void lifecycle(int mode)
{
    char directory[]="/tmp/hidclient-test-XXXXXX"; assert(mkdtemp(directory));
    char fifo[256],argument[260]; snprintf(fifo,sizeof(fifo),"%s/events",directory);
    assert(mkfifo(fifo,0600)==0);
    int writer=open(fifo,O_RDWR | O_NONBLOCK); assert(writer>=0);
    snprintf(argument,sizeof(argument),"-f%s",fifo);
    next_listener=0;
    for (int i=0;i<2;++i) { assert(pipe(triggers[i])==0); assert(socketpair(AF_UNIX,SOCK_SEQPACKET,0,transports[i])==0); }
    fflush(NULL); pid_t pid=fork(); assert(pid>=0);
    if (!pid) {
        close(writer);
        for (int i=0;i<2;++i) { close(triggers[i][1]); close(transports[i][1]); }
        char *args[]={"hidclient","-s",argument,NULL}; _exit(hidclient_main(3,args));
    }
    for (int i=0;i<2;++i) { close(triggers[i][0]); close(transports[i][0]); }
    char first=mode==4 ? 1 : 0,second=1-first;
    assert(write(triggers[(int)first][1],&first,1)==1); usleep(20000);
    assert(write(triggers[(int)second][1],&second,1)==1); usleep(30000);
    int control=transports[0][1],interrupt=transports[1][1];
    write_event(writer,EV_KEY,KEY_A,1,mode==3); write_event(writer,EV_SYN,SYN_REPORT,0,false);
    wait_for_key(interrupt,4); /* Prove both channels are active before testing. */
    if (mode==0 || mode==3 || mode==4) {
        kill(pid,SIGTERM); await_exit(pid,0);
    } else if (mode==1) {
        close(interrupt); interrupt=-1; close(control); control=-1;
        usleep(100000); assert(kill(pid,0)==0); kill(pid,SIGINT); await_exit(pid,0);
    } else if (mode==2) {
        close(writer); writer=-1; await_exit(pid,1);
    } else if (mode==5) {
        write_event(writer,EV_KEY,KEY_LEFTCTRL,1,false);
        write_event(writer,EV_KEY,KEY_LEFTALT,1,false);
        write_event(writer,EV_KEY,KEY_F12,1,false);
        await_exit(pid,0);
    } else if (mode==6) {
        uint8_t request[]={0x60}; assert(write(control,request,1)==1);
        struct pollfd ready={control,POLLIN,0}; assert(poll(&ready,1,2000)==1);
        uint8_t response[2]; assert(read(control,response,2)==2); assert(response[0]==0xa0 && response[1]==1);
        kill(pid,SIGTERM); await_exit(pid,0);
    } else if (mode==8) {
        write_event(writer,EV_KEY,KEY_LEFTCTRL,1,false);
        write_event(writer,EV_KEY,KEY_LEFTALT,1,false);
        write_event(writer,EV_SYN,SYN_DROPPED,0,false);
        write_event(writer,EV_KEY,KEY_F12,1,false);
        usleep(100000);
        int status; assert(waitpid(pid,&status,WNOHANG)==0);
        kill(pid,SIGTERM); await_exit(pid,0);
    } else {
        write_event(writer,EV_SYN,SYN_DROPPED,0,false);
        write_event(writer,EV_SYN,SYN_REPORT,0,false);
        await_exit(pid,1); /* FIFO cannot provide the required evdev snapshot. */
    }
    if (writer>=0) close(writer);
    if (control>=0) close(control);
    if (interrupt>=0) close(interrupt);
    for (int i=0;i<2;++i) close(triggers[i][1]);
    unlink(fifo); rmdir(directory);
}
int main(void)
{
    for (int mode=0;mode<9;++mode) lifecycle(mode);
    puts("Nine connected lifecycle, channel-order, control and FIFO checks passed");
    return 0;
}
