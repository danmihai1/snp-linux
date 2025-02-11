/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_MSHYPER_H
#define _ASM_X86_MSHYPER_H

#include <linux/types.h>
#include <linux/nmi.h>
#include <linux/msi.h>
#include <asm/io.h>
#include <asm/hyperv-tlfs.h>
#include <asm/nospec-branch.h>
#include <asm/paravirt.h>
#include <asm/mshyperv.h>

union hv_ghcb;

DECLARE_STATIC_KEY_FALSE(isolation_type_snp);

typedef int (*hyperv_fill_flush_list_func)(
		struct hv_guest_mapping_flush_list *flush,
		void *data);

#define hv_get_raw_timer() rdtsc_ordered()

void hyperv_vector_handler(struct pt_regs *regs);

#if IS_ENABLED(CONFIG_HYPERV)
extern int hyperv_init_cpuhp;

extern void *hv_hypercall_pg;

extern u64 hv_current_partition_id;

extern union hv_ghcb * __percpu *hv_ghcb_pg;

int hv_call_deposit_pages(int node, u64 partition_id, u32 num_pages);
int hv_call_add_logical_proc(int node, u32 lp_index, u32 acpi_id);
int hv_call_create_vp(int node, u64 partition_id, u32 vp_index, u32 flags);

static inline u64 hv_do_hypercall(u64 control, void *input, void *output)
{
	u64 input_address = input ? virt_to_phys(input) : 0;
	u64 output_address = output ? virt_to_phys(output) : 0;
	u64 hv_status;

#ifdef CONFIG_X86_64
	if (!hv_hypercall_pg)
		return U64_MAX;

	__asm__ __volatile__("mov %4, %%r8\n"
			     CALL_NOSPEC
			     : "=a" (hv_status), ASM_CALL_CONSTRAINT,
			       "+c" (control), "+d" (input_address)
			     :  "r" (output_address),
				THUNK_TARGET(hv_hypercall_pg)
			     : "cc", "memory", "r8", "r9", "r10", "r11");
#else
	u32 input_address_hi = upper_32_bits(input_address);
	u32 input_address_lo = lower_32_bits(input_address);
	u32 output_address_hi = upper_32_bits(output_address);
	u32 output_address_lo = lower_32_bits(output_address);

	if (!hv_hypercall_pg)
		return U64_MAX;

	__asm__ __volatile__(CALL_NOSPEC
			     : "=A" (hv_status),
			       "+c" (input_address_lo), ASM_CALL_CONSTRAINT
			     : "A" (control),
			       "b" (input_address_hi),
			       "D"(output_address_hi), "S"(output_address_lo),
			       THUNK_TARGET(hv_hypercall_pg)
			     : "cc", "memory");
#endif /* !x86_64 */
	return hv_status;
}

/* Fast hypercall with 8 bytes of input and no output */
static inline u64 hv_do_fast_hypercall8(u16 code, u64 input1)
{
	u64 hv_status, control = (u64)code | HV_HYPERCALL_FAST_BIT;

#ifdef CONFIG_X86_64
	{
		__asm__ __volatile__(CALL_NOSPEC
				     : "=a" (hv_status), ASM_CALL_CONSTRAINT,
				       "+c" (control), "+d" (input1)
				     : THUNK_TARGET(hv_hypercall_pg)
				     : "cc", "r8", "r9", "r10", "r11");
	}
#else
	{
		u32 input1_hi = upper_32_bits(input1);
		u32 input1_lo = lower_32_bits(input1);

		__asm__ __volatile__ (CALL_NOSPEC
				      : "=A"(hv_status),
					"+c"(input1_lo),
					ASM_CALL_CONSTRAINT
				      :	"A" (control),
					"b" (input1_hi),
					THUNK_TARGET(hv_hypercall_pg)
				      : "cc", "edi", "esi");
	}
#endif
		return hv_status;
}

/* Fast hypercall with 16 bytes of input */
static inline u64 hv_do_fast_hypercall16(u16 code, u64 input1, u64 input2)
{
	u64 hv_status, control = (u64)code | HV_HYPERCALL_FAST_BIT;

#ifdef CONFIG_X86_64
	{
		__asm__ __volatile__("mov %4, %%r8\n"
				     CALL_NOSPEC
				     : "=a" (hv_status), ASM_CALL_CONSTRAINT,
				       "+c" (control), "+d" (input1)
				     : "r" (input2),
				       THUNK_TARGET(hv_hypercall_pg)
				     : "cc", "r8", "r9", "r10", "r11");
	}
#else
	{
		u32 input1_hi = upper_32_bits(input1);
		u32 input1_lo = lower_32_bits(input1);
		u32 input2_hi = upper_32_bits(input2);
		u32 input2_lo = lower_32_bits(input2);

		__asm__ __volatile__ (CALL_NOSPEC
				      : "=A"(hv_status),
					"+c"(input1_lo), ASM_CALL_CONSTRAINT
				      :	"A" (control), "b" (input1_hi),
					"D"(input2_hi), "S"(input2_lo),
					THUNK_TARGET(hv_hypercall_pg)
				      : "cc");
	}
#endif
	return hv_status;
}

