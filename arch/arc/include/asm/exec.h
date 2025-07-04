/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 */

#ifndef __ASM_ARC_EXEC_H
#define __ASM_ARC_EXEC_H

/* Align to 16b */
#define arch_align_stack(p) ((unsigned long)(p) & ~0xf)

#endif
