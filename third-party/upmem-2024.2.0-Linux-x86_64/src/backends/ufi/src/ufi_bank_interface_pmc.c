#include <dpu_types.h>
#include <dpu_attributes.h>
#include <ufi_rank_utils.h>
#include <ufi/ufi.h>
#include <dpu_bank_interface_pmc.h>
#include <dpu_management.h>

// Addresses mapping scheme Bank Monitor
// * @0x90: Global Configuration
//    - [0]: Enable/Disable Bank Monitor Module
//       * 0b1: enable Module - shall be set during Conttrol Command (except for Enable/Disable the module)
//       * 0b0: Disable Module
// * @0x91: Command of Counter Byte Index
//    - [2:0]: Return Counters value Index to Bank Interface
//      * 0h0: return counter1[7:0]
//      * 0h1: return counter1[15:8]
//      * 0h2: return counter1[23:16]
//      * 0h3: return counter1[31:24]
//      * 0h4: return counter2[7:0]
//      * 0h5: return counter2[15:8]
//      * 0h6: return counter2[23:16]
//      * 0h7: return counter2[31:24]
//    -[7]: Daisy chain the 32-bits counter to enable a single 64-bits counter
// * @0x92 : Configuration of Event Counter1 selection, writing in this register clears the counter1
//  - 0x00: Count DPU LDMA Instruction
//  - 0x01: Count DPU SDMA INstruction
//  - 0x02: Count DPU 64-bit read Access
//  - 0x03: Count DPU 64-bit Write Access
//  - 0x04: Count Host Activate command
//  - 0x05: Count Host refresh command
//  - 0x06: Count Row Hammer refresh command
//  - 0x07: Count WaveGen Clock Cycle (fclk_div2)
// * @0x93 : Configuration Event Counter2 selection, writing in this register clears the counter2
//  - 0x00: Count DPU LDMA Instruction
//  - 0x01: Count DPU SDMA Instruction
//  - 0x02: Count DPU 64-bit read Access
//  - 0x03: Count DPU 64-bit Write Access
//  - 0x04: Count Host Activate command
//  - 0x05: Count Host refresh command
//  - 0x06: Count Row Hammer refresh command
//  - 0x07: Count WaveGen Clock Cycle (fclk_div2)
// * @0x94: Command Stop Counters
//  - writing 0x01, stop counter1
//  - writing 0x02, stop counter2
//  - writing 0x03, stop both counters

const uint8_t bank_interface_pmc_cmd_monitor_en = 0x90;
const uint8_t bank_interface_pmc_cfg_event_cntr_chain = 0x91;

const uint8_t bank_interface_pmc_cfg_event1 = 0x92;
const uint8_t bank_interface_pmc_cfg_event2 = 0x93;

const uint8_t bank_interface_pmc_cmd_stop_cntr = 0x94;
const uint8_t bank_interface_select_wavegen_read_register = 0xFF;
const uint8_t bank_interface_cmd_select_return_data = 0x04;

__API_SYMBOL__ dpu_error_t
dpu_bank_interface_pmc_check_compatibility(struct dpu_t *dpu)
{
	if (dpu->rank->description->hw.signature.chip_id < vD_fun_v1_4) {
		LOG_DPU(DEBUG, dpu, "DMA PMC not available");
		return DPU_ERR_INTERNAL;
	}
	return DPU_OK;
}

__API_SYMBOL__ dpu_error_t __dpu_bank_interface_pmc_enable(
	struct dpu_t *dpu, bank_interface_pmc_config_t configuration)
{
	dpu_error_t status;
	uint8_t mask = CI_MASK_ONE(dpu->slice_id);

	LOG_DPU(VERBOSE, dpu, "");
	if ((status = dpu_bank_interface_pmc_check_compatibility(dpu)) !=
	    DPU_OK) {
		return status;
	}

	/* sanity check: */
	if (configuration.mode != BANK_INTERFACE_PMC_64BIT_MODE &&
	    configuration.mode != BANK_INTERFACE_PMC_32BIT_MODE) {
		LOG_DPU(DEBUG, dpu, "DMA PMC invalid mode");
		return DPU_ERR_INTERNAL;
	}

	if (configuration.counter_1 > BANK_INTERFACE_PMC_CYCLES ||
	    configuration.counter_2 > BANK_INTERFACE_PMC_CYCLES) {
		LOG_DPU(DEBUG, dpu, "DMA PMC invalid counter configuration");
		return DPU_ERR_INTERNAL;
	}

	if (configuration.mode == BANK_INTERFACE_PMC_64BIT_MODE &&
	    configuration.counter_1 != configuration.counter_2) {
		LOG_DPU(DEBUG, dpu, "DMA PMC missmatch counter in 64bit mode");
		return DPU_ERR_INTERNAL;
	}

	dpu_lock_rank(dpu->rank);

	FF(ufi_select_dpu(dpu->rank, &mask, dpu->dpu_id));

	// Enable Module for configuration
	FF(ufi_write_dma_ctrl(dpu->rank, mask,
			      bank_interface_pmc_cmd_monitor_en, 0x1));

	// Stop Counter (if there were previously enabled)
	FF(ufi_write_dma_ctrl(dpu->rank, mask, bank_interface_pmc_cmd_stop_cntr,
			      0x3));

	// Set 2 32-bits Counter Mode or 1 64-bits Counter Mode
	FF(ufi_write_dma_ctrl(dpu->rank, mask,
			      bank_interface_pmc_cfg_event_cntr_chain,
			      configuration.mode << 7));

	// Set and clear counters
	FF(ufi_write_dma_ctrl(dpu->rank, mask, bank_interface_pmc_cfg_event1,
			      configuration.counter_1));
	FF(ufi_write_dma_ctrl(dpu->rank, mask, bank_interface_pmc_cfg_event2,
			      configuration.counter_2));

