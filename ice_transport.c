#include "ice_transport.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void cb_candidate_gathering_done(NiceAgent *agent, guint stream_id, gpointer user_data) {
    ice_transport_t *ice = (ice_transport_t *)user_data;
    g_main_loop_quit(ice->loop);
}

static void cb_component_state_changed(NiceAgent *agent, guint stream_id, guint component_id, guint state, gpointer user_data) {
    ice_transport_t *ice = (ice_transport_t *)user_data;
    if (state == NICE_COMPONENT_STATE_READY) {
        ice->negotiated = TRUE;
    } else if (state == NICE_COMPONENT_STATE_FAILED) {
        ice->negotiated = FALSE;
    }
}

static void cb_nice_recv(NiceAgent *agent, guint stream_id, guint component_id, guint len, gchar *buf, gpointer user_data) {
    ice_transport_t *ice = (ice_transport_t *)user_data;
    g_mutex_lock(&ice->mutex);
    if (ice->recv_buf_len + len <= sizeof(ice->recv_buf)) {
        memcpy(ice->recv_buf + ice->recv_buf_len, buf, len);
        ice->recv_buf_len += len;
    }
    g_mutex_unlock(&ice->mutex);
}

ice_transport_t* ice_transport_create(const char *stun_addr, const char *turn_addr, const char *turn_user, const char *turn_pass) {
    ice_transport_t *ice = calloc(1, sizeof(ice_transport_t));
    ice->loop = g_main_loop_new(NULL, FALSE);
    ice->agent = nice_agent_new(g_main_loop_get_context(ice->loop), NICE_COMPATIBILITY_RFC5245);
    g_mutex_init(&ice->mutex);

    if (stun_addr) {
        g_object_set(ice->agent, "stun-server", stun_addr, NULL);
    }

    if (turn_addr) {
        nice_agent_set_relay_info(ice->agent, 1, 1, turn_addr, 3478, turn_user, turn_pass, NICE_RELAY_TYPE_TURN_UDP);
    }

    ice->stream_id = nice_agent_add_stream(ice->agent, 1);
    ice->component_id = 1;

    g_signal_connect(ice->agent, "candidate-gathering-done", G_CALLBACK(cb_candidate_gathering_done), ice);
    g_signal_connect(ice->agent, "component-state-changed", G_CALLBACK(cb_component_state_changed), ice);
    nice_agent_attach_recv(ice->agent, ice->stream_id, ice->component_id, g_main_loop_get_context(ice->loop), cb_nice_recv, ice);

    return ice;
}

void ice_transport_free(ice_transport_t *ice) {
    if (ice) {
        g_mutex_clear(&ice->mutex);
        g_object_unref(ice->agent);
        g_main_loop_unref(ice->loop);
        free(ice);
    }
}

char* ice_transport_get_local_sdp(ice_transport_t *ice) {
    nice_agent_gather_candidates(ice->agent, ice->stream_id);
    g_main_loop_run(ice->loop);

    gchar *ufrag = NULL;
    gchar *pwd = NULL;
    nice_agent_get_local_credentials(ice->agent, ice->stream_id, &ufrag, &pwd);

    GSList *candidates = nice_agent_get_local_candidates(ice->agent, ice->stream_id, ice->component_id);
    GString *sdp = g_string_new("");
    g_string_append_printf(sdp, "%s %s", ufrag, pwd);

    for (GSList *l = candidates; l; l = l->next) {
        NiceCandidate *c = (NiceCandidate *)l->data;
        gchar ip[INET6_ADDRSTRLEN];
        nice_address_to_string(&c->addr, ip);
        g_string_append_printf(sdp, "|%d,%d,%s,%d", c->type, c->priority, ip, nice_address_get_port(&c->addr));
    }

    g_free(ufrag);
    g_free(pwd);
    g_slist_free_full(candidates, (GDestroyNotify)nice_candidate_free);

    return g_string_free(sdp, FALSE);
}

int ice_transport_negotiate(ice_transport_t *ice, const char *sdp_str) {
    gchar **parts = g_strsplit(sdp_str, "|", -1);
    if (g_strv_length(parts) < 1) return -1;

    gchar **creds = g_strsplit(parts[0], " ", 2);
    if (g_strv_length(creds) < 2) return -1;

    nice_agent_set_remote_credentials(ice->agent, ice->stream_id, creds[0], creds[1]);

    for (int i = 1; parts[i]; i++) {
        gchar **cparts = g_strsplit(parts[i], ",", 4);
        if (g_strv_length(cparts) < 4) {
            g_strfreev(cparts);
            continue;
        }

        NiceCandidate *c = nice_candidate_new(atoi(cparts[0]));
        c->component_id = ice->component_id;
        c->stream_id = ice->stream_id;
        c->priority = atoi(cparts[1]);
        nice_address_set_from_string(&c->addr, cparts[2]);
        nice_address_set_port(&c->addr, atoi(cparts[3]));
        c->transport = NICE_CANDIDATE_TRANSPORT_UDP;

        GSList *l = g_slist_append(NULL, c);
        nice_agent_set_remote_candidates(ice->agent, ice->stream_id, ice->component_id, l);
        g_slist_free(l);
        g_strfreev(cparts);
    }

    g_strfreev(creds);
    g_strfreev(parts);
    return 0;
}
