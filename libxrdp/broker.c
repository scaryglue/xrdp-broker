#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "broker.h"
#include "string_calls.h"

#include <openssl/rand.h>
#include "libxrdp.h"
#include <curl/curl.h>

void out_string_null_terminated(struct stream *s, const char *str)
{
    while (*str)
        out_uint8(s, *str++);

    out_uint8(s, 0x00);
}

// helper to convert to unicode
void out_unicode_string(struct stream *s, const char *ascii_str)
{
    g_writeln("1 in unicode unicode %s", ascii_str);
    while (*ascii_str)
    {
        out_uint8(s, *ascii_str);     // Low byte
        out_uint8(s, 0x00);           // High byte
        ascii_str++;
    }
}

// with null-terminator
void out_unicode_string_null_terminated(struct stream *s, const char *ascii_str)
{
    out_unicode_string(s, ascii_str);

    out_uint16_le(s, 0x0000);  // null-terminator
}

void hex_dump(const uint8_t* data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (i % 16 == 0)
            g_write("%08zx  ", i);
        g_write("%02x ", data[i]);
        if ((i + 1) % 8 == 0 && (i + 1) % 16 != 0)
            g_write(" ");
        if ((i + 1) % 16 == 0)
            g_write("\n");
    }
    if (len % 16 != 0)
        g_write("\n");
}

int write_redirect_packet(struct stream *s,
                               const char *target_ip,
                               const char *username,
                               const char *domain)
{
    uint16_t flags = 0x0400; // SEC_REDIRECTION_PKT
    uint16_t length = 0;   // placeholder
    uint32_t session_id = 0;
    uint32_t redir_flags = //0x0;
        //0x00000001 | // LB_LOAD_BALANCE_INFO_PRESENT
        0x00000004 | // LB_USERNAME
        //0x00000004 | // LB_DOMAIN
        //0x00000008 | // LB_PASSWORD
        //0x00000010 | // LB_TARGET_FQDN
        //0x00000020 | // LB_TARGET_NETBIOS_NAME
        0x00000001; // LB_TARGET_NET_ADDRESS
        //0x00000080 | // LB_CLIENT_TSV_URL
        //0x00000100 | // LB_SERVER_REDIRECTION_GUID
        //0x00000200;  // LB_TARGET_CERTIFICATE

    // placeholder
    //const char *password = "";
    //const char *fqdn = "";
    //const char *netbios = "";
    //const char *tsv_url = "";
    // Redirection GUID (16 bytes)
    //const uint8_t guid[16] = {
    //   0xde, 0xad, 0xbe, 0xef,
    //   0xba, 0xad,
    //   0xf0, 0x0d,
    //  0xca, 0xfe, 0xba, 0xbe, 0x00, 0x01, 0x02, 0x03
    //};
    //const char *cert = "";

    

    // lengths
    uint32_t target_len = (uint32_t)(g_strlen(target_ip) * 2 + 2);
    //uint32_t lbinfo_len = 0;
    uint32_t user_len   = (uint32_t)(g_strlen(username) * 2 + 2);
    //uint32_t domain_len = (uint32_t)(g_strlen(domain) * 2 + 2);
    //uint32_t pass_len   = (uint32_t)(g_strlen(password) * 2 + 2);
    //uint32_t fqdn_len   = (uint32_t)(g_strlen(fqdn) + 1);
    //uint32_t netbios_len= (uint32_t)(g_strlen(netbios) + 1);
    //uint32_t tsv_len    = (uint32_t)(g_strlen(tsv_url) * 2 + 2);
    //uint32_t guid_len   = 16;
    //uint32_t cert_len   = (uint32_t)(g_strlen(cert));
    //uint32_t addresses_len   = (uint32_t)(4+4+target_len);

    // Padding
    out_uint16_le(s, 0x5f59);

    //size_t start_pos = s->end - s->data;
    length = 12 + 4 + target_len + 4 + user_len;
    g_writeln("size: %d", length);

    // Flags
    out_uint16_le(s, flags);
    //out_uint16_le(s, 0); // Länge, später gefüllt
    out_uint16_le(s, length);
    out_uint32_le(s, session_id);
    out_uint32_le(s, redir_flags);

    // length -> content
    out_uint32_le(s, target_len);
    out_unicode_string_null_terminated(s, target_ip);


    //out_uint32_le(s, lbinfo_len); //is 0 and array of bytes, so no lbinfo needed

    out_uint32_le(s, user_len);
    out_unicode_string_null_terminated(s, username);

    //out_uint32_le(s, domain_len);
    //out_unicode_string_null_terminated(s, domain);

    //out_uint32_le(s, pass_len);
    //out_unicode_string_null_terminated(s, password);

    //out_uint32_le(s, fqdn_len);
    //out_string_null_terminated(s, fqdn);

    //out_uint32_le(s, netbios_len);
    //out_string_null_terminated(s, netbios);

    //out_uint32_le(s, tsv_len);
    //out_unicode_string_null_terminated(s, tsv_url);

    //out_uint32_le(s, guid_len);
    //for (int i = 0; i < 16; i++)
    //   out_uint8(s, guid[i]);

    //out_uint32_le(s, cert_len);
    //out_unicode_string_null_terminated(s, cert);


    //out_uint32_le(s, addresses_len);
    //out_uint32_le(s, 0x1);  //always one address
    //repeat the address?
    //out_uint32_le(s, target_len);
    //out_unicode_string_null_terminated(s, target_ip);

    // Padding (8 bytes 0x00)
    //for (int i = 0; i < 8; i++)
    //   out_uint8(s, 0x00);


    // put in length now
    //length = (uint16_t)((s->end - s->data) - start_pos);
    //s->data[start_pos + 2] = length & 0xFF;
    //s->data[start_pos + 3] = (length >> 8) & 0xFF;

    return 0;
}

