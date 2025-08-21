#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#ifndef BROKER
#define BROKER

#define PDUTYPE_SERVER_REDIR 0x0A
#define TYPE_REDIR 0x0400
#define LB_TARGET_NET_ADDRESS  0x00000001
#define LB_LOAD_BALANCE_INFO   0x00000002

#include <stddef.h>
#include <stdint.h>

struct xrdp_rdp;
struct stream;

int communicate_with_broker(const char* username, char *target)

void hex_dump(const uint8_t* data, size_t len);

void out_string_null_terminated(struct stream *s, const char *str);

void out_unicode_string_null_terminated(struct stream *s, const char *ascii_str);

void out_unicode_string(struct stream *s, const char *ascii_str);

int write_redirect_packet(struct stream *s,
                               const char *target_ip,
                               const char *username,
                               const char *domain);

int broker_redirect(struct xrdp_rdp *self);

int
xrdp_rdp_send_redir(struct xrdp_rdp *self, struct stream *s, int pdu_type);


#endif