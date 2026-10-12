/* Exercise real GDBus messages on a socket pair with a fake BlueZ service.
 * No system bus, Bluetooth radio, or system configuration is touched. */
#include <assert.h>
#include <sys/socket.h>
#include <unistd.h>
#include <gio/gunixinputstream.h>
#include <gio/gunixoutputstream.h>
#include "../bluez.c"

struct fixture {
    int fd;
    char *guid;
    GDBusConnection *connection;
    GMainContext *context;
    GMainLoop *loop;
    GMutex mutex;
    GCond condition;
    bool ready;
    int registrations,unregistrations,connections;
};
static const char fake_xml[]=
    "<node>"
    "<interface name='org.freedesktop.DBus'><method name='Hello'><arg type='s' direction='out'/></method><method name='GetNameOwner'><arg type='s' direction='in'/><arg type='s' direction='out'/></method></interface>"
    "<interface name='org.freedesktop.DBus.ObjectManager'><method name='GetManagedObjects'><arg type='a{oa{sa{sv}}}' direction='out'/></method></interface>"
    "<interface name='org.bluez.ProfileManager1'>"
    "<method name='RegisterProfile'><arg type='o' direction='in'/><arg type='s' direction='in'/><arg type='a{sv}' direction='in'/></method>"
    "<method name='UnregisterProfile'><arg type='o' direction='in'/></method></interface>"
    "<interface name='org.bluez.Device1'><method name='Connect'/></interface>"
    "</node>";
