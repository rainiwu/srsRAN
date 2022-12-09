/**
 * Raini Wu <rainiwu@ucsd.edu>, November 2022
 *
 * This file is not a part of srsRAN.
 */

#include "srsenb/hdr/stack/mac/schedulers/sched_ext_prio.h"

// address of a zeromq pub socket of rntis
#define ADDR "ipc:///dev/shm/priorities"

namespace srsenb {

sched_ext_prio::sched_ext_prio(const sched_cell_params_t&           cell_params_,
                               const sched_interface::sched_args_t& sched_args) :
  cell_params(&cell_params_)
{
  // initialize appropriate zeromq communications
  this->context = std::make_shared<zmq::context_t>(1);
  this->socket  = zmq::socket_t(*this->context, zmq::socket_type::sub);
  this->socket.set(zmq::sockopt::subscribe, "");
  this->socket.connect(ADDR);
}

// reads the appropriate rnti to prioritize from the handle app
size_t sched_ext_prio::get_external_prio(const sched_ue_list& ue_db)
{
  zmq::message_t msg;
  this->socket.recv(msg, zmq::recv_flags::dontwait);
  if (msg.empty()) {
    // utilize previous priority if no new direction
    return this->prev_prio;
  }
  const int prio_rnti = *msg.data<int>();
  size_t    prio_idx  = 0;
  for (const auto& ue : ue_db) {
    if (prio_rnti == ue.first) {
      this->prev_prio = prio_idx;
      return prio_idx;
    }
    prio_idx++;
  }
  // utilize previous priority if allocation falls through
  printf("warning: cannot match against rnti 0x%x\n", prio_rnti);
  return this->prev_prio;
}

// downlink scheduling uses our custom priority-based results
void sched_ext_prio::sched_dl_users(sched_ue_list& ue_db, sf_sched* tti_sched)
{
  static int i = 0;
  if (ue_db.empty()) {
    i++;
    return;
  }

  auto result = this->get_external_prio(ue_db);
  sched_dl_txs(ue_db, tti_sched, result);
}

// uplink scheduling uses default round robin - intended to enable connections
// even when the rl is not actively making decisions
void sched_ext_prio::sched_ul_users(sched_ue_list& ue_db, sf_sched* tti_sched)
{
  if (ue_db.empty()) {
    return;
  }

  auto result = tti_sched->get_tti_tx_ul().to_uint() % (uint32_t)ue_db.size();
  sched_ul_retxs(ue_db, tti_sched, result);
  sched_ul_newtxs(ue_db, tti_sched, result);
}

/** The following functions are derived from sched_time_rr
 *
 */

// modified downlink scheduling - instead of a two tier retransmission/newtx allocation
// utilize only one to allow the given priority to be absolute
// (even retransmissions from an unprioritized ue will be starved if necessary)
void sched_ext_prio::sched_dl_txs(sched_ue_list& ue_db, sf_sched* tti_sched, size_t prio_idx)
{
  auto iter = ue_db.begin();
  std::advance(iter, prio_idx);
  for (uint32_t ue_count = 0; ue_count < ue_db.size(); ++iter, ++ue_count) {
    if (iter == ue_db.end()) {
      iter = ue_db.begin();
    }
    sched_ue& user = *iter->second;

    const dl_harq_proc* h = get_dl_retx_harq(user, tti_sched);
    if (h != nullptr) {
      try_dl_retx_alloc(*tti_sched, user, *h);
    }

    // allocate new txs
    if (user.enb_to_ue_cc_idx(cell_params->enb_cc_idx) < 0) {
      continue;
    }
    h = get_dl_newtx_harq(user, tti_sched);
    // Check if there is an empty harq for the newtx
    if (h == nullptr) {
      continue;
    }
    if (try_dl_newtx_alloc_greedy(*tti_sched, user, *h) == alloc_result::no_cch_space) {
      logger.info("SCHED: Couldn't find space in PDCCH/PUCCH for DL tx for rnti=0x%x", user.get_rnti());
    }
  }
}

void sched_ext_prio::sched_ul_retxs(sched_ue_list& ue_db, sf_sched* tti_sched, size_t prio_idx)
{
  auto iter = ue_db.begin();
  std::advance(iter, prio_idx);
  for (uint32_t ue_count = 0; ue_count < ue_db.size(); ++iter, ++ue_count) {
    if (iter == ue_db.end()) {
      iter = ue_db.begin(); // wrap around
    }
    sched_ue&           user = *iter->second;
    const ul_harq_proc* h    = get_ul_retx_harq(user, tti_sched);
    // Check if there is a pending retx
    if (h == nullptr) {
      continue;
    }
    alloc_result code = try_ul_retx_alloc(*tti_sched, user, *h);
    if (code == alloc_result::no_cch_space) {
      logger.debug("SCHED: Couldn't find space in PDCCH for UL retx of rnti=0x%x", user.get_rnti());
    }
  }
}

void sched_ext_prio::sched_ul_newtxs(sched_ue_list& ue_db, sf_sched* tti_sched, size_t prio_idx)
{
  auto iter = ue_db.begin();
  std::advance(iter, prio_idx);
  for (uint32_t ue_count = 0; ue_count < ue_db.size(); ++iter, ++ue_count) {
    if (iter == ue_db.end()) {
      iter = ue_db.begin(); // wrap around
    }
    sched_ue&           user = *iter->second;
    const ul_harq_proc* h    = get_ul_newtx_harq(user, tti_sched);
    // Check if there is a empty harq
    if (h == nullptr) {
      continue;
    }
    uint32_t pending_data = user.get_pending_ul_new_data(tti_sched->get_tti_tx_ul(), cell_params->enb_cc_idx);
    // Check if there is a empty harq, and data to transmit
    if (pending_data == 0) {
      continue;
    }
    uint32_t     pending_rb = user.get_required_prb_ul(cell_params->enb_cc_idx, pending_data);
    prb_interval alloc      = find_contiguous_ul_prbs(pending_rb, tti_sched->get_ul_mask());
    if (alloc.empty()) {
      continue;
    }
    alloc_result ret = tti_sched->alloc_ul_user(&user, alloc);
    if (ret == alloc_result::no_cch_space) {
      logger.info(
          "SCHED: rnti=0x%x, cc=%d, Couldn't find space in PDCCH for UL tx", user.get_rnti(), cell_params->enb_cc_idx);
    }
  }
}
} // namespace srsenb
