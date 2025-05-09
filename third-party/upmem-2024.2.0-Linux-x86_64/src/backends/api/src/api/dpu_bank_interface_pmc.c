#include <stdio.h>

#include "dpu_attributes.h"
#include "dpu_error.h"
#include "dpu_management.h"
#include "dpu_types.h"
#include "ufi/ufi_bank_interface_pmc.h"
#include "ufi/ufi_config.h"
#include <dpu_bank_interface_pmc.h>
#include <ufi_rank_utils.h>

__API_SYMBOL__
dpu_error_t
dpu_bank_interface_pmc_enable(struct dpu_set_t set, bank_interface_pmc_config_t configuration)
{
    struct dpu_t *dpu = dpu_from_set(set);
    if (!dpu)
        return DPU_ERR_INTERNAL;
    return __dpu_bank_interface_pmc_enable(dpu, configuration);
}

__API_SYMBOL__
dpu_error_t
dpu_bank_interface_pmc_disable(struct dpu_set_t set)
{
    struct dpu_t *dpu = dpu_from_set(set);
    if (!dpu)
        return DPU_ERR_INTERNAL;
    return __dpu_bank_interface_pmc_disable(dpu);
}

__API_SYMBOL__
dpu_error_t
dpu_bank_interface_pmc_stop_counters(struct dpu_set_t set)
{
    struct dpu_t *dpu = dpu_from_set(set);
    if (!dpu)
        return DPU_ERR_INTERNAL;
    return __dpu_bank_interface_pmc_stop_counters(dpu);
}

__API_SYMBOL__
dpu_error_t
dpu_bank_interface_pmc_read_counters(struct dpu_set_t set, bank_interface_pmc_result_t *result)
{
    dpu_error_t status = DPU_OK;

    struct dpu_t *dpu = dpu_from_set(set);
    if (!dpu)
        return DPU_ERR_INTERNAL;

    if (!(dpu->bank_interface_pmc_configured)) {
        LOG_DPU(DEBUG, dpu, "DMA PMC not configured");
        return DPU_ERR_INTERNAL;
    }

    if (dpu->bank_interface_pmc_configuration_is_32bit_mode) {
        status = __dpu_bank_interface_pmc_read_reg32(dpu, 0, &result->two_32bits.counter_1);
        if (status != DPU_OK) {
            LOG_DPU(DEBUG, dpu, "DMA PMC reading failure");
            return status;
        }
        status = __dpu_bank_interface_pmc_read_reg32(dpu, 1, &result->two_32bits.counter_2);
        if (status != DPU_OK) {
            LOG_DPU(DEBUG, dpu, "DMA PMC reading failure");
            return status;
        }
    } else {
        status = __dpu_bank_interface_pmc_read_reg64(dpu, &result->one_64bits.counter_1);
        if (status != DPU_OK) {
            LOG_DPU(DEBUG, dpu, "DMA PMC reading failure");
            return status;
        }
    }

    return status;
}
