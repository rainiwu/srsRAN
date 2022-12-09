/*
 * Raini Wu <rainiwu@ucsd.edu>, November 2022
 * This file is not a part of srsRAN.
 */
#ifndef SRSRAN_SCHED_EXT_PRIO_H
#define SRSRAN_SCHED_EXT_PRIO_H

#include "srsenb/hdr/stack/mac/schedulers/sched_base.h"
#include "srsenb/hdr/stack/mac/schedulers/zmq.hpp"
#include <memory>

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
  void sched_ul_retxs(sched_ue_list& ue_db, sf_sched* tti_sched, size_t prio_idx);
  void sched_ul_newtxs(sched_ue_list& ue_db, sf_sched* tti_sched, size_t prio_idx);

  void sched_dl_txs(sched_ue_list& ue_db, sf_sched* tti_sched, size_t prio_idx);

  size_t get_external_prio(const sched_ue_list& ue_db);

  const sched_cell_params_t* cell_params = nullptr;

  std::shared_ptr<zmq::context_t> context = nullptr;

  zmq::socket_t socket;

  size_t prev_prio = 0;
};

} // namespace srsenb

#endif