/*****************************************************************************/
/* Send a [MS-RDPBCGR] Control PDU with for the given pduType with the headers
   added, we need to add version=0 for the enhanced security server Redirection PDU.  */
int
xrdp_rdp_send_redir(struct xrdp_rdp *self, struct stream *s, int pdu_type)
{
    int len = 0;

    s_pop_layer(s, rdp_hdr);
    len = s->end - s->p;

    /* TS_SHARECONTROLHEADER */
    out_uint16_le(s, len);               /* totalLength */
    out_uint16_le(s, pdu_type);   /* pduType */
    out_uint16_le(s, self->mcs_channel); /* pduSource */
    LOG_DEVEL(LOG_LEVEL_TRACE, "Adding header [MS-RDPBCGR] TS_SHARECONTROLHEADER "
              "totalLength %d, pduType.type %s (%d), pduType.PDUVersion %d, "
              "pduSource %d", len, PDUTYPE_TO_STR(pdu_type & 0xf),
              pdu_type & 0xf, (((0x10 | pdu_type) & 0xfff0) >> 4),
              self->mcs_channel);

    if (xrdp_sec_send(self->sec_layer, s, MCS_GLOBAL_CHANNEL) != 0)
    {
        LOG(LOG_LEVEL_ERROR, "xrdp_rdp_send: xrdp_sec_send failed");
        return 1;
    }

    return 0;
}


int broker_redirect(struct xrdp_rdp *self)
{
    //struct xrdp_mcs *mcs = self->sec_layer->mcs_layer;
    g_writeln("%s", self->client_info.username);
    struct stream *s;
    make_stream(s);
    init_stream(s, 8192);

    xrdp_rdp_init(self, s);
    //xrdp_mcs_init(mcs, s);

    //TS_SECURITY_HEADER
    //out_uint16_le(s, 0x0400); /* flags */
    //out_uint16_le(s, 0); /* flagsHi */



    write_redirect_packet(s,"", self->client_info.username, "");

    s_mark_end(s);

    g_writeln("PDU vor Senden:");
    hex_dump((const uint8_t*)s->data, s->end - s->data);
    LOG(LOG_LEVEL_TRACE, "Sending the Redirection PDU with username %s and Target Address %s", self->client_info.username, "");

    if (xrdp_rdp_send_redir(self, s, PDUTYPE_SERVER_REDIR) != 0)
    {
        free_stream(s);
        LOG(LOG_LEVEL_ERROR, "Sending Redirection PDU failed");
        return 1;
    }
    free_stream(s);

    return 0;
}
