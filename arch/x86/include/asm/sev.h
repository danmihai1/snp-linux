/* SPDX-License-Identifier: GPL-2.0 */
/*
 * AMD Encrypted Register State Support
 *
 * Author: Joerg Roedel <jroedel@suse.de>
 */

#ifndef __ASM_ENCRYPTED_STATE_H
#define __ASM_ENCRYPTED_STATE_H

#include <linux/types.h>
#include <linux/sev.h>
#include <asm/insn.h>
#include <asm/sev-common.h>
#include <asm/bootparam.h>

#define GHCB_PROTOCOL_MIN	1ULL
#define GHCB_PROTOCOL_MAX	2ULL
#define GHCB_DEFAULT_USAGE	0ULL

#define	VMGEXIT()			{ asm volatile("rep; vmmcall\n\r"); }

enum es_result {
	ES_OK,			/* All good */
	ES_UNSUPPORTED,		/* Requested operation not supported */
	ES_VMM_ERROR,		/* Unexpected state from the VMM */
	ES_DECODE_FAILED,	/* Instruction decoding failed */
	ES_EXCEPTION,		/* Instruction caused exception */
	ES_RETRY,		/* Retry instruction emulation */
};

struct es_fault_info {
	unsigned long vector;
	unsigned long error_code;
	unsigned long cr2;
};

struct pt_regs;

/* ES instruction emulation context */
struct es_em_ctxt {
	struct pt_regs *regs;
	struct insn insn;
	struct es_fault_info fi;
};

/*
 * AMD SEV Confidential computing blob structure. The structure is
 * defined in OVMF UEFI firmware header:
 * https://github.com/tianocore/edk2/blob/master/OvmfPkg/Include/Guid/ConfidentialComputingSevSnpBlob.h
 */
#define CC_BLOB_SEV_HDR_MAGIC	0x45444d41
struct cc_blob_sev_info {
	u32 magic;
	u16 version;
	u16 reserved;
	u64 secrets_phys;
	u32 secrets_len;
	u32 rsvd1;
	u64 cpuid_phys;
	u32 cpuid_len;
	u32 rsvd2;
} __packed;

void do_vc_no_ghcb(struct pt_regs *regs, unsigned long exit_code);

static inline u64 lower_bits(u64 val, unsigned int bits)
{
	u64 mask = (1ULL << bits) - 1;

	return (val & mask);
}

struct real_mode_header;
enum stack_type;
struct ghcb;

/* Early IDT entry points for #VC handler */
extern void vc_no_ghcb(void);
extern void vc_boot_ghcb(void);
extern bool handle_vc_boot_ghcb(struct pt_regs *regs);

/* Software defined (when rFlags.CF = 1) */
#define PVALIDATE_FAIL_NOUPDATE		255

#define RMPTABLE_CPU_BOOKKEEPING_SZ     0x4000

/* RMP page size */
#define RMP_PG_SIZE_4K			0
#define RMP_PG_SIZE_2M			1
#define RMP_TO_X86_PG_LEVEL(level)	(((level) == RMP_PG_SIZE_4K) ? PG_LEVEL_4K : PG_LEVEL_2M)
#define X86_TO_RMP_PG_LEVEL(level)	(((level) == PG_LEVEL_4K) ? RMP_PG_SIZE_4K : RMP_PG_SIZE_2M)

/*
 * The RMP entry format is not architectural. The format is defined in PPR
 * Family 19h Model 01h, Rev B1 processor.
 */
struct __packed rmpentry {
	union {
		struct {
			u64	assigned	: 1,
				pagesize	: 1,
				immutable	: 1,
				rsvd1		: 9,
				gpa		: 39,
				asid		: 10,
				vmsa		: 1,
				validated	: 1,
				rsvd2		: 1;
		} info;
		u64 low;
	};
	u64 high;
};

#define rmpentry_assigned(x)	((x)->info.assigned)
#define rmpentry_pagesize(x)	((x)->info.pagesize)
#define rmpentry_vmsa(x)	((x)->info.vmsa)
#define rmpentry_asid(x)	((x)->info.asid)
#define rmpentry_validated(x)	((x)->info.validated)
#define rmpentry_gpa(x)		((unsigned long)(x)->info.gpa)
#define rmpentry_immutable(x)	((x)->info.immutable)

#define RMPADJUST_VMSA_PAGE_BIT		BIT(16)

/* SNP Guest message request */
struct snp_req_data {
	unsigned long req_gpa;
	unsigned long resp_gpa;
	unsigned long data_gpa;
	unsigned int data_npages;
};

struct sev_guest_platform_data {
	u64 secrets_gpa;
};

/*
 * The secrets page contains 96-bytes of reserved field that can be used by
 * the guest OS. The guest OS uses the area to save the message sequence
 * number for each VMPCK.
 *
 * See the GHCB spec section Secret page layout for the format for this area.
 */
struct secrets_os_area {
	u32 msg_seqno_0;
	u32 msg_seqno_1;
	u32 msg_seqno_2;
	u32 msg_seqno_3;
	u64 ap_jump_table_pa;
	u8 rsvd[40];
	u8 guest_usage[32];
} __packed;

#define VMPCK_KEY_LEN		32