extern struct hv_vp_assist_page **hv_vp_assist_page;

static inline struct hv_vp_assist_page *hv_get_vp_assist_page(unsigned int cpu)
{
	if (!hv_vp_assist_page)
		return NULL;

	return hv_vp_assist_page[cpu];
}

void __init hyperv_init(void);
void hyperv_setup_mmu_ops(void);
void set_hv_tscchange_cb(void (*cb)(void));
void clear_hv_tscchange_cb(void);
void hyperv_stop_tsc_emulation(void);
int hyperv_flush_guest_mapping(u64 as);
int hyperv_flush_guest_mapping_range(u64 as,
		hyperv_fill_flush_list_func fill_func, void *data);
int hyperv_fill_flush_guest_mapping_list(
		struct hv_guest_mapping_flush_list *flush,
		u64 start_gfn, u64 end_gfn);

#ifdef CONFIG_X86_64
void hv_apic_init(void);
void __init hv_init_spinlocks(void);
bool hv_vcpu_is_preempted(int vcpu);
#else
static inline void hv_apic_init(void) {}
#endif

struct irq_domain *hv_create_pci_msi_domain(void);

int hv_map_ioapic_interrupt(int ioapic_id, bool level, int vcpu, int vector,
		struct hv_interrupt_entry *entry);
int hv_unmap_ioapic_interrupt(int ioapic_id, struct hv_interrupt_entry *entry);
int hv_set_mem_host_visibility(unsigned long addr, int numpages, bool visible);

#ifdef CONFIG_AMD_MEM_ENCRYPT
void hv_ghcb_msr_write(u64 msr, u64 value);
void hv_ghcb_msr_read(u64 msr, u64 *value);
bool hv_ghcb_negotiate_protocol(void);
void hv_ghcb_terminate(unsigned int set, unsigned int reason);
#else
static inline void hv_ghcb_msr_write(u64 msr, u64 value) {}
static inline void hv_ghcb_msr_read(u64 msr, u64 *value) {}
static inline bool hv_ghcb_negotiate_protocol(void) { return false; }
static inline void hv_ghcb_terminate(unsigned int set, unsigned int reason) {}
#endif

extern bool hv_isolation_type_snp(void);

extern struct resource rmp_res;
bool hv_needs_snp_rmp(void);

static inline bool hv_is_synic_reg(unsigned int reg)
{
	if ((reg >= HV_REGISTER_SCONTROL) &&
	    (reg <= HV_REGISTER_SINT15))
		return true;
	return false;
}

static inline u64 hv_get_register(unsigned int reg)
{
	u64 value;

	if (hv_is_synic_reg(reg) && hv_isolation_type_snp())
		hv_ghcb_msr_read(reg, &value);
	else
		rdmsrl(reg, value);
	return value;
}

static inline void hv_set_register(unsigned int reg, u64 value)
{
	if (hv_is_synic_reg(reg) && hv_isolation_type_snp()) {
		hv_ghcb_msr_write(reg, value);

		/* Write proxy bit via wrmsl instruction */
		if (reg >= HV_REGISTER_SINT0 &&
		    reg <= HV_REGISTER_SINT15)
			wrmsrl(reg, value | 1 << 20);
	} else {
		wrmsrl(reg, value);
	}
}

#else /* CONFIG_HYPERV */
static inline void hyperv_init(void) {}
static inline void hyperv_setup_mmu_ops(void) {}
static inline void set_hv_tscchange_cb(void (*cb)(void)) {}
static inline void clear_hv_tscchange_cb(void) {}
static inline void hyperv_stop_tsc_emulation(void) {};
static inline struct hv_vp_assist_page *hv_get_vp_assist_page(unsigned int cpu)
{
	return NULL;
}
static inline int hyperv_flush_guest_mapping(u64 as) { return -1; }
static inline int hyperv_flush_guest_mapping_range(u64 as,
		hyperv_fill_flush_list_func fill_func, void *data)
{
	return -1;
}
static inline void hv_set_register(unsigned int reg, u64 value) { }
static inline u64 hv_get_register(unsigned int reg) { return 0; }
static inline int hv_set_mem_host_visibility(unsigned long addr, int numpages,
					     bool visible)
{
	return -1;
}
#endif /* CONFIG_HYPERV */


#include <asm-generic/mshyperv.h>

#endif
