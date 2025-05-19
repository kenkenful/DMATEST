#pragma once

#include <vector>

struct dma_mem{
	void* virt;
	long long phys;
	size_t size;
};

class DmaAllocator{
public:
	DmaAllocator();
	~DmaAllocator();
	struct dma_mem alloc(off64_t sz);

private:
	void create_memfd_with_seals(bool hpage);
	void mmap_fd();
	
	void write_to_memfd(char chr);

private:
	std::vector<struct dma_mem> mem_v;
	std::vector<int> buf_v;
	std::vector<int> memfd_v;

	unsigned int page_size;
	int memfd;
	int devfd;
	void *addr;
	off64_t size;
	int buf;

};
