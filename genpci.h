#pragma once

#include <sys/eventfd.h>

class GenPci{
public:
    GenPci(uint16_t b, uint16_t d, uint16_t f);
    ~GenPci();
    void get_pci_config_addr();
    void get_bar0();
    void map_ctrlreg();

private:

    uint16_t busn; 
    uint16_t devn;
    uint16_t funcn;

    uint64_t pci_addr;
    uint64_t bar0;


protected:
    void*   bar0_virt;
    void*   config_virt;
};