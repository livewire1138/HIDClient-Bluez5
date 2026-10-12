/* BlueZ D-Bus SDP registration. GPL-2.0-only. */
#include "bluez.h"
#include "hid.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PROFILE_PATH "/org/hidclient/profile"
#define HID_UUID "00001124-0000-1000-8000-00805f9b34fb"

char *bluez_service_record(const char *name, int mode)
{
    size_t length;
    uint8_t *descriptor=hid_descriptor(mode,&length);
    if (!descriptor) return NULL;
    GString *hex=g_string_sized_new(length*2);
    for (size_t i=0;i<length;++i) g_string_append_printf(hex,"%02x",descriptor[i]);
    free(descriptor);
    char *escaped=g_markup_escape_text(name,-1);
    unsigned subclass=mode==HID_MODE_KEYBOARD ? 0x40 : mode==HID_MODE_MOUSE ? 0x80 : 0xc0;
    char *xml=g_strdup_printf(
        "<?xml version=\"1.0\"?><record>"
        "<attribute id=\"0x0001\"><sequence><uuid value=\"0x1124\"/></sequence></attribute>"
        "<attribute id=\"0x0004\"><sequence>"
          "<sequence><uuid value=\"0x0100\"/><uint16 value=\"0x0011\"/></sequence>"
          "<sequence><uuid value=\"0x0011\"/></sequence></sequence></attribute>"
        "<attribute id=\"0x0005\"><sequence><uuid value=\"0x1002\"/></sequence></attribute>"
        "<attribute id=\"0x0006\"><sequence><uint16 value=\"0x656e\"/><uint16 value=\"0x006a\"/><uint16 value=\"0x0100\"/></sequence></attribute>"
        "<attribute id=\"0x0009\"><sequence><sequence><uuid value=\"0x1124\"/><uint16 value=\"0x0101\"/></sequence></sequence></attribute>"
        "<attribute id=\"0x000d\"><sequence><sequence>"
          "<sequence><uuid value=\"0x0100\"/><uint16 value=\"0x0013\"/></sequence>"
          "<sequence><uuid value=\"0x0011\"/></sequence></sequence></sequence></attribute>"
        "<attribute id=\"0x0100\"><text value=\"%s\"/></attribute>"
        "<attribute id=\"0x0101\"><text value=\"Standard Bluetooth HID\"/></attribute>"
        "<attribute id=\"0x0102\"><text value=\"hidclient\"/></attribute>"
        "<attribute id=\"0x0200\"><uint16 value=\"0x0100\"/></attribute>"
        "<attribute id=\"0x0201\"><uint16 value=\"0x0111\"/></attribute>"
        "<attribute id=\"0x0202\"><uint8 value=\"0x%02x\"/></attribute>"
        "<attribute id=\"0x0203\"><uint8 value=\"0x00\"/></attribute>"
        "<attribute id=\"0x0204\"><boolean value=\"true\"/></attribute>"
        "<attribute id=\"0x0205\"><boolean value=\"false\"/></attribute>"
        "<attribute id=\"0x0206\"><sequence><sequence><uint8 value=\"0x22\"/>"
          "<text encoding=\"hex\" value=\"%s\"/></sequence></sequence></attribute>"
        "<attribute id=\"0x0207\"><sequence><sequence><uint16 value=\"0x0409\"/><uint16 value=\"0x0100\"/></sequence></sequence></attribute>"
        "<attribute id=\"0x0209\"><boolean value=\"false\"/></attribute>"
        "<attribute id=\"0x020a\"><boolean value=\"false\"/></attribute>"
        "<attribute id=\"0x020b\"><uint16 value=\"0x0101\"/></attribute>"
        "<attribute id=\"0x020d\"><boolean value=\"true\"/></attribute>"
        "<attribute id=\"0x020e\"><boolean value=\"true\"/></attribute>"
        "</record>",escaped,subclass,hex->str);
    g_free(escaped); g_string_free(hex,TRUE);
    return xml;
}
GVariant *bluez_profile_options(const char *name,int mode)
{
    char *xml=bluez_service_record(name,mode);
    if (!xml) return NULL;
    GVariantBuilder options;
    g_variant_builder_init(&options,G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&options,"{sv}","Name",g_variant_new_string(name));
    g_variant_builder_add(&options,"{sv}","Role",g_variant_new_string("server"));
    g_variant_builder_add(&options,"{sv}","ServiceRecord",g_variant_new_string(xml));
    /* Deliberately omit PSM/Channel: BlueZ registers our record, while this
     * process owns BOTH Classic HID sockets. PSM=0 would allocate a listener. */
    g_free(xml);
    return g_variant_builder_end(&options);
}
static GVariant *call(struct bluez_session *s,const char *path,const char *interface,const char *method,GVariant *args,GError **error)
{
    return g_dbus_connection_call_sync(s->bus,"org.bluez",path,interface,method,args,
                                      NULL,G_DBUS_CALL_FLAGS_NONE,5000,NULL,error);
}
static void owner_changed(GDBusConnection *bus,const char *sender,const char *path,
                          const char *interface,const char *signal,GVariant *args,gpointer data)
{
    (void)bus;(void)sender;(void)path;(void)interface;(void)signal;
    const char *name,*old,*current;
    g_variant_get(args,"(&s&s&s)",&name,&old,&current);
    struct bluez_session *s=data;
    if (!strcmp(name,"org.bluez") && strcmp(current,s->owner)) s->lost=TRUE;
}
static void bus_closed(GDBusConnection *bus,gboolean remote,GError *error,gpointer data)
{
    (void)bus;(void)remote;(void)error;
    ((struct bluez_session *)data)->lost=TRUE;
}
static const char profile_xml[]=
    "<node><interface name='org.bluez.Profile1'>"
    "<method name='Release'/>"
    "<method name='NewConnection'><arg type='o' direction='in'/><arg type='h' direction='in'/><arg type='a{sv}' direction='in'/></method>"
    "<method name='RequestDisconnection'><arg type='o' direction='in'/></method>"
    "</interface></node>";