/* See the SNP spec version 0.9 for secrets page format */
struct snp_secrets_page_layout {
	u32 version;
	u32 imien	: 1,
	    rsvd1	: 31;
	u32 fms;
	u32 rsvd2;
	u8 gosvw[16];
	u8 vmpck0[VMPCK_KEY_LEN];
	u8 vmpck1[VMPCK_KEY_LEN];
	u8 vmpck2[VMPCK_KEY_LEN];
	u8 vmpck3[VMPCK_KEY_LEN];
	struct secrets_os_area os_area;
	u8 rsvd3[3840];
} __packed;

struct rmpupdate {
	u64 gpa;
	u8 assigned;
	u8 pagesize;
	u8 immutable;
	u8 rsvd;
	u32 asid;
} __packed;

#ifdef CONFIG_AMD_MEM_ENCRYPT
extern struct static_key_false sev_es_enable_key;
extern void __sev_es_ist_enter(struct pt_regs *regs);
extern void __sev_es_ist_exit(void);
static __always_inline void sev_es_ist_enter(struct pt_regs *regs)
{
	if (static_branch_unlikely(&sev_es_enable_key))
		__sev_es_ist_enter(regs);
}
static __always_inline void sev_es_ist_exit(void)
{
	if (static_branch_unlikely(&sev_es_enable_key))
		__sev_es_ist_exit();
}
extern int sev_es_setup_ap_jump_table(struct real_mode_header *rmh);
extern void __sev_es_nmi_complete(void);
static __always_inline void sev_es_nmi_complete(void)
{
	if (static_branch_unlikely(&sev_es_enable_key))
		__sev_es_nmi_complete();
}
extern int __init sev_es_efi_map_ghcbs(pgd_t *pgd);
extern enum es_result sev_es_ghcb_hv_call(struct ghcb *ghcb,
					  bool set_ghcb_msr,
					  struct es_em_ctxt *ctxt,
					  u64 exit_code, u64 exit_info_1,
					  u64 exit_info_2);
static inline int rmpadjust(unsigned long vaddr, bool rmp_psize, unsigned long attrs)
{
	int rc;

	/* "rmpadjust" mnemonic support in binutils 2.36 and newer */
	asm volatile(".byte 0xF3,0x0F,0x01,0xFE\n\t"
		     : "=a"(rc)
		     : "a"(vaddr), "c"(rmp_psize), "d"(attrs)
		     : "memory", "cc");

	return rc;
}
static inline int pvalidate(unsigned long vaddr, bool rmp_psize, bool validate)
{
	bool no_rmpupdate;
	int rc;

	/* "pvalidate" mnemonic support in binutils 2.36 and newer */
	asm volatile(".byte 0xF2, 0x0F, 0x01, 0xFF\n\t"
		     CC_SET(c)
		     : CC_OUT(c) (no_rmpupdate), "=a"(rc)
		     : "a"(vaddr), "c"(rmp_psize), "d"(validate)
		     : "memory", "cc");

	if (no_rmpupdate)
		return PVALIDATE_FAIL_NOUPDATE;

	return rc;
}
void setup_ghcb(void);
void __init early_snp_set_memory_private(unsigned long vaddr, unsigned long paddr,
					 unsigned int npages);
void __init early_snp_set_memory_shared(unsigned long vaddr, unsigned long paddr,
					unsigned int npages);
void __init snp_prep_memory(unsigned long paddr, unsigned int sz, enum psc_op op);
void snp_set_memory_shared(unsigned long vaddr, unsigned int npages);
void snp_set_memory_private(unsigned long vaddr, unsigned int npages);
void snp_set_wakeup_secondary_cpu(void);
bool snp_init(struct boot_params *bp);
void snp_abort(void);
int snp_issue_guest_request(u64 exit_code, struct snp_req_data *input, unsigned long *fw_err);
bool snp_soft_rmptable(void);
void __init snp_set_soft_rmptable(void);
#else
static inline void sev_es_ist_enter(struct pt_regs *regs) { }
static inline void sev_es_ist_exit(void) { }
static inline int sev_es_setup_ap_jump_table(struct real_mode_header *rmh) { return 0; }
static inline void sev_es_nmi_complete(void) { }
static inline int sev_es_efi_map_ghcbs(pgd_t *pgd) { return 0; }
static inline int pvalidate(unsigned long vaddr, bool rmp_psize, bool validate) { return 0; }
static inline int rmpadjust(unsigned long vaddr, bool rmp_psize, unsigned long attrs) { return 0; }
static inline void setup_ghcb(void) { }
static inline void __init
early_snp_set_memory_private(unsigned long vaddr, unsigned long paddr, unsigned int npages) { }
static inline void __init
early_snp_set_memory_shared(unsigned long vaddr, unsigned long paddr, unsigned int npages) { }
static inline void __init snp_prep_memory(unsigned long paddr, unsigned int sz, enum psc_op op) { }
static inline void snp_set_memory_shared(unsigned long vaddr, unsigned int npages) { }
static inline void snp_set_memory_private(unsigned long vaddr, unsigned int npages) { }
static inline void snp_set_wakeup_secondary_cpu(void) { }
static inline bool snp_init(struct boot_params *bp) { return false; }
static inline void snp_abort(void) { }
static inline int snp_issue_guest_request(u64 exit_code, struct snp_req_data *input,
					  unsigned long *fw_err)
{
	return -ENOTTY;
}
static inline bool snp_soft_rmptable(void) { return false; }
static inline void __init snp_set_soft_rmptable(void) {}
#endif

#endif
