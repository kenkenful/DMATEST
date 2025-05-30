#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/memfd.h>
#include <linux/udmabuf.h>
#include <memory>
#include "dma.h"

#define CURRENT_RBP(x) \
  __asm__("mov %%rbp, %0;" \
            :"=r"(x) \
          );

#define CHECK_NRVO(tag, object) \
{ \
  unsigned long rbp; \
  CURRENT_RBP(rbp); \
  unsigned long address = reinterpret_cast<unsigned long>(&object); \
  if (rbp > address) { \
    std::cout << tag << ": NRVO isn't applied." << std::endl; \
  } \
}


static uintptr_t virt_to_phys(void *virt)
{
	long pagesize = sysconf(_SC_PAGESIZE);
	int fd = open("/proc/self/pagemap", O_RDONLY);

	if (fd == -1)
	{
		printf("failed to open /proc/self/pagemap\n");
		return (uintptr_t)0;
	}

	// ASSERT(fd != -1, "failed to open /proc/self/pagemap");
	off_t ret = lseek(fd, (uintptr_t)virt / pagesize * sizeof(uintptr_t), SEEK_SET);

	if (ret == -1)
	{
		printf("lseek error\n");
		return (uintptr_t)0;
	}

	uintptr_t entry = 0;
	ssize_t rc = read(fd, &entry, sizeof(entry));
	if (rc <= 0)
	{
		printf("read error\n");
		return (uintptr_t)0;
	}

	if (entry == 0)
	{
		printf("failed to get physical address for %p (perhaps forgot sudo?)", virt);
		return (uintptr_t)0;
	}

	// ASSERT(rc > 0, "read error");
	// ASSERT(entry != 0,"failed to get physical address for %p (perhaps forgot sudo?)",virt);
	close(fd);

	return (entry & 0x7fffffffffffffULL) * pagesize + ((uintptr_t)virt) % pagesize;
}


DmaAllocator::DmaAllocator(){
	devfd = open("/dev/udmabuf", O_RDWR);
	if (devfd < 0)
	{
		printf("fail open\n");
		exit(1);
	}
}


DmaAllocator::~DmaAllocator(){
	for(auto &a : buf_v) close(a);
	for(auto &a : mem_v) munmap(a.virt, a.size);
	for(auto &a : memfd_v) close(a);
	close(devfd);
}


struct dma_mem DmaAllocator::alloc(off64_t sz){
	struct udmabuf_create create = {0};
	struct dma_mem mem = {0};
	CHECK_NRVO("mem", mem);
	page_size = getpagesize();
	size = sz;	
	create_memfd_with_seals(false);
	mmap_fd();
	write_to_memfd('X');
	create.memfd = memfd;
	create.offset = 0;
	create.size = size;
	buf = ioctl(devfd, UDMABUF_CREATE, &create);
	if (buf < 0){
		printf("fail %d\n", __LINE__);
		exit(1);
	}else{
		printf("suceess %d\n", __LINE__);
	}
	buf_v.emplace_back(buf);
	printf("phys addr: %lx\n", virt_to_phys(addr));
	mem.size = sz;
	mem.phys = virt_to_phys(addr);
	mem.virt = addr;
	mem_v.emplace_back(mem);
	return mem;
}

void DmaAllocator::create_memfd_with_seals(bool hpage)
{
	int ret;
	unsigned int flags = MFD_ALLOW_SEALING;
	if (hpage)
		flags |= MFD_HUGETLB;
	memfd = memfd_create("udmabuf-test", flags);
	if (memfd < 0)
	{
		printf("fail memfd_create\n");
		exit(1);
	}
	memfd_v.emplace_back(memfd);
	ret = fcntl(memfd, F_ADD_SEALS, F_SEAL_SHRINK);
	if (ret < 0)
	{
		printf("fail fcntl\n");
		exit(1);
	}
	ret = ftruncate(memfd, size);
	if (ret == -1)
	{
		printf("fail ftruncate\n");
		exit(1);
	}
}

void DmaAllocator::mmap_fd()
{
	addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, memfd, 0);
	if (addr == MAP_FAILED)
	{
		printf("fail mmap %d\n", __LINE__);
		exit(1);
	}
}

void DmaAllocator::write_to_memfd(char chr)
{
	int i;
	for (i = 0; i < size / page_size; i++)
	{
		*((char *)addr + (i * page_size)) = chr;
	}
}
