
#include <iostream>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>

#include <sys/eventfd.h>
#include "genpci.h"


GenPci::GenPci(uint16_t b, uint16_t d, uint16_t f){
    busn = b;
    devn = d;
    funcn = f;
    bar0_virt = nullptr;
    config_virt = nullptr;
    get_pci_config_addr();
    get_bar0();
    map_ctrlreg();
  
    printf("pci_addr: %lx\n", pci_addr);
    printf("bar0: %lx\n", bar0);
}

GenPci::~GenPci(){
    if(bar0_virt != nullptr) munmap(bar0_virt, 8192);
    if(config_virt != nullptr) munmap(config_virt, 4096);    
}

void GenPci::get_pci_config_addr(){
    uint32_t buf[60] = {0};
  
    int fd = open("/sys/firmware/acpi/tables/MCFG", O_RDONLY);
    if(fd == -1){
        perror("open in get_pci_config_addr");
        exit(-1);
    }
    int sz = read(fd, buf, 60);
    if(sz != 60){
        perror("read in get_pci_config_addr\n");
        exit(-1);
    }
    close(fd);
    pci_addr = buf[11] + 4096 * ((uint32_t)funcn + 8 * ((uint32_t)devn + 32 * (uint32_t)busn));
}

void GenPci::get_bar0(){
    int fd = open("/dev/mem", O_RDWR | O_DSYNC);
    if(fd == -1){
        perror("open in get_bar0");
        exit(-1);
    }
    config_virt = (uint8_t*)mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, pci_addr);
    if(config_virt == MAP_FAILED){
        perror("mmap in get_bar0");
        close(fd);
    }
    uint32_t bar_lower = *(uint32_t*)((uint8_t*)config_virt + 0x10);
    uint32_t bar_upper = *(uint32_t*)((uint8_t*)config_virt + 0x14);
    bar0 = ((uint64_t)bar_upper << 32) | (bar_lower & 0xfffffff0);
}

void GenPci::map_ctrlreg(){
    int fd = open("/dev/mem", O_RDWR|O_SYNC);    
    if(fd == -1){
        perror("open in map_ctrlreg");
        exit(-1);
    }

    printf("%x\n", bar0);
    bar0_virt =  mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_SHARED, fd, bar0);
    if(bar0_virt == MAP_FAILED){
        perror("mmap in map_ctrlreg");
        close(fd);
        exit(-1);
    }
    close(fd);
}

