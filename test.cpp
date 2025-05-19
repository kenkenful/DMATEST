#include <iostream>
#include <unistd.h>
#include <cstring>
#include <stdlib.h>
#include <cstdint>
#include <sys/ioctl.h>
#include <memory>
#include <pthread.h>
#include "genpci.h"
#include "udmabuf.h"
#include "nvme_defs.h"


class NVMeCtrl: public GenPci{
public:

    NVMeCtrl(uint16_t b, uint16_t d, uint16_t f, std::shared_ptr<DmaAllocator> p): GenPci(b, d, f){
        dma = p;
        ctrl_reg = (nvme_controller_reg_t*)bar0_virt;
        init_ctrl();
		
		kick_polling_thread();
        
       // pthread_mutex_init(&cq_mutex, NULL);

    }

    ~NVMeCtrl(){
        pthread_mutex_destroy(&cq_mutex);
    }

    void* polling_handler(void){

			//printf("cq[0].entry: %p\n", cq[0].entry );
			//printf("cid: %d\n", cq[0].entry->cid );

#if 1
        for (;;) {
			printf("poll\n");

			//nvme_cq_entry_t* pp = &cq[0].entry; 
			//printf("cid: %d\n", pp->cid);
			//printf("phase: %d\n", pp->p);
			//printf("entry: %p\n", &cq[0].entry[0]);
			//printf("entry: %p\n", &cq[0].entry[1]);
			//printf("phase: %d\n", cq[0].phase);

            if (cq[0].entry[cq[0].head].p == cq[0].phase) {
			    while (cq[0].entry[cq[0].head].p == cq[0].phase) {
			        printf("receive response\n");

                    if (++cq[0].head == cq[0].size) {
			        	cq[0].head = 0;
			        	cq[0].phase = !cq[0].phase;
			        }
			        *(volatile u32*)(cq[0].doorbell) = cq[0].head;
			    }
            }
            usleep(1);         
        }
#endif
		return (void*)0;
    }

    static void* polling_handler_wrapper(void* p){
        return ((NVMeCtrl*)p) -> polling_handler();
    }

    void kick_polling_thread(){
        if (pthread_create(&intr_th, NULL, polling_handler_wrapper, this) != 0) {
            perror("pthread create");
            exit(-1);
        }
        if (pthread_detach(intr_th) != 0) {
            perror("pthread detach");
            exit(-1);
        }
    }

    bool wait_ready(){
        int cnt = 0;
        ctrl_reg->cc.en = 1;

        while (ctrl_reg->csts.rdy == 0) {
		    if (cnt++ >  ctrl_reg->cap.to) {
		    	std::cerr << "timeout: controller enable" << std::endl;
		    	return false;
		    }
            usleep(500000);         
        }
        return true;
    }

    bool wait_not_ready(){
        int cnt = 0;
        ctrl_reg->cc.en = 0;
        while (ctrl_reg->csts.rdy == 1) {
		    printf("Waiting  controller disable: %d\n", ctrl_reg->csts.rdy);
		    if (cnt++ > ctrl_reg->cap.to) {
		    	std::cerr << "timeout: controller disable" << std::endl;
		    	return false;
		    }
            usleep(500000);         
        }
        std::cout << "controller is not ready" << std::endl;

        return true;
    }

