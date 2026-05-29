#include "timer_registry.h"
#include "protocol.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static void gen_timer_id(char out[TIMER_ID_LEN]) {
    // "t" + 6 hex bytes from /dev/urandom or rand()
    unsigned char buf[6];
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        (void)fread(buf, 1, sizeof(buf), f);
        fclose(f);
    } else {
        for (int i = 0; i < 6; i++) buf[i] = (unsigned char)(rand() & 0xFF);
    }
    snprintf(out, TIMER_ID_LEN, "t%02x%02x%02x%02x%02x%02x",
             buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void timer_registry_init(TimerRegistry *r) {
    memset(r->slots, 0, sizeof(r->slots));
    pthread_mutex_init(&r->mutex, NULL);
}

void timer_registry_destroy(TimerRegistry *r) {
    pthread_mutex_destroy(&r->mutex);
}

bool timer_create(TimerRegistry *r, const char *name, double interval, bool repeat,
                  char out_id[TIMER_ID_LEN]) {
    pthread_mutex_lock(&r->mutex);
    for (int i = 0; i < TIMER_MAX; i++) {
        if (!r->slots[i].used) {
            TimerSlot *s = &r->slots[i];
            memset(s, 0, sizeof(*s));
            gen_timer_id(s->id);
            strncpy(s->name, name ? name : "", sizeof(s->name) - 1);
            s->interval = interval > 0.0 ? interval : 0.001;
            s->elapsed  = 0.0;
            s->repeat   = repeat;
            s->used     = true;
            memcpy(out_id, s->id, TIMER_ID_LEN);
            pthread_mutex_unlock(&r->mutex);
            return true;
        }
    }
    pthread_mutex_unlock(&r->mutex);
    return false;
}

bool timer_delete(TimerRegistry *r, const char *id) {
    if (!id) return false;
    pthread_mutex_lock(&r->mutex);
    for (int i = 0; i < TIMER_MAX; i++) {
        if (r->slots[i].used && strcmp(r->slots[i].id, id) == 0) {
            r->slots[i].used = false;
            pthread_mutex_unlock(&r->mutex);
            return true;
        }
    }
    pthread_mutex_unlock(&r->mutex);
    return false;
}

void timer_tick(TimerRegistry *r, double dt,
                void (*fired_cb)(const char *id, const char *name, void *userdata),
                void *userdata) {
    pthread_mutex_lock(&r->mutex);
    for (int i = 0; i < TIMER_MAX; i++) {
        TimerSlot *s = &r->slots[i];
        if (!s->used) continue;
        s->elapsed += dt;
        while (s->elapsed >= s->interval) {
            s->elapsed -= s->interval;
            // Copy id/name before potentially deleting slot
            char id[TIMER_ID_LEN];
            char name[64];
            memcpy(id, s->id, TIMER_ID_LEN);
            strncpy(name, s->name, sizeof(name) - 1);
            name[sizeof(name) - 1] = '\0';
            if (!s->repeat) {
                s->used = false;
                pthread_mutex_unlock(&r->mutex);
                fired_cb(id, name, userdata);
                pthread_mutex_lock(&r->mutex);
                break;  // slot is gone
            }
            pthread_mutex_unlock(&r->mutex);
            fired_cb(id, name, userdata);
            pthread_mutex_lock(&r->mutex);
            // Re-check slot is still active (delete could have been called)
            if (!r->slots[i].used) break;
        }
    }
    pthread_mutex_unlock(&r->mutex);
}

bool tr_handle_cmd(TimerRegistry *r, const ParsedCmd *p, int conn_fd) {
    const char *name = p->cmd;
    cJSON      *args = p->args;

    if (strcmp(name, "TimerCreate") == 0 || strcmp(name, "TimerOnce") == 0) {
        bool   repeat   = (strcmp(name, "TimerCreate") == 0);
        double interval = 1.0;
        cJSON *iv = cJSON_GetObjectItemCaseSensitive(args,
                        repeat ? "interval" : "delay");
        if (cJSON_IsNumber(iv)) interval = iv->valuedouble;

        cJSON *nm = cJSON_GetObjectItemCaseSensitive(args, "name");
        const char *tname = (cJSON_IsString(nm) && nm->valuestring) ? nm->valuestring : "";

        char id[TIMER_ID_LEN];
        if (!timer_create(r, tname, interval, repeat, id)) {
            char *err = protocol_error(p->id, "timer registry full");
            protocol_send(conn_fd, err);
            free(err);
            return true;
        }
        cJSON *result = cJSON_CreateObject();
        cJSON_AddStringToObject(result, "timerId", id);
        char *resp = protocol_ok(p->id, result);
        cJSON_Delete(result);
        protocol_send(conn_fd, resp);
        free(resp);
        return true;
    }

    if (strcmp(name, "TimerDelete") == 0) {
        cJSON *tid = cJSON_GetObjectItemCaseSensitive(args, "timerId");
        const char *id_str = (cJSON_IsString(tid) && tid->valuestring) ? tid->valuestring : "";
        bool ok = timer_delete(r, id_str);
        if (p->id) {
            char *resp = ok ? protocol_ok(p->id, NULL)
                            : protocol_error(p->id, "timer not found");
            protocol_send(conn_fd, resp);
            free(resp);
        }
        return true;
    }

    if (strcmp(name, "ListTimers") == 0) {
        cJSON *arr    = timer_list_json(r);
        cJSON *result = cJSON_CreateObject();
        cJSON_AddItemToObject(result, "timers", arr);
        char *resp = protocol_ok(p->id, result);
        cJSON_Delete(result);
        protocol_send(conn_fd, resp);
        free(resp);
        return true;
    }

    return false;
}

cJSON *timer_list_json(TimerRegistry *r) {
    pthread_mutex_lock(&r->mutex);
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < TIMER_MAX; i++) {
        TimerSlot *s = &r->slots[i];
        if (!s->used) continue;
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "timerId",  s->id);
        cJSON_AddStringToObject(obj, "name",     s->name);
        cJSON_AddNumberToObject(obj, "interval", s->interval);
        cJSON_AddItemToObject(obj, "repeat", cJSON_CreateBool(s->repeat));
        cJSON_AddNumberToObject(obj, "elapsed",  s->elapsed);
        cJSON_AddItemToArray(arr, obj);
    }
    pthread_mutex_unlock(&r->mutex);
    return arr;
}
