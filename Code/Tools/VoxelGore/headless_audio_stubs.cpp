// The MeshModel vtable links animation sound references even though this
// CPU-only tool never evaluates sound/starts WW3D. No Miles DLL or audio device.
// Do not add this file to the game or any non-offline target.
#include "always.h"
#include "WWAudio.h"
WWAudioClass* WWAudioClass::_theInstance=NULL;
int WWAudioClass::Create_Instant_Sound(int,const Matrix3D&,RefCountClass*,unsigned long,int){return 0;}