	// Flush readop2 (Pipeline to DMA cfg data path)
	FF(ufi_clear_dma_ctrl(dpu->rank, mask));

	dpu->bank_interface_pmc_configuration_is_32bit_mode =
		(configuration.mode == BANK_INTERFACE_PMC_32BIT_MODE);
	dpu->bank_interface_pmc_configured = true;
end:
	dpu_unlock_rank(dpu->rank);

	return status;
}

__API_SYMBOL__ dpu_error_t __dpu_bank_interface_pmc_disable(struct dpu_t *dpu)
{
	dpu_error_t status;
	uint8_t mask = CI_MASK_ONE(dpu->slice_id);

	LOG_DPU(VERBOSE, dpu, "");
	if ((status = dpu_bank_interface_pmc_check_compatibility(dpu)) !=
	    DPU_OK) {
		return status;
	}

	dpu_lock_rank(dpu->rank);

	FF(ufi_select_dpu(dpu->rank, &mask, dpu->dpu_id));

	FF(ufi_write_dma_ctrl(dpu->rank, mask,
			      bank_interface_pmc_cmd_monitor_en, 0x0));

	// Flush readop2 (Pipeline to DMA cfg data path)
	FF(ufi_clear_dma_ctrl(dpu->rank, mask));

end:
	dpu_unlock_rank(dpu->rank);

	return status;
}

__API_SYMBOL__ dpu_error_t
__dpu_bank_interface_pmc_stop_counters(struct dpu_t *dpu)
{
	dpu_error_t status;
	uint8_t mask = CI_MASK_ONE(dpu->slice_id);

	LOG_DPU(VERBOSE, dpu, "");
	if ((status = dpu_bank_interface_pmc_check_compatibility(dpu)) !=
	    DPU_OK) {
		return status;
	}

	dpu_lock_rank(dpu->rank);

	FF(ufi_select_dpu(dpu->rank, &mask, dpu->dpu_id));

	FF(ufi_write_dma_ctrl(dpu->rank, mask, bank_interface_pmc_cmd_stop_cntr,
			      0x3));

	// Flush readop2 (Pipeline to DMA cfg data path)
	FF(ufi_clear_dma_ctrl(dpu->rank, mask));

end:
	dpu_unlock_rank(dpu->rank);

	return status;
}

__API_SYMBOL__ dpu_error_t __dpu_bank_interface_pmc_read_reg32(
	struct dpu_t *dpu, int reg_sel, uint32_t *reg_result)
{
	dpu_error_t status;
	uint8_t mask = CI_MASK_ONE(dpu->slice_id);

	uint32_t bnk_cntr = 0;
	uint32_t each_byte;

	uint8_t result_array[DPU_MAX_NR_CIS];

	LOG_DPU(VERBOSE, dpu, "");

	dpu_lock_rank(dpu->rank);

	FF(ufi_select_dpu(dpu->rank, &mask, dpu->dpu_id));

	//Bank Interface return data Mux - Select returned Data from Bank Montoring
	FF(ufi_write_dma_ctrl(dpu->rank, mask,
			      bank_interface_select_wavegen_read_register,
			      bank_interface_cmd_select_return_data));
	FF(ufi_clear_dma_ctrl(dpu->rank, mask));

	for (each_byte = 0; each_byte < 4; each_byte++) {
		uint8_t cntr_byte = 0;
		FF(ufi_write_dma_ctrl(dpu->rank, mask,
				      bank_interface_pmc_cfg_event_cntr_chain,
				      each_byte + (4 * reg_sel)));
		FF(ufi_clear_dma_ctrl(dpu->rank, mask));

		FF(ufi_read_dma_ctrl(dpu->rank, mask, result_array));
		cntr_byte = result_array[dpu->slice_id];

		bnk_cntr |= (uint32_t)cntr_byte << (each_byte * 8);
	}

	*reg_result = bnk_cntr;

end:
	dpu_unlock_rank(dpu->rank);

	return status;
}

__API_SYMBOL__ dpu_error_t
__dpu_bank_interface_pmc_read_reg64(struct dpu_t *dpu, uint64_t *reg_result)
{
	dpu_error_t status;
	uint8_t mask = CI_MASK_ONE(dpu->slice_id);

	uint64_t bnk_cntr = 0;
	uint32_t each_byte;

	uint8_t result_array[DPU_MAX_NR_CIS];

	LOG_DPU(VERBOSE, dpu, "");

	dpu_lock_rank(dpu->rank);

	FF(ufi_select_dpu(dpu->rank, &mask, dpu->dpu_id));

	//Bank Interface return data Mux - Select returned Data from Bank Montoring
	FF(ufi_write_dma_ctrl(dpu->rank, mask,
			      bank_interface_select_wavegen_read_register,
			      bank_interface_cmd_select_return_data));
	FF(ufi_clear_dma_ctrl(dpu->rank, mask));

	for (each_byte = 0; each_byte < 8; each_byte++) {
		uint8_t cntr_byte = 0;
		FF(ufi_write_dma_ctrl(dpu->rank, mask,
				      bank_interface_pmc_cfg_event_cntr_chain,
				      (0x80 | each_byte)));
		FF(ufi_clear_dma_ctrl(dpu->rank, mask));

		FF(ufi_read_dma_ctrl(dpu->rank, mask, result_array));
		cntr_byte = result_array[dpu->slice_id];

		bnk_cntr |= (uint64_t)cntr_byte << (each_byte * 8);
	}

	*reg_result = bnk_cntr;

end:
	dpu_unlock_rank(dpu->rank);

	return status;
}
