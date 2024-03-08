#ifndef NACHOS_VMEM_COREMAP__HH
#define NACHOS_VMEM_COREMAP__HH


#include "lib/bitmap.hh"
#include "userprog/address_space.hh"
#include "machine/mmu.hh"

class Coremap {
public:

    Coremap();

    ~Coremap();
    
    bool InMemory(AddressSpace *addrSpace, TranslationEntry entry);
    
    int Find(AddressSpace *addrSpace, unsigned virtualPage);
    
    void FreePage();
    
    void UpdateVictim();
    
    void UpdateEntry(unsigned physPage);
    
private:

    Bitmap *physPagesMap;
    
    AddressSpace *addrSpaces[NUM_PHYS_PAGES];
    unsigned virtualPages[NUM_PHYS_PAGES];
    
    unsigned victim;
};


#endif