static void profile_method(GDBusConnection *bus,const char *sender,const char *path,
                           const char *interface,const char *method,GVariant *args,
                           GDBusMethodInvocation *invocation,gpointer data)
{
    (void)bus;(void)path;(void)interface;(void)args;
    struct bluez_session *s=data;
    if (g_strcmp0(sender,s->owner)) {
        g_dbus_method_invocation_return_dbus_error(invocation,"org.bluez.Error.Rejected","Only bluetoothd may call this profile");
    } else if (!strcmp(method,"Release")) {
        s->registered=FALSE; s->lost=TRUE;
        g_dbus_method_invocation_return_value(invocation,NULL);
    } else if (!strcmp(method,"RequestDisconnection")) {
        s->disconnect=TRUE;
        g_dbus_method_invocation_return_value(invocation,NULL);
    } else {
        g_dbus_method_invocation_return_dbus_error(invocation,"org.bluez.Error.Rejected","HID sockets are owned by hidclient");
    }
}
static const GDBusInterfaceVTable profile_vtable={.method_call=profile_method};
static int setup_session(struct bluez_session *s,const char *adapter,bdaddr_t *address)
{
    GError *error=NULL;
    g_dbus_connection_set_exit_on_close(s->bus,FALSE);
    GVariant *reply=g_dbus_connection_call_sync(s->bus,"org.freedesktop.DBus","/org/freedesktop/DBus",
        "org.freedesktop.DBus","GetNameOwner",g_variant_new("(s)","org.bluez"),G_VARIANT_TYPE("(s)"),0,5000,NULL,&error);
    if (!reply) goto fail;
    g_variant_get(reply,"(s)",&s->owner); g_variant_unref(reply);
    s->owner_watch=g_dbus_connection_signal_subscribe(s->bus,"org.freedesktop.DBus","org.freedesktop.DBus",
        "NameOwnerChanged","/org/freedesktop/DBus","org.bluez",0,owner_changed,s,NULL);
    s->closed_handler=g_signal_connect(s->bus,"closed",G_CALLBACK(bus_closed),s);
    reply=call(s,"/","org.freedesktop.DBus.ObjectManager","GetManagedObjects",NULL,&error);
    if (!reply) goto fail;
    GVariant *objects=g_variant_get_child_value(reply,0);
    GVariantIter iterator; g_variant_iter_init(&iterator,objects);
    const char *path; GVariant *interfaces; int found=0;
    char *wanted=adapter ? g_strdup_printf("/org/bluez/%s",adapter) : NULL;
    while (g_variant_iter_next(&iterator,"{&o@a{sa{sv}}}",&path,&interfaces)) {
        GVariant *properties=g_variant_lookup_value(interfaces,"org.bluez.Adapter1",G_VARIANT_TYPE_VARDICT);
        if (properties && (!wanted || !strcmp(path,wanted))) {
            const char *mac=NULL; gboolean powered=FALSE;
            if (g_variant_lookup(properties,"Address","&s",&mac) &&
                g_variant_lookup(properties,"Powered","b",&powered) && powered) {
                ++found;
                g_free(s->adapter_path); s->adapter_path=g_strdup(path);
                if (str2ba(mac,address)) found=0;
            }
        }
        if (properties) g_variant_unref(properties);
        g_variant_unref(interfaces);
    }
    g_free(wanted); g_variant_unref(objects); g_variant_unref(reply);
    if (found!=1) {
        fprintf(stderr,"Select exactly one powered Bluetooth adapter with --adapter hciN (found %d).\n",found);
        return -1;
    }
    return 0;
fail:
    fprintf(stderr,"BlueZ D-Bus: %s\n",error ? error->message : "unavailable");
    g_clear_error(&error); return -1;
}
int bluez_open(struct bluez_session *s,const char *adapter,bdaddr_t *address)
{
    GError *error=NULL;
    char *bus_address=g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SYSTEM,NULL,&error);
    if (bus_address) {
        s->bus=g_dbus_connection_new_for_address_sync(bus_address,
            G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
            NULL,NULL,&error);
        g_free(bus_address);
    }
    if (!s->bus) {
        fprintf(stderr,"System D-Bus: %s\n",error ? error->message : "unavailable");
        g_clear_error(&error); return -1;
    }
    return setup_session(s,adapter,address);
}
int bluez_register(struct bluez_session *s,const char *name,int mode)
{
    GError *error=NULL;
    s->introspection=g_dbus_node_info_new_for_xml(profile_xml,&error);
    if (!s->introspection) goto fail;
    s->object=g_dbus_connection_register_object(s->bus,PROFILE_PATH,s->introspection->interfaces[0],&profile_vtable,s,NULL,&error);
    if (!s->object) goto fail;
    GVariant *options=bluez_profile_options(name,mode);
    if (!options) goto fail;
    GVariant *reply=call(s,"/org/bluez","org.bluez.ProfileManager1","RegisterProfile",
                        g_variant_new("(os@a{sv})",PROFILE_PATH,HID_UUID,options),&error);
    if (!reply) goto fail;
    g_variant_unref(reply); s->registered=TRUE;
    return 0;
fail:
    fprintf(stderr,"HID profile registration: %s\n",error ? error->message : "failed");
    g_clear_error(&error); return -1;
}
void bluez_dispatch(struct bluez_session *s)
{
    (void)s;
    while (g_main_context_iteration(NULL,FALSE)) {}
}
void bluez_close(struct bluez_session *s)
{
    if (s->bus) {
        if (s->registered && !s->lost) {
            GError *error=NULL;
            GVariant *reply=call(s,"/org/bluez","org.bluez.ProfileManager1","UnregisterProfile",g_variant_new("(o)",PROFILE_PATH),&error);
            if (reply) g_variant_unref(reply);
            g_clear_error(&error);
        }
        if (s->object) g_dbus_connection_unregister_object(s->bus,s->object);
        if (s->owner_watch) g_dbus_connection_signal_unsubscribe(s->bus,s->owner_watch);
        if (s->closed_handler) g_signal_handler_disconnect(s->bus,s->closed_handler);
        g_dbus_connection_close_sync(s->bus,NULL,NULL);
        g_object_unref(s->bus);
    }
    if (s->introspection) g_dbus_node_info_unref(s->introspection);
    g_free(s->adapter_path); g_free(s->owner);
    memset(s,0,sizeof(*s));
}
