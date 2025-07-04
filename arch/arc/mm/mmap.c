// SPDX-License-Identifier: GPL-2.0-only
/*
 * ARC700 mmap
 *
 * (started from arm version - for VIPT alias handling)
 *
 * Copyright (C) 2013 Synopsys, Inc. (www.synopsys.com)
 */

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/sched/mm.h>

#include <asm/cacheflush.h>

/*
 * Ensure that shared mappings are correctly aligned to
 * avoid aliasing issues with VIPT caches.
 * We need to ensure that
 * a specific page of an object is always mapped at a multiple of
 * SHMLBA bytes.
 */
unsigned long
arch_get_unmapped_area(struct file *filp, unsigned long addr,
		unsigned long len, unsigned long pgoff,
		unsigned long flags, vm_flags_t vm_flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	struct vm_unmapped_area_info info = {};

	/*
	 * We enforce the MAP_FIXED case.
	 */
	if (flags & MAP_FIXED) {
		if (flags & MAP_SHARED &&
		    (addr - (pgoff << PAGE_SHIFT)) & (SHMLBA - 1))
			return -EINVAL;
		return addr;
	}

	if (len > TASK_SIZE)
		return -ENOMEM;

	if (addr) {
		addr = PAGE_ALIGN(addr);

		vma = find_vma(mm, addr);
		if (TASK_SIZE - len >= addr &&
		    (!vma || addr + len <= vm_start_gap(vma)))
			return addr;
	}

	info.length = len;
	info.low_limit = mm->mmap_base;
	info.high_limit = TASK_SIZE;
	info.align_offset = pgoff << PAGE_SHIFT;
	return vm_unmapped_area(&info);
}

static const pgprot_t protection_map[16] = {
	[VM_NONE]					= PAGE_U_NONE,
	[VM_READ]					= PAGE_U_R,
	[VM_WRITE]					= PAGE_U_R,
	[VM_WRITE | VM_READ]				= PAGE_U_R,
	[VM_EXEC]					= PAGE_U_X_R,
	[VM_EXEC | VM_READ]				= PAGE_U_X_R,
	[VM_EXEC | VM_WRITE]				= PAGE_U_X_R,
	[VM_EXEC | VM_WRITE | VM_READ]			= PAGE_U_X_R,
	[VM_SHARED]					= PAGE_U_NONE,
	[VM_SHARED | VM_READ]				= PAGE_U_R,
	[VM_SHARED | VM_WRITE]				= PAGE_U_W_R,
	[VM_SHARED | VM_WRITE | VM_READ]		= PAGE_U_W_R,
	[VM_SHARED | VM_EXEC]				= PAGE_U_X_R,
	[VM_SHARED | VM_EXEC | VM_READ]			= PAGE_U_X_R,
	[VM_SHARED | VM_EXEC | VM_WRITE]		= PAGE_U_X_W_R,
	[VM_SHARED | VM_EXEC | VM_WRITE | VM_READ]	= PAGE_U_X_W_R
};
DECLARE_VM_GET_PAGE_PROT
