/* GameSpy patch/usage tracking (compat stand-in, see gqueryreporting.h): reports nothing. */
#ifndef PORT_GS_PATCH_USAGE_H
#define PORT_GS_PATCH_USAGE_H
#ifdef __cplusplus
extern "C" {
#endif
int ptTrackUsage(int userID, int productID, const char * versionUniqueID, int distributionID, int blocking);
#ifdef __cplusplus
}
#endif
#endif
