// Offline Command & Conquer music. Included only by gdcnc.cpp.
// Uses the native music channel/volume and an independent shuffle RNG.
#include "wwaudio.h"
#include "AudibleSound.h"
#include "assets.h"
#include "timemgr.h"
#include <vector>
#include <windows.h>

static cGameDataCnc *_CommandMusicOwner=NULL;
static AudibleSoundClass *_CommandMusicSound=NULL;
static std::vector<StringClass> _CommandMusicTracks;
static std::vector<int> _CommandMusicBag;
static StringClass _CommandMusicLast;
static unsigned _CommandMusicRandom=0;
static unsigned _CommandMusicNext=0;
static bool _CommandMusicEnabled=true;
static float _CommandMusicRetry=0;

static const char *const _CommandMusicDefaults[]={
    "01-command&conquer.mp3", "02-packing iron.mp3", "03-ammoclip.mp3",
    "04-ambient industrial.mp3", "05-moveit.mp3", "06-onyourfeet.mp3",
    "07-got a present for ya.mp3", "08-sniper.mp3", "09-sneakattack.mp3",
    "10-stomp.mp3", "11-ambient beach.mp3", "defunkt.mp3",
    "in the line of fire.mp3", "mechmansrevenge.mp3", "renegadejungle.mp3",
    "stopthemagain.mp3", "raveshaw_act on instinct.mp3", "sakura battle theme.mp3"
};

static unsigned Command_Music_Random()
{
    if(!_CommandMusicRandom)_CommandMusicRandom=GetTickCount()^0x9e3779b9u;
    _CommandMusicRandom=_CommandMusicRandom*1664525u+1013904223u;
    return _CommandMusicRandom;
}

static void Command_Music_Shuffle()
{
    _CommandMusicBag.clear();
    for(unsigned i=0;i<_CommandMusicTracks.size();++i)_CommandMusicBag.push_back(i);
    for(int i=(int)_CommandMusicBag.size()-1;i>0;--i){
        int j=Command_Music_Random()%(i+1),saved=_CommandMusicBag[i];
        _CommandMusicBag[i]=_CommandMusicBag[j];_CommandMusicBag[j]=saved;
    }
    // Avoid repeating the last track at the boundary of two complete shuffles.
    if(_CommandMusicBag.size()>1 && !stricmp(_CommandMusicTracks[_CommandMusicBag[0]],_CommandMusicLast)){
        int saved=_CommandMusicBag[0];_CommandMusicBag[0]=_CommandMusicBag[1];_CommandMusicBag[1]=saved;
    }
    _CommandMusicNext=0;
}

static void Command_Music_Load()
{
    _CommandMusicTracks.clear();_CommandMusicBag.clear();_CommandMusicNext=0;
    INIClass *ini=Get_INI("command_music.ini");
    _CommandMusicEnabled=!ini || ini->Get_Bool("Music","Enabled",true);
    int defaults=sizeof(_CommandMusicDefaults)/sizeof(_CommandMusicDefaults[0]);
    int count=ini?ini->Get_Int("Music","TrackCount",defaults):defaults;
    if(count<0)count=0;if(count>128)count=128;
    for(int i=0;i<count;++i){
        char key[16],name[256];_snprintf(key,sizeof(key),"Track%d",i+1);
        const char *fallback=i<defaults?_CommandMusicDefaults[i]:"";
        if(ini)ini->Get_String("Tracks",key,fallback,name,sizeof(name));else lstrcpynA(name,fallback,sizeof(name));
        if(!name[0])continue;bool duplicate=false;
        for(unsigned j=0;j<_CommandMusicTracks.size();++j)if(!stricmp(_CommandMusicTracks[j],name)){duplicate=true;break;}
        if(!duplicate)_CommandMusicTracks.push_back(StringClass(name));
    }
    if(ini)Release_INI(ini);Command_Music_Shuffle();
}

static void Command_Music_Release_Track()
{
    if(!_CommandMusicSound)return;
    WWAudioClass *audio=WWAudioClass::Get_Instance();
    // Release only this playlist's sound; a newer menu/cinematic track may own the channel.
    if(audio && audio->Peek_Background_Music()==_CommandMusicSound)audio->Set_Background_Music(NULL);
    else _CommandMusicSound->Stop();
    REF_PTR_RELEASE(_CommandMusicSound);
}

static void Command_Music_Stop(cGameDataCnc *game)
{
    if(_CommandMusicOwner!=game)return;
    Command_Music_Release_Track();_CommandMusicOwner=NULL;
    _CommandMusicTracks.clear();_CommandMusicBag.clear();_CommandMusicRetry=0;
}

static void Command_Music_Update(cGameDataCnc *game,bool active)
{
    if(!active){Command_Music_Stop(game);return;}
    WWAudioClass *audio=WWAudioClass::Get_Instance();if(!audio)return;
    if(_CommandMusicOwner!=game){
        if(_CommandMusicOwner)Command_Music_Stop(_CommandMusicOwner);
        _CommandMusicOwner=game;Command_Music_Load();_CommandMusicRetry=0;
    }
    if(!_CommandMusicEnabled || _CommandMusicTracks.empty())return;
    if(_CommandMusicSound && _CommandMusicSound->Get_State()!=AudibleSoundClass::STATE_STOPPED)return;
    _CommandMusicRetry-=TimeManager::Get_Frame_Seconds();if(_CommandMusicRetry>0)return;
    Command_Music_Release_Track();
    if(_CommandMusicNext>=_CommandMusicBag.size())Command_Music_Shuffle();
    const StringClass &track=_CommandMusicTracks[_CommandMusicBag[_CommandMusicNext++]];
    audio->Set_Background_Music(track);_CommandMusicSound=audio->Peek_Background_Music();
    if(_CommandMusicSound){
        _CommandMusicSound->Add_Ref();
        // Set_Background_Music defaults to looping. Restart once to initialize
        // both the native sample and its virtual playback loop counter to one.
        _CommandMusicSound->Stop();_CommandMusicSound->Set_Loop_Count(1);_CommandMusicSound->Play();
        _CommandMusicLast=track;
    }else{
        // A bad/missing custom file cannot cause a per-frame decoder retry storm.
        _CommandMusicRetry=1.0f;
    }
}
