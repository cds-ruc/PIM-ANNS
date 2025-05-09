#ifndef UFI_BANK_INTERFACE_PMC
#define UFI_BANK_INTERFACE_PMC

#include <dpu_types.h>
#include <dpu_bank_interface_pmc.h>

dpu_error_t
__dpu_bank_interface_pmc_enable(struct dpu_t *dpu,
				bank_interface_pmc_config_t configuration);

dpu_error_t __dpu_bank_interface_pmc_disable(struct dpu_t *dpu);

dpu_error_t __dpu_bank_interface_pmc_stop_counters(struct dpu_t *dpu);

dpu_error_t __dpu_bank_interface_pmc_read_reg32(struct dpu_t *dpu, int reg_sel,
						uint32_t *reg_result);

dpu_error_t __dpu_bank_interface_pmc_read_reg64(struct dpu_t *dpu,
						uint64_t *reg_result);

#endif /* UFI_BANK_INTERFACE_PMC */
