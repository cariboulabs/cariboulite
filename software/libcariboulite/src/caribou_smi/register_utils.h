#ifndef __REGISTER_UTILS_H__
#define __REGISTER_UTILS_H__

//==========================================================
// GENERAL DEFS
//==========================================================
#define REG32(m, x) ((volatile uint32_t *)((uint32_t)(m.virt)+(uint32_t)(x)))

// Get bus address of register
#define REG_BUS_ADDR(m, x)  ((uint32_t)(m.bus)  + (uint32_t)(x))

// Convert uncached memory virtual address to bus address
#define MEM_BUS_ADDR(mp, a) ((uint32_t)a-(uint32_t)mp->virt+(uint32_t)mp->bus)

// Convert bus address to physical address (for mmap)
#define BUS_PHYS_ADDR(a)    ((void *)((uint32_t)(a)&~0xC0000000))

// Union of 32-bit value with register bitfields
#define REG_DEF(name, fields) typedef union {struct {volatile uint32_t fields;}; volatile uint32_t value;} name

// VC flags for unchached DMA memory
#define DMA_MEM_FLAGS (MEM_FLAG_COHERENT | MEM_FLAG_ZERO)


// Structure for mapped peripheral or memory
typedef struct 
{
    int fd;             // File descriptor
    int h;              // Memory handle 
    int size;           // Memory size
    void *bus;          // Bus address
    void *virt;         // Virtual address
    void *phys;         // Physical address
} MEM_MAP;


//=====================================================================
// Get virtual memory segment for peripheral regs or physical mem
void* map_segment(void *addr, int size);
void unmap_segment(void *mem, int size);
void* map_periph(MEM_MAP *mp, uint32_t base_addr, int size);
void unmap_periph_mem(MEM_MAP *mp);
void* map_uncached_mem(MEM_MAP *mp, int size);

#endif // __REGISTER_UTILS_H__