// Desktop file dialogs are unavailable in an immersive Android activity.
// The Quest launcher imports the ROM into the application's data directory.
#include <nfd.h>
#include <cstdlib>
extern "C" {
nfdresult_t NFD_Init() { return NFD_OKAY; }
void NFD_Quit() {}
const char* NFD_GetError() { return "Copy pokemonsnap.z64 to the app's external files directory."; }
void NFD_FreePathN(nfdnchar_t* p) { std::free(p); }
void NFD_FreePathU8(nfdu8char_t* p) { std::free(p); }
nfdresult_t NFD_OpenDialogN(nfdnchar_t** p,const nfdnfilteritem_t*,nfdfiltersize_t,const nfdnchar_t*) { *p=nullptr; return NFD_CANCEL; }
nfdresult_t NFD_OpenDialogU8(nfdu8char_t** p,const nfdu8filteritem_t*,nfdfiltersize_t,const nfdu8char_t*) { *p=nullptr; return NFD_CANCEL; }
nfdresult_t NFD_SaveDialogN(nfdnchar_t** p,const nfdnfilteritem_t*,nfdfiltersize_t,const nfdnchar_t*,const nfdnchar_t*) { *p=nullptr; return NFD_CANCEL; }
nfdresult_t NFD_SaveDialogU8(nfdu8char_t** p,const nfdu8filteritem_t*,nfdfiltersize_t,const nfdu8char_t*,const nfdu8char_t*) { *p=nullptr; return NFD_CANCEL; }
nfdresult_t NFD_PickFolderN(nfdnchar_t** p,const nfdnchar_t*) { *p=nullptr; return NFD_CANCEL; }
nfdresult_t NFD_PickFolderU8(nfdu8char_t** p,const nfdu8char_t*) { *p=nullptr; return NFD_CANCEL; }
}