    void init_adminQ(int cq_depth, int sq_depth){
        nvme_adminq_attr_t	aqa = { 0 };
        sq[0].tail = 0;
	    cq[0].head = 0;
	    cq[0].phase = 1;

	    cq[0].size = cq_depth;
	    sq[0].size = sq_depth;
        
        aqa.acqs = cq[0].size -1;
        aqa.asqs = sq[0].size -1;

        ctrl_reg -> aqa.val = aqa.val;

		int page_num = (sizeof(nvme_cq_entry_t) * cq[0].size + getpagesize() -1)/getpagesize();

		struct dma_mem cq_mem = dma->alloc(getpagesize() * page_num);
        cq[0].entry = (nvme_cq_entry_t*)cq_mem.virt;    
        cq[0].dma_addr = cq_mem.phys;

        if(cq[0].entry == nullptr){
            std::cerr << "cannot allocate admin cq" << std::endl;
            exit(1);
        }

		printf("cq_mem.virt: %p\n", cq_mem.virt);
		printf("cid: %d\n", cq[0].entry->cid);
        
		page_num = (sizeof(nvme_sq_entry_t) * sq[0].size + getpagesize() -1)/getpagesize();

		struct dma_mem sq_mem = dma -> alloc(getpagesize() * page_num);
        sq[0].entry = (nvme_sq_entry_t*)sq_mem.virt;    
        sq[0].dma_addr = sq_mem.phys;
        
        if(sq[0].entry == nullptr){
            std::cerr << "cannot allocate admin sq" << std::endl;
            exit(1);
        }

        ctrl_reg->acq = cq[0].dma_addr;
	    ctrl_reg->asq = sq[0].dma_addr;

        sq[0].doorbell = &ctrl_reg->sq0tdbl[0];
	    cq[0].doorbell = &ctrl_reg->sq0tdbl[0] + (1 << ctrl_reg->cap.dstrd);

    }

    bool init_ctrl(){
        nvme_controller_config_t cc = {0};
        bool ret = true;

        ret = wait_not_ready();

        init_adminQ(64, 64);

        cc.val = NVME_CC_CSS_NVM;
	    cc.val |= 0 << NVME_CC_MPS_SHIFT;
	    cc.val |= NVME_CC_AMS_RR | NVME_CC_SHN_NONE;
	    cc.val |= NVME_CC_IOSQES | NVME_CC_IOCQES;

        ctrl_reg ->cc.val = cc.val;

        ret = wait_ready();
        if(ret == true){
            std::cout << "controller is ready" << std::endl;
        }

        return ret;
    }


    void issueCommand(nvme_sq_entry_t &entry, int entry_no, int data_len){
        int cid = sq[entry_no].tail;

        if(data_len){
            struct dma_mem data_mem = dma -> alloc(data_len);
            
            if(data_mem.virt == nullptr){
                std::cerr << "cannot allocate data buffer" << std::endl;
                exit(-1);
            }
            entry.common.prp1 = data_mem.phys;
        }

        entry.common.command_id = cid;
        memcpy(&sq[entry_no].entry[cid], &entry, sizeof(nvme_sq_entry_t));
        
        if (++sq[entry_no].tail == sq[entry_no].size) sq[entry_no].tail = 0;
		*(volatile u32*)sq[entry_no].doorbell = sq[entry_no].tail;

    }

private:

    std::shared_ptr<DmaAllocator> dma;
    pthread_mutex_t	cq_mutex;

    pthread_t intr_th;
    nvme_controller_reg_t* ctrl_reg;

    typedef struct sq{
        int              size;
	    int              tail;
        uint64_t         dma_addr;
	    nvme_sq_entry_t* entry;
        void*            doorbell;
    }SQ_t;

    typedef struct cq{
        int              size;
        int              head;
    	int              phase;
        uint64_t         dma_addr;
        nvme_cq_entry_t* entry;
        void*            doorbell;    
    }CQ_t;

public:
    SQ_t sq[32];
    CQ_t cq[32];

};


void* polling_handler(void* m){
    NVMeCtrl* p = (NVMeCtrl*)m;
	printf("cq[0].entry: %p\n", p-> cq[0].entry );
	printf("cid: %d\n", p->cq[0].entry->cid );
	return (void*)0;
}

void kick_polling_thread(NVMeCtrl *p){
    pthread_t intr_th;

    printf("kick thread\n");
    if (pthread_create(&intr_th, NULL, polling_handler, p) != 0) {
        perror("pthread create");
        exit(-1);
    }
    if (pthread_detach(intr_th) != 0) {
        perror("pthread detach");
        exit(-1);
    }
}


int main(int argc, char *argv[])
{

	std::shared_ptr<DmaAllocator> p = std::make_shared<DmaAllocator>();

    //NVMeCtrl *nvme = new NVMeCtrl(7,0,0,p);
    //kick_polling_thread(nvme);   

    std::shared_ptr<NVMeCtrl> nvme = std::make_shared<NVMeCtrl>(7,0,0,p);


    sleep(2);

	return 0;
}