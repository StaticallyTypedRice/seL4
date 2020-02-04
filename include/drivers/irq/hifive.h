/*
 * Copyright 2019, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(DATA61_GPL)
 */

#ifndef __DRIVER_IRQ_HIFIVE_H
#define __DRIVER_IRQ_HIFIVE_H

#include <plat/machine/devices_gen.h>
#include <arch/model/smp.h>

/* The memory map is based on the PLIC section in
 * https://static.dev.sifive.com/U54-MC-RVCoreIP.pdf
 */

#define PLIC_PPTR_BASE      (PLIC_PPTR + 0x0C000000)


#define PLIC_HART_ID (CONFIG_FIRST_HART_ID)

#define PLIC_PRIO               0x0
#define PLIC_PRIO_PER_ID        0x4

#define PLIC_PENDING            0x1000
#define PLIC_EN                 0x2000
#define PLIC_EN_PER_HART        0x100
#define PLIC_EN_PER_CONTEXT     0x80


#define PLIC_THRES              0x200000
#define PLIC_SVC_CONTEXT        1
#define PLIC_THRES_PER_HART     0x2000
#define PLIC_THRES_PER_CONTEXT  0x1000
#define PLIC_THRES_CLAIM        0x4

#ifdef CONFIG_PLAT_HIFIVE
/* SiFive U54-MC has 5 cores, and the first core does not
 * have supervisor mode. Therefore, we need to compensate
 * for the addresses.
 */
#define PLIC_NUM_INTERRUPTS 53
#define PLAT_PLIC_THRES_ADJUST(x) ((x) - PLIC_THRES_PER_CONTEXT)
#define PLAT_PLIC_EN_ADJUST(x)    ((x) - PLIC_EN_PER_CONTEXT)

#else

#define PLIC_NUM_INTERRUPTS 511
#define PLAT_PLIC_THRES_ADJUST(x)   (x)
#define PLAT_PLIC_EN_ADJUST(x)      (x)

#endif

typedef uint32_t interrupt_t;

static inline void write_sie(word_t value)
{
    asm volatile("csrw sie,  %0" :: "r"(value));
}

static inline word_t read_sie(void)
{
    word_t temp;
    asm volatile("csrr %0, sie" : "=r"(temp));
    return temp;
}

static inline uint32_t readl(const volatile uint64_t addr)
{
    uint32_t val;
    asm volatile("lw %0, 0(%1)" : "=r"(val) : "r"(addr));
    return val;
}

static inline void writel(uint32_t val, volatile uint64_t addr)
{
    asm volatile("sw %0, 0(%1)" : : "r"(val), "r"(addr));
}

static inline word_t plic_enable_offset(word_t hart_id, word_t context_id)
{
    word_t addr = PLAT_PLIC_EN_ADJUST(PLIC_EN + hart_id * PLIC_EN_PER_HART + context_id * PLIC_EN_PER_CONTEXT);
    return addr;
}


static inline word_t plic_thres_offset(word_t hart_id, word_t context_id)
{
    word_t addr = PLAT_PLIC_THRES_ADJUST(PLIC_THRES + hart_id * PLIC_THRES_PER_HART + context_id * PLIC_THRES_PER_CONTEXT);
    return addr;
}

static inline word_t plic_claim_offset(word_t hart_id, word_t context_id)
{
    word_t addr = plic_thres_offset(hart_id, context_id) + PLIC_THRES_CLAIM;
    return addr;
}

static inline bool_t plic_pending_interrupt(word_t interrupt)
{
    word_t addr = PLIC_PPTR_BASE + PLIC_PENDING + (interrupt / 32) * 4;
    word_t bit = interrupt % 32;
    if (readl(addr) & BIT(bit)) {
        return true;
    } else {
        return false;
    }
}

static inline word_t get_hart_id(void)
{
#ifdef ENABLE_SMP_SUPPORT
    return cpuIndexToID(getCurrentCPUIndex());
#else
    return CONFIG_FIRST_HART_ID;
#endif
}

static inline interrupt_t plic_get_claim(void)
{
    /* Read the claim register for our HART interrupt context */
    word_t hart_id = get_hart_id();
    return readl(PLIC_PPTR_BASE + plic_claim_offset(hart_id, PLIC_SVC_CONTEXT));
}

static inline void plic_complete_claim(interrupt_t irq)
{
    /* Complete the IRQ claim by writing back to the claim register. */
    word_t hart_id = get_hart_id();
    writel(irq, PLIC_PPTR_BASE + plic_claim_offset(hart_id, PLIC_SVC_CONTEXT));
}

static inline void plic_mask_irq(bool_t disable, interrupt_t irq)
{
    uint64_t addr = 0;
    uint32_t val = 0;
    uint32_t bit = 0;

    word_t hart_id = get_hart_id();
    addr = PLIC_PPTR_BASE + plic_enable_offset(hart_id, PLIC_SVC_CONTEXT) + (irq / 32) * 4;
    bit = irq % 32;

    val = readl(addr);
    if (disable) {
        val &= ~BIT(bit);
    } else {
        val |= BIT(bit);
    }
    writel(val, addr);
}

static inline void plic_init_hart(void)
{

    word_t hart_id = get_hart_id();

    for (int i = 1; i <= PLIC_NUM_INTERRUPTS; i++) {
        /* Disable interrupts */
        plic_mask_irq(true, i);
    }

    /* Set threshold to zero */
    writel(0, (PLIC_PPTR_BASE + plic_thres_offset(hart_id, PLIC_SVC_CONTEXT)));
}

static inline void plic_init_controller(void)
{

    for (int i = 1; i <= PLIC_NUM_INTERRUPTS; i++) {
        /* Clear all pending bits */
        if (plic_pending_interrupt(i)) {
            readl(PLIC_PPTR_BASE + plic_claim_offset(PLIC_HART_ID, PLIC_SVC_CONTEXT));
            writel(i, PLIC_PPTR_BASE + plic_claim_offset(PLIC_HART_ID, PLIC_SVC_CONTEXT));
        }
    }

    /* Set the priorities of all interrupts to 1 */
    for (int i = 1; i <= PLIC_MAX_IRQ + 1; i++) {
        writel(2, PLIC_PPTR_BASE + PLIC_PRIO + PLIC_PRIO_PER_ID * i);
    }

}

#endif /* __DRIVER_IRQ_HIFIVE_H */
