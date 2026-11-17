/*
** gamespy_stub.cpp - stand-in for the GameSpy SDK (see compat/GameSpy/gqueryreporting.h).
**
** Server reporting does nothing; CD-key checks always accept (answered from gcd_think, the
** way GameSpy answered asynchronously); MD5 is a real implementation (RFC 1321).
*/
#include "GameSpy/gqueryreporting.h"
#include "GameSpy/gcdkeyserver.h"
#include "GameSpy/gcdkeyclient.h"
#include "GameSpy/gs_md5.h"
#include "GameSpy/ghttp.h"
#include "GameSpy/gs_patch_usage.h"
#include <stdio.h>
#include <string.h>
#include <vector>

/*
** Query & reporting
*/
struct qr_implementation_s { int unused; };
static qr_implementation_s	_QR;
static int						_MasterCount = 0;

extern "C" int qr_init(qr_t * qrec, const char *, int, const char *, const char *,
	qr_querycallback_t, qr_querycallback_t, qr_querycallback_t, qr_querycallback_t, void *)
{
	if (qrec != NULL) {
		*qrec = &_QR;
	}
	return 0;
}

extern "C" void qr_process_queries(qr_t)	{}
extern "C" void qr_send_exiting(qr_t)		{}
extern "C" void qr_shutdown(qr_t)			{}
extern "C" int  get_master_count(void)		{ return _MasterCount; }
extern "C" void clear_master_list(void)		{ _MasterCount = 0; }
extern "C" void add_master(struct sockaddr_in *)	{ _MasterCount++; }

extern "C" int get_sockaddrin(const char * host, int port, struct sockaddr_in * saddr, struct hostent ** savehent)
{
	if (savehent != NULL) {
		*savehent = NULL;
	}
	if (host == NULL || saddr == NULL) {
		return 0;
	}
	memset(saddr, 0, sizeof(*saddr));
	saddr->sin_family = AF_INET;
	saddr->sin_port = htons((unsigned short)port);
	unsigned long addr = inet_addr(host);
	if (addr == INADDR_NONE) {
		struct hostent * h = gethostbyname(host);
		if (h == NULL || h->h_addr_list[0] == NULL) {
			return 0;
		}
		memcpy(&saddr->sin_addr, h->h_addr_list[0], sizeof(saddr->sin_addr));
		if (savehent != NULL) {
			*savehent = h;
		}
	} else {
		saddr->sin_addr.s_addr = addr;
	}
	return 1;
}

extern "C" void ghttpThink(void)	{}
extern "C" int ptTrackUsage(int, int, const char *, int, int)	{ return 0; }

/*
** CD keys
*/
struct PendingAuth
{
	int					LocalId;
	AuthCallBackFn		Callback;
	void *				Instance;
};
static std::vector<PendingAuth>	_Pending;

extern "C" int gcd_init_qr(qr_t, int)	{ return 0; }

extern "C" void gcd_authenticate_user(int localid, unsigned int, const char *, const char *, AuthCallBackFn authfn, void * instance)
{
	PendingAuth p = { localid, authfn, instance };
	_Pending.push_back(p);
}

extern "C" void gcd_disconnect_user(int localid)
{
	for (size_t i = 0; i < _Pending.size(); i++) {
		if (_Pending[i].LocalId == localid) {
			_Pending.erase(_Pending.begin() + i);
			break;
		}
	}
}

extern "C" void gcd_think(void)
{
	std::vector<PendingAuth> ready;
	ready.swap(_Pending);
	for (size_t i = 0; i < ready.size(); i++) {
		if (ready[i].Callback != NULL) {
			ready[i].Callback(ready[i].LocalId, 1, (char *)"", ready[i].Instance);
		}
	}
}

extern "C" void gcd_compute_response(char * cdkey, const char * challenge, char * response)
{
	// Deterministic, GameSpy-shaped (72 hex characters); nothing checks it any more
	char a[33], b[33];
	char joined[512];
	MD5Digest((unsigned char *)cdkey, (unsigned int)strlen(cdkey), a);
	_snprintf(joined, sizeof(joined), "%s%s", challenge, cdkey);
	joined[sizeof(joined) - 1] = 0;
	MD5Digest((unsigned char *)joined, (unsigned int)strlen(joined), b);
	_snprintf(response, RESPONSE_SIZE, "%s%s%.8s", a, b, a);
	response[RESPONSE_SIZE - 1] = 0;
}

/*
** MD5 (RFC 1321)
*/
static unsigned int Rotl(unsigned int x, int c) { return (x << c) | (x >> (32 - c)); }

extern "C" void MD5Digest(unsigned char * input, unsigned int len, char output[33])
{
	static const unsigned int K[64] = {
		0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
		0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
		0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
		0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
		0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
		0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
		0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
		0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391 };
	static const int R[64] = {
		7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22, 5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
		4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23, 6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21 };

	unsigned int h0 = 0x67452301, h1 = 0xefcdab89, h2 = 0x98badcfe, h3 = 0x10325476;
	size_t padded = ((len + 8) / 64 + 1) * 64;
	std::vector<unsigned char> msg(padded, 0);
	if (len > 0) memcpy(&msg[0], input, len);
	msg[len] = 0x80;
	unsigned long long bits = (unsigned long long)len * 8;
	for (int i = 0; i < 8; i++) msg[padded - 8 + i] = (unsigned char)(bits >> (8 * i));

	for (size_t off = 0; off < padded; off += 64) {
		unsigned int w[16];
		for (int i = 0; i < 16; i++) {
			w[i] = msg[off + i * 4] | (msg[off + i * 4 + 1] << 8) | (msg[off + i * 4 + 2] << 16) | ((unsigned int)msg[off + i * 4 + 3] << 24);
		}
		unsigned int a = h0, b = h1, c = h2, d = h3;
		for (int i = 0; i < 64; i++) {
			unsigned int f;
			int g;
			if (i < 16)      { f = (b & c) | (~b & d); g = i; }
			else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
			else if (i < 48) { f = b ^ c ^ d;          g = (3 * i + 5) % 16; }
			else             { f = c ^ (b | ~d);       g = (7 * i) % 16; }
			unsigned int t = d;
			d = c;
			c = b;
			b = b + Rotl(a + f + K[i] + w[g], R[i]);
			a = t;
		}
		h0 += a; h1 += b; h2 += c; h3 += d;
	}
	unsigned int h[4] = { h0, h1, h2, h3 };
	for (int i = 0; i < 16; i++) {
		sprintf(output + i * 2, "%02x", (h[i / 4] >> (8 * (i % 4))) & 255);
	}
	output[32] = 0;
}
