/* GameSpy CD-key server (compat stand-in, see gqueryreporting.h). Users are always accepted. */
#ifndef PORT_GCDKEYSERVER_H
#define PORT_GCDKEYSERVER_H
#include "gqueryreporting.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef void (*AuthCallBackFn)(int localid, int authenticated, char * errmsg, void * instance);
int  gcd_init_qr(qr_t qrec, int gameid);
void gcd_authenticate_user(int localid, unsigned int userip, const char * challenge, const char * response,
                           AuthCallBackFn authfn, void * instance);
void gcd_disconnect_user(int localid);
void gcd_think(void);
#ifdef __cplusplus
}
#endif
#endif
