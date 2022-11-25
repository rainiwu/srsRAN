/**
 * Raini Wu <rainiwu@ucsd.edu>, November 2022
 *
 * This file is not a part of srsRAN.
 */

#include "srsenb/hdr/stack/mac/schedulers/sched_ext_prio.h"

namespace srsenb {

sched_ext_prio::sched_ext_prio(const sched_cell_params_t& cell_params_, const sched_interface::sched_args_t& sched_args)
{}

void sched_ext_prio::sched_dl_users(sched_ue_list& ue_db, sf_sched* tti_sched) {}
void sched_ext_prio::sched_ul_users(sched_ue_list& ue_db, sf_sched* tti_sched) {}

} // namespace srsenb
