#include "coremap.hh"
#include "threads/system.hh"
#include "machine/translation_entry.hh"


Coremap::Coremap()
{
    physPagesMap = freePagesMap;
    victim       = 0;
}

Coremap::~Coremap()
{}

bool
Coremap::InMemory(AddressSpace *addrSpace, TranslationEntry entry)
{
    int physPage = entry.physicalPage;
    
    if (addrSpaces[physPage] == addrSpace && virtualPages[physPage] == entry.virtualPage)
        return true;
    
    return false;
}

int
Coremap::Find(AddressSpace *addrSpace, unsigned virtualPage)
{
    int physAddr = physPagesMap->Find();
    ASSERT(physAddr != -1);
    
    addrSpaces[physAddr]   = addrSpace;
    virtualPages[physAddr] = virtualPage;
    
    return physAddr;
}

void
Coremap::FreePage()
{
#ifdef VMEM
    UpdateVictim();
#endif

    physPagesMap->Clear(victim);
    
    AddressSpace *addrSpace = addrSpaces[victim];
    unsigned virtualPage    = virtualPages[victim];
    
    addrSpace->SaveToSwap(virtualPage);
}

#ifdef VMEM
void
Coremap::UpdateVictim()
{
    victim = (victim + 1) % NUM_PHYS_PAGES;
    
    unsigned virtualPage    = virtualPages[victim];
    TranslationEntry *entry = addrSpaces[victim]->GetPage(virtualPage);
    
    while (entry->use == true) {
        entry->use  = false;
        victim      = (victim + 1) % NUM_PHYS_PAGES;
        virtualPage = virtualPages[victim];
        entry       = addrSpaces[victim]->GetPage(virtualPage);
    }
}
#endif

void
Coremap::UpdateEntry(unsigned physPage)
{
    TranslationEntry *entry = addrSpaces[physPage]->GetPage(virtualPages[physPage]);
    
    if (InMemory(addrSpaces[physPage], *entry))
        entry->dirty = true;
}
