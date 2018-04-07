#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"
#include "paging.h"
#include "fs.h"


// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.
static pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;

  pde = &pgdir[PDX(va)];
  if(*pde & PTE_P){
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
  } else {
    if(!alloc || (pgtab = (pte_t*)kalloc()) == 0)
      return 0;
    // Make sure all those PTE_P bits are zero.
    memset(pgtab, 0, PGSIZE);
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table
    // entries, if necessary.
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  return &pgtab[PTX(va)];
}

// Select a page-table entry which is mapped
// but not accessed. Notice that the user memory
// is mapped between 0...KERNBASE.
pte_t*
select_a_victim(pde_t *pgdir)
{
	// cprintf("SELECT A VICTIM");
	for(;;) {
		for(int i = 0; i < NPDENTRIES; i++) {
			if( pgdir[i] & ~(PTE_A) ) {
				return (pte_t*)P2V(PTE_ADDR(pgdir[i]));
			}
		}
		clearaccessbit(pgdir);		
	}
	return 0;
}

// return the disk block-id, if the virtual address
// was swapped, -1 otherwise.
int
getswappedblk(pde_t *pgdir, uint va)
{
	// cprintf("GET SWAPPED BLK");
	pte_t *potential = walkpgdir(pgdir, (char*)va, 0);
	if( *potential & ~(PTE_P) ) {
		// in swap
		return PTE_ADDR(*potential);
	}
	else {
  		return -1;
	}
}



// Clear access bit of a random pte.
void
clearaccessbit(pde_t *pgdir)
{
	// cprintf("CLEAR ACCESS BIT");
	int random_no = 10 % NPDENTRIES;
	pde_t *pde = &pgdir[random_no];
	pte_t *random_pte = (pte_t*)P2V(PTE_ADDR(*pde));
	*random_pte &= ~(PTE_A); 
}

/* Allocate eight consecutive disk blocks.
 * Save the content of the physical page in the pte
 * to the disk blocks and save the block-id into the
 * pte.
 */
void
swap_page_from_pte(pte_t *pte)
{
	uint addr = balloc_page(ROOTDEV);
	uint ppn = PTE_ADDR(*pte);	
	char *pg = (char *)P2V(ppn);
	asm volatile("invlpg (%0)" ::"r" ((unsigned long)P2V(ppn)) : "memory");
	write_page_to_disk(ROOTDEV, pg, addr);
	*pte &= ~(PTE_P);
	*pte = addr; ///> Check if it makes sense -> reserve some bit? WHAT?
	kfree(pg);
}

/* Select a victim and swap the contents to the disk.
 */
pte_t*
swap_page(pde_t *pgdir)
{
	// cprintf("SWAP PAGE");
	pte_t *victim = select_a_victim(pgdir);
	swap_page_from_pte(victim);
	// panic("swap_page is not implemented");
	return victim;
}

/* Map a physical page to the virtual address addr.
 * If the page table entry points to a swapped block
 * restore the content of the page from the swapped
 * block and free the swapped block.
 */
void
map_address(pde_t *pgdir, uint addr)
{
	// cprintf("MAP ADDRESS");
	pte_t *target;
	// int swap_blk;

	if( (target = walkpgdir(pgdir, (char *)addr, 1)) == 0 ) {
		target = swap_page(pgdir);
	} //Recheck for round down !!!
	uint ppn = PTE_ADDR(*target);	
	char *pg = (char *)P2V(ppn);

	// if((swap_blk = getswappedblk(pgdir, addr)) != -1){
	if( (*target & ~(PTE_P)) ){
		read_page_from_disk(ROOTDEV, pg, *target);
		bfree_page(ROOTDEV, *target);
	}

	// panic("map_address is not implemented");
}

/* page fault handler */
void
handle_pgfault()
{
	// cprintf("HANDLE PGFAULT");
	unsigned addr;
	struct proc *curproc = myproc();

	asm volatile ("movl %%cr2, %0 \n\t" : "=r" (addr));
	addr &= ~0xfff;
	map_address(curproc->pgdir, addr);
}