static GVariant *managed_objects(void)
{
    GVariantBuilder objects; g_variant_builder_init(&objects,G_VARIANT_TYPE("a{oa{sa{sv}}}"));
    for (int i=2;i<4;++i) {
        GVariantBuilder interfaces,properties;
        g_variant_builder_init(&interfaces,G_VARIANT_TYPE("a{sa{sv}}"));
        g_variant_builder_init(&properties,G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&properties,"{sv}","Address",g_variant_new_string(i==2 ? "01:02:03:04:05:06" : "11:12:13:14:15:16"));
        g_variant_builder_add(&properties,"{sv}","Powered",g_variant_new_boolean(TRUE));
        g_variant_builder_add(&interfaces,"{s@a{sv}}","org.bluez.Adapter1",g_variant_builder_end(&properties));
        char path[32]; snprintf(path,sizeof(path),"/org/bluez/hci%d",i);
        g_variant_builder_add(&objects,"{o@a{sa{sv}}}",path,g_variant_builder_end(&interfaces));
    }
    return g_variant_new("(@a{oa{sa{sv}}})",g_variant_builder_end(&objects));
}
static void fake_method(GDBusConnection *bus,const char *sender,const char *path,
                        const char *interface,const char *method,GVariant *parameters,
                        GDBusMethodInvocation *invocation,gpointer data)
{
    (void)bus;(void)sender;(void)path;(void)interface;
    struct fixture *f=data;
    if (!strcmp(method,"Hello")) {
        g_dbus_method_invocation_return_value(invocation,g_variant_new("(s)",":1.42"));
    } else if (!strcmp(method,"GetNameOwner")) {
        g_dbus_method_invocation_return_value(invocation,g_variant_new("(s)",":1.99"));
    } else if (!strcmp(method,"GetManagedObjects")) {
        g_dbus_method_invocation_return_value(invocation,managed_objects());
    } else if (!strcmp(method,"RegisterProfile")) {
        const char *profile,*uuid; GVariant *options;
        g_variant_get(parameters,"(&o&s@a{sv})",&profile,&uuid,&options);
        assert(!strcmp(profile,PROFILE_PATH) && !strcmp(uuid,HID_UUID));
        assert(!g_variant_lookup_value(options,"PSM",NULL));
        const char *record; assert(g_variant_lookup(options,"ServiceRecord","&s",&record));
        assert(strstr(record,"0x0206"));
        g_variant_unref(options); g_atomic_int_inc(&f->registrations);
        g_dbus_method_invocation_return_value(invocation,NULL);
    } else if (!strcmp(method,"UnregisterProfile")) {
        g_atomic_int_inc(&f->unregistrations); g_dbus_method_invocation_return_value(invocation,NULL);
    } else if (!strcmp(method,"Connect")) {
        g_atomic_int_inc(&f->connections); g_dbus_method_invocation_return_value(invocation,NULL);
    } else assert(!"unexpected fake service call");
}
static const GDBusInterfaceVTable fake_vtable={.method_call=fake_method};
static GIOStream *stream_for_fd(int fd)
{
    GInputStream *input=g_unix_input_stream_new(fd,TRUE);
    GOutputStream *output=g_unix_output_stream_new(dup(fd),TRUE);
    GIOStream *stream=g_simple_io_stream_new(input,output);
    g_object_unref(input); g_object_unref(output); return stream;
}
static gpointer server_thread(gpointer data)
{
    struct fixture *f=data;
    f->context=g_main_context_new(); g_main_context_push_thread_default(f->context);
    GIOStream *stream=stream_for_fd(f->fd); GError *error=NULL;
    f->connection=g_dbus_connection_new_sync(stream,f->guid,
        G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_SERVER | G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_ALLOW_ANONYMOUS | G_DBUS_CONNECTION_FLAGS_DELAY_MESSAGE_PROCESSING,
        NULL,NULL,&error);
    assert(f->connection && !error); g_object_unref(stream);
    g_dbus_connection_set_exit_on_close(f->connection,FALSE);
    GDBusNodeInfo *node=g_dbus_node_info_new_for_xml(fake_xml,&error); assert(node && !error);
    const char *paths[]={"/org/freedesktop/DBus","/","/org/bluez","/org/bluez/hci2/dev_AA_BB_CC_DD_EE_FF"};
    for (int i=0;i<4;++i) assert(g_dbus_connection_register_object(f->connection,paths[i],node->interfaces[i],&fake_vtable,f,NULL,&error));
    f->loop=g_main_loop_new(f->context,FALSE);
    g_dbus_connection_start_message_processing(f->connection);
    g_mutex_lock(&f->mutex); f->ready=true; g_cond_signal(&f->condition); g_mutex_unlock(&f->mutex);
    g_main_loop_run(f->loop);
    g_dbus_connection_close_sync(f->connection,NULL,NULL);
    g_object_unref(f->connection); g_dbus_node_info_unref(node); g_main_loop_unref(f->loop);
    g_main_context_pop_thread_default(f->context); g_main_context_unref(f->context);
    return NULL;
}
static gboolean quit_server(gpointer data) { g_main_loop_quit(data); return G_SOURCE_REMOVE; }
static void wait_flag(struct bluez_session *s,gboolean *flag)
{
    for (int i=0;i<200 && !*flag;++i) { bluez_dispatch(s); g_usleep(1000); }
    assert(*flag);
}
int main(void)
{
    int sockets[2]; assert(socketpair(AF_UNIX,SOCK_STREAM,0,sockets)==0);
    struct fixture f={.fd=sockets[0]}; f.guid=g_dbus_generate_guid();
    g_mutex_init(&f.mutex); g_cond_init(&f.condition);
    GThread *thread=g_thread_new("fake-bluez",server_thread,&f);
    GIOStream *stream=stream_for_fd(sockets[1]); GError *error=NULL;
    struct bluez_session session={0};
    session.bus=g_dbus_connection_new_sync(stream,NULL,G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,NULL,NULL,&error);
    assert(session.bus && !error); g_object_unref(stream);
    g_mutex_lock(&f.mutex); while (!f.ready) g_cond_wait(&f.condition,&f.mutex); g_mutex_unlock(&f.mutex);
    bdaddr_t local; assert(!setup_session(&session,"hci2",&local));
    char mac[18]; ba2str(&local,mac); assert(!strcmp(mac,"01:02:03:04:05:06"));
    assert(!strcmp(session.adapter_path,"/org/bluez/hci2"));
    assert(!bluez_register(&session,"Generic keyboard",HID_MODE_KEYBOARD));
    assert(session.registered && g_atomic_int_get(&f.registrations)==1);
    GDBusMessage *message=g_dbus_message_new_method_call(NULL,PROFILE_PATH,"org.bluez.Profile1","RequestDisconnection");
    g_dbus_message_set_sender(message,":1.98");
    g_dbus_message_set_flags(message,G_DBUS_MESSAGE_FLAGS_NO_REPLY_EXPECTED);
    g_dbus_message_set_body(message,g_variant_new("(o)","/org/bluez/hci2/dev_AA_BB_CC_DD_EE_FF"));
    assert(g_dbus_connection_send_message(f.connection,message,0,NULL,&error)); g_object_unref(message);
    for (int i=0;i<50;++i) { bluez_dispatch(&session); g_usleep(1000); }
    assert(!session.disconnect); /* A caller other than the daemon is rejected. */
    message=g_dbus_message_new_method_call(NULL,PROFILE_PATH,"org.bluez.Profile1","RequestDisconnection");
    g_dbus_message_set_sender(message,":1.99");
    g_dbus_message_set_flags(message,G_DBUS_MESSAGE_FLAGS_NO_REPLY_EXPECTED);
    g_dbus_message_set_body(message,g_variant_new("(o)","/org/bluez/hci2/dev_AA_BB_CC_DD_EE_FF"));
    assert(g_dbus_connection_send_message(f.connection,message,0,NULL,&error)); g_object_unref(message);
    wait_flag(&session,&session.disconnect);
    /* Verify daemon owner changes invalidate the session. */
    GVariant *owners=g_variant_ref_sink(g_variant_new("(sss)","org.bluez",":1.99",":1.100"));
    owner_changed(NULL,NULL,NULL,NULL,NULL,owners,&session); assert(session.lost); g_variant_unref(owners);
    session.lost=FALSE; /* Keep the fake daemon alive to test normal unregister. */
    bluez_close(&session); assert(g_atomic_int_get(&f.unregistrations)==1);
    g_main_context_invoke(f.context,quit_server,f.loop); g_thread_join(thread);
    g_free(f.guid); g_cond_clear(&f.condition); g_mutex_clear(&f.mutex);
    puts("GDBus adapter selection, registration, callbacks and cleanup checks passed");
    return 0;
}
