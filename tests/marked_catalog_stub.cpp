// Existing capture/menu unit tests isolate the new background catalog.
// The dedicated Windows catalog suite links the real worker and SQLite.
#include "../native/ui/marked_catalog.h"
namespace history {
static bool marked_enabled=false,marked_rebuilding=false;
void ConfigureMarkedCatalog(const std::wstring&,bool enabled){marked_enabled=enabled;if(!enabled)marked_rebuilding=false;}
void QueueMarkedFile(const std::wstring&,const std::wstring&){}
bool ToggleMarkedRebuild(){if(!marked_enabled)return false;marked_rebuilding=!marked_rebuilding;return true;}
bool MarkedRebuildRunning(){return marked_rebuilding;}
uint64_t MarkedCatalogRevision(){return 0;}
bool QueueMarkedQuery(const std::string&,const std::vector<std::string>&,void (*)()){return false;}
bool TakeMarkedReply(MarkedQueryReply&){return false;}
void StopMarkedCatalog(){marked_rebuilding=false;}
}
