/* GameSpy CD-key client (compat stand-in, see gqueryreporting.h). */
#ifndef PORT_GCDKEYCLIENT_H
#define PORT_GCDKEYCLIENT_H
#define RESPONSE_SIZE 73
#ifdef __cplusplus
extern "C" {
#endif
void gcd_compute_response(char * cdkey, const char * challenge, char * response);
#ifdef __cplusplus
}
#endif
#endif
