/* SPDX-License-Identifier: GPL-2.0-only */
/* Protocollo A1 locale: un solo formato, copiato nella build QEMU. */
#ifndef HABUMI_SESSION_H
#define HABUMI_SESSION_H
#include <stdint.h>
#include <string.h>

#define HS_MAGIC UINT32_C(0x31424148)
#define HS_VERSION 1u
#define HS_CAPS 7u
enum { HS_HELLO = 1, HS_VIEWPORT = 2, HS_FOCUS = 3 };
enum { HS_OK, HS_BAD_VERSION, HS_BAD_SESSION, HS_EXPIRED, HS_BAD_COMMAND,
       HS_WINDOW_ERROR };
typedef struct HabumiSessionFrame {
    uint32_t magic, version, op, seq;
    uint32_t token[4];
    int32_t x, y, width, height;
    uint64_t deadline, hwnd;
    uint32_t pid, status, caps, reply;
} HabumiSessionFrame;
_Static_assert(sizeof(HabumiSessionFrame) == 80, "session wire ABI");

static inline uint32_t hs_request_status(const HabumiSessionFrame *r,
                                         const uint32_t token[4], uint64_t now)
{
    if (r->magic != HS_MAGIC || r->version != HS_VERSION) {
        return HS_BAD_VERSION;
    }
    if (memcmp(r->token, token, sizeof(r->token))) {
        return HS_BAD_SESSION;
    }
    if (r->deadline <= now || r->deadline - now > 5000) {
        return HS_EXPIRED;
    }
    if (!r->seq || r->reply || r->op < HS_HELLO || r->op > HS_FOCUS ||
        (r->op == HS_VIEWPORT && (r->x || r->y || r->width < 1 ||
         r->height < 1 || r->width > 16384 || r->height > 16384)) ||
        (r->op == HS_FOCUS && r->x != 0 && r->x != 1)) {
        return HS_BAD_COMMAND;
    }
    return HS_OK;
}

static inline int hs_reply_matches(const HabumiSessionFrame *r,
                                   const HabumiSessionFrame *request,
                                   uint32_t pid)
{
    return r->magic == HS_MAGIC && r->version == HS_VERSION && r->reply == 1 &&
           r->seq == request->seq && r->op == request->op && r->pid == pid &&
           !memcmp(r->token, request->token, sizeof(r->token)) &&
           (r->caps & HS_CAPS) == HS_CAPS;
}
#endif
