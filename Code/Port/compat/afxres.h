/* afxres.h (compat): MFC isn't needed to compile chat.rc; it only wants the standard
   resource definitions and IDC_STATIC. */
#ifndef PORT_AFXRES_H
#define PORT_AFXRES_H
#include <winresrc.h>
#ifndef IDC_STATIC
#define IDC_STATIC (-1)
#endif
#endif
