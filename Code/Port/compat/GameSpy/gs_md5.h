/* MD5 as GameSpy's SDK exposed it (compat, real MD5 in Port/compat/gamespy_stub.cpp). */
#ifndef PORT_GS_MD5_H
#define PORT_GS_MD5_H
#ifdef __cplusplus
extern "C" {
#endif
void MD5Digest(unsigned char * input, unsigned int len, char output[33]);	/* lower-case hex */
#ifdef __cplusplus
}
#endif
#endif
