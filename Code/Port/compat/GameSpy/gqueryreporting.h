/*
** GameSpy query & reporting (compat). The GameSpy SDK isn't part of EA's release and the
** GameSpy master servers closed in 2014, so this is a do-nothing stand-in with the same API
** (Port/compat/gamespy_stub.cpp). Only internet server listing used it.
*/
#ifndef PORT_GQUERYREPORTING_H
#define PORT_GQUERYREPORTING_H
#include <winsock.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct qr_implementation_s * qr_t;
typedef void (*qr_querycallback_t)(char * outbuf, int maxlen, void * userdata);
int  qr_init(qr_t * qrec, const char * ip, int baseport, const char * gamename, const char * secret_key,
             qr_querycallback_t basic, qr_querycallback_t info, qr_querycallback_t rules,
             qr_querycallback_t players, void * userdata);
void qr_process_queries(qr_t qrec);
void qr_send_exiting(qr_t qrec);
void qr_shutdown(qr_t qrec);
int  get_master_count(void);
void clear_master_list(void);
void add_master(struct sockaddr_in * addr);
int  get_sockaddrin(const char * host, int port, struct sockaddr_in * saddr, struct hostent ** savehent);
#ifdef __cplusplus
}
#endif
#endif
