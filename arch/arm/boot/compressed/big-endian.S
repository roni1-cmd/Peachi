/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  linux/arch/arm/boot/compressed/big-endian.S
 *
 *  Switch CPU into big endian mode.
 *  Author: Nicolas Pitre
 */

	.section ".start", "ax"

	mrc	p15, 0, r0, c1, c0, 0	@ read control reg
	orr	r0, r0, #(1 << 7)	@ enable big endian mode
	mcr	p15, 0, r0, c1, c0, 0	@ write control reg

