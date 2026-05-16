#ifndef ICE_TRANSPORT_H
#define ICE_TRANSPORT_H

#include <nice/nice.h>
#include <glib.h>

typedef struct {
    NiceAgent *agent;
    GMainLoop *loop;
    guint stream_id;
    int component_id;
    gboolean negotiated;
    char recv_buf[8192];
    int recv_buf_len;
    GMutex mutex;
} ice_transport_t;

ice_transport_t* ice_transport_create(const char *stun_addr, const char *turn_addr, const char *turn_user, const char *turn_pass);
void ice_transport_free(ice_transport_t *ice);
int ice_transport_negotiate(ice_transport_t *ice, const char *remote_sdp);
char* ice_transport_get_local_sdp(ice_transport_t *ice);

#endif
