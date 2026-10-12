#ifndef HIDCLIENT_BLUEZ_H
#define HIDCLIENT_BLUEZ_H
#include <gio/gio.h>
#include <bluetooth/bluetooth.h>
struct bluez_session {
    GDBusConnection *bus;
    guint object, owner_watch;
    gulong closed_handler;
    GDBusNodeInfo *introspection;
    char *adapter_path, *owner;
    gboolean registered, lost, disconnect;
};
char *bluez_service_record(const char *, int);
GVariant *bluez_profile_options(const char *, int);
int bluez_open(struct bluez_session *, const char *, bdaddr_t *);
int bluez_register(struct bluez_session *, const char *, int);
void bluez_dispatch(struct bluez_session *);
void bluez_close(struct bluez_session *);
#endif
