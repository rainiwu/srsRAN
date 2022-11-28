/*
 * Raini Wu <rainiwu@ucsd.edu>, November 2022
 * This file is not a part of srsRAN.
 */
#ifndef SRSRAN_SCHED_EXT_PRIO_H
#define SRSRAN_SCHED_EXT_PRIO_H

#include "srsenb/hdr/stack/mac/schedulers/sched_base.h"

namespace srsenb {

/**
 * A scheduler designed to take user priorities from an external program
 */
class sched_ext_prio final : public sched_base
{
public:
  sched_ext_prio(const sched_cell_params_t& cell_params_, const sched_interface::sched_args_t& sched_args);
  void sched_dl_users(sched_ue_list& ue_db, sf_sched* tti_sched) override;
  void sched_ul_users(sched_ue_list& ue_db, sf_sched* tti_sched) override;

protected:
  void sched_dl_retxs(sched_ue_list& ue_db, sf_sched* tti_sched, size_t prio_idx);
  void sched_dl_newtxs(sched_ue_list& ue_db, sf_sched* tti_sched, size_t prio_idx);
  void sched_ul_retxs(sched_ue_list& ue_db, sf_sched* tti_sched, size_t prio_idx);
  void sched_ul_newtxs(sched_ue_list& ue_db, sf_sched* tti_sched, size_t prio_idx);

  const sched_cell_params_t* cell_params = nullptr;
};

} // namespace srsenb

#endif
