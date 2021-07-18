/**
 * Copyright 2013-2021 Software Radio Systems Limited
 *
 * This file is part of srsRAN.
 *
 * srsRAN is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsRAN is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#include "sched_nr_sim_ue.h"
#include "srsenb/hdr/stack/mac/nr/sched_nr.h"
#include "srsran/common/phy_cfg_nr_default.h"
#include "srsran/common/test_common.h"
#include "srsran/common/thread_pool.h"
#include <chrono>

namespace srsenb {

using dl_sched_t = sched_nr_interface::dl_sched_t;

static const srsran::phy_cfg_nr_t default_phy_cfg =
    srsran::phy_cfg_nr_default_t{srsran::phy_cfg_nr_default_t::reference_cfg_t{}};

srsran_coreset_t get_default_coreset0()
{
  srsran_coreset_t coreset{};
  coreset.id                   = 0;
  coreset.duration             = 1;
  coreset.precoder_granularity = srsran_coreset_precoder_granularity_reg_bundle;
  for (uint32_t i = 0; i < SRSRAN_CORESET_FREQ_DOMAIN_RES_SIZE; ++i) {
    coreset.freq_resources[i] = i < 8;
  }
  return coreset;
}

sched_nr_interface::cell_cfg_t get_default_cell_cfg()
{
  sched_nr_interface::cell_cfg_t cell_cfg{};

  cell_cfg.carrier = default_phy_cfg.carrier;
  cell_cfg.tdd     = default_phy_cfg.tdd;

  cell_cfg.bwps.resize(1);
  cell_cfg.bwps[0].pdcch    = default_phy_cfg.pdcch;
  cell_cfg.bwps[0].pdsch    = default_phy_cfg.pdsch;
  cell_cfg.bwps[0].rb_width = default_phy_cfg.carrier.nof_prb;

  cell_cfg.bwps[0].pdcch.coreset_present[0]      = true;
  cell_cfg.bwps[0].pdcch.coreset[0]              = get_default_coreset0();
  cell_cfg.bwps[0].pdcch.search_space_present[0] = true;
  auto& ss                                       = cell_cfg.bwps[0].pdcch.search_space[0];
  ss.id                                          = 0;
  ss.coreset_id                                  = 0;
  ss.duration                                    = 1;
  ss.type                                        = srsran_search_space_type_common_0;
  ss.nof_candidates[0]                           = 1;
  ss.nof_candidates[1]                           = 1;
  ss.nof_candidates[2]                           = 1;
  ss.nof_candidates[3]                           = 0;
  ss.nof_candidates[4]                           = 0;
  ss.nof_formats                                 = 1;
  ss.formats[0]                                  = srsran_dci_format_nr_1_0;
  cell_cfg.bwps[0].pdcch.ra_search_space_present = true;
  cell_cfg.bwps[0].pdcch.ra_search_space         = cell_cfg.bwps[0].pdcch.search_space[1];

  return cell_cfg;
}
std::vector<sched_nr_interface::cell_cfg_t> get_default_cells_cfg(uint32_t nof_sectors)
{
  std::vector<sched_nr_interface::cell_cfg_t> cells;
  cells.reserve(nof_sectors);
  for (uint32_t i = 0; i < nof_sectors; ++i) {
    cells.push_back(get_default_cell_cfg());
  }
  return cells;
}

sched_nr_interface::ue_cfg_t get_default_ue_cfg(uint32_t nof_cc)
{
  sched_nr_interface::ue_cfg_t uecfg{};
  uecfg.carriers.resize(nof_cc);
  for (uint32_t cc = 0; cc < nof_cc; ++cc) {
    uecfg.carriers[cc].active = true;
  }
  uecfg.phy_cfg = default_phy_cfg;

  return uecfg;
}

struct task_job_manager {
  std::mutex            mutex;
  int                   res_count   = 0;
  int                   pdsch_count = 0;
  srslog::basic_logger& test_logger = srslog::fetch_basic_logger("TEST");
  struct slot_guard {
    int                     count = 0;
    std::condition_variable cvar;
  };
  srsran::bounded_vector<slot_guard, 10> slot_counter{};

  explicit task_job_manager(int max_concurrent_slots = 4) : slot_counter(max_concurrent_slots) {}

  void start_slot(tti_point tti, int nof_sectors)
  {
    std::unique_lock<std::mutex> lock(mutex);
    auto&                        sl = slot_counter[tti.to_uint() % slot_counter.size()];
    while (sl.count > 0) {
      sl.cvar.wait(lock);
    }
    sl.count = nof_sectors;
  }
  void finish_cc(tti_point tti, const dl_sched_t& dl_res, const sched_nr_interface::ul_sched_t& ul_res)
  {
    std::unique_lock<std::mutex> lock(mutex);
    TESTASSERT(dl_res.pdcch_dl.size() <= 1);
    res_count++;
    pdsch_count += dl_res.pdcch_dl.size();
    auto& sl = slot_counter[tti.to_uint() % slot_counter.size()];
    if (--sl.count == 0) {
      sl.cvar.notify_one();
    }
  }
  void wait_task_finish()
  {
    std::unique_lock<std::mutex> lock(mutex);
    for (auto& sl : slot_counter) {
      while (sl.count > 0) {
        sl.cvar.wait(lock);
      }
      sl.count = 1;
    }
  }
  void print_results() const
  {
    test_logger.info("TESTER: %f PDSCH/{slot,cc} were allocated", pdsch_count / (double)res_count);
    srslog::flush();
  }
};

void sched_nr_cfg_serialized_test()
{
  uint32_t         max_nof_ttis = 1000, nof_sectors = 2;
  task_job_manager tasks;

  sched_nr_interface::sched_cfg_t             cfg;
  std::vector<sched_nr_interface::cell_cfg_t> cells_cfg = get_default_cells_cfg(nof_sectors);

  sched_nr_sim_base sched_tester(cfg, cells_cfg, "Serialized Test");

  sched_nr_interface::ue_cfg_t uecfg = get_default_ue_cfg(2);

  sched_tester.add_user(0x46, uecfg, 0);

  std::vector<long> count_per_cc(nof_sectors, 0);
  for (uint32_t nof_ttis = 0; nof_ttis < max_nof_ttis; ++nof_ttis) {
    tti_point tti_rx(nof_ttis % 10240);
    tti_point tti_tx = tti_rx + TX_ENB_DELAY;
    tasks.start_slot(tti_rx, nof_sectors);
    sched_tester.new_slot(tti_tx);
    for (uint32_t cc = 0; cc < cells_cfg.size(); ++cc) {
      sched_nr_interface::dl_sched_t dl_res;
      sched_nr_interface::ul_sched_t ul_res;
      auto                           tp1 = std::chrono::steady_clock::now();
      TESTASSERT(sched_tester.get_sched()->get_dl_sched(tti_tx, cc, dl_res) == SRSRAN_SUCCESS);
      TESTASSERT(sched_tester.get_sched()->get_ul_sched(tti_tx, cc, ul_res) == SRSRAN_SUCCESS);
      auto tp2 = std::chrono::steady_clock::now();
      count_per_cc[cc] += std::chrono::duration_cast<std::chrono::nanoseconds>(tp2 - tp1).count();
      sched_nr_cc_output_res_t out{tti_tx, cc, &dl_res, &ul_res};
      sched_tester.update(out);
      tasks.finish_cc(tti_rx, dl_res, ul_res);
      TESTASSERT(not srsran_tdd_nr_is_dl(&cells_cfg[cc].tdd, 0, (tti_tx).sf_idx()) or dl_res.pdcch_dl.size() == 1);
    }
  }

  tasks.print_results();
  TESTASSERT(tasks.pdsch_count == (int)(max_nof_ttis * nof_sectors * 0.6));

  double final_avg_usec = 0;
  for (uint32_t cc = 0; cc < cells_cfg.size(); ++cc) {
    final_avg_usec += count_per_cc[cc];
  }
  final_avg_usec = final_avg_usec / 1000.0 / max_nof_ttis;
  printf("Total time taken per slot: %f usec\n", final_avg_usec);
}

void sched_nr_cfg_parallel_cc_test()
{
  uint32_t         nof_sectors  = 2;
  uint32_t         max_nof_ttis = 1000;
  task_job_manager tasks;

  sched_nr_interface::sched_cfg_t             cfg;
  std::vector<sched_nr_interface::cell_cfg_t> cells_cfg = get_default_cells_cfg(nof_sectors);

  sched_nr_sim_base sched_tester(cfg, cells_cfg, "Parallel CC Test");

  sched_nr_interface::ue_cfg_t uecfg = get_default_ue_cfg(cells_cfg.size());
  sched_tester.add_user(0x46, uecfg, 0);

  std::array<std::atomic<long>, SRSRAN_MAX_CARRIERS> nano_count{};
  for (uint32_t nof_ttis = 0; nof_ttis < max_nof_ttis; ++nof_ttis) {
    tti_point tti_rx(nof_ttis % 10240);
    tti_point tti_tx = tti_rx + TX_ENB_DELAY;
    tasks.start_slot(tti_tx, nof_sectors);
    sched_tester.new_slot(tti_tx);
    for (uint32_t cc = 0; cc < cells_cfg.size(); ++cc) {
      srsran::get_background_workers().push_task([cc, tti_tx, &tasks, &sched_tester, &nano_count]() {
        sched_nr_interface::dl_sched_t dl_res;
        sched_nr_interface::ul_sched_t ul_res;
        auto                           tp1 = std::chrono::steady_clock::now();
        TESTASSERT(sched_tester.get_sched()->get_dl_sched(tti_tx, cc, dl_res) == SRSRAN_SUCCESS);
        TESTASSERT(sched_tester.get_sched()->get_ul_sched(tti_tx, cc, ul_res) == SRSRAN_SUCCESS);
        auto tp2 = std::chrono::steady_clock::now();
        nano_count[cc].fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(tp2 - tp1).count(),
                                 std::memory_order_relaxed);
        sched_nr_cc_output_res_t out{tti_tx, cc, &dl_res, &ul_res};
        sched_tester.update(out);
        tasks.finish_cc(tti_tx, dl_res, ul_res);
      });
    }
  }

  tasks.wait_task_finish();

  tasks.print_results();
  TESTASSERT(tasks.pdsch_count == (int)(max_nof_ttis * nof_sectors * 0.6));

  double final_avg_usec = 0;
  for (uint32_t i = 0; i < nof_sectors; ++i) {
    final_avg_usec += nano_count[i];
  }
  final_avg_usec = final_avg_usec / 1000.0 / max_nof_ttis / nof_sectors;
  printf("Total time taken per slot [usec]: %f\n", final_avg_usec);
}

void sched_nr_cfg_parallel_sf_test()
{
  uint32_t         max_nof_ttis = 1000;
  uint32_t         nof_sectors  = 2;
  task_job_manager tasks;

  sched_nr_interface::sched_cfg_t cfg;
  cfg.nof_concurrent_subframes                          = 2;
  std::vector<sched_nr_interface::cell_cfg_t> cells_cfg = get_default_cells_cfg(nof_sectors);

  sched_nr_sim_base sched_tester(cfg, cells_cfg, "Parallel SF Test");

  sched_nr_interface::ue_cfg_t uecfg = get_default_ue_cfg(cells_cfg.size());
  sched_tester.add_user(0x46, uecfg, 0);

  std::array<std::atomic<long>, SRSRAN_MAX_CARRIERS> nano_count{};
  for (uint32_t nof_ttis = 0; nof_ttis < max_nof_ttis; ++nof_ttis) {
    tti_point tti_rx(nof_ttis % 10240);
    tti_point tti_tx = tti_rx + TX_ENB_DELAY;
    tasks.start_slot(tti_tx, nof_sectors);
    sched_tester.new_slot(tti_tx);
    for (uint32_t cc = 0; cc < cells_cfg.size(); ++cc) {
      srsran::get_background_workers().push_task([cc, tti_tx, &sched_tester, &tasks, &nano_count]() {
        sched_nr_interface::dl_sched_t dl_res;
        sched_nr_interface::ul_sched_t ul_res;
        auto                           tp1 = std::chrono::steady_clock::now();
        TESTASSERT(sched_tester.get_sched()->get_dl_sched(tti_tx, cc, dl_res) == SRSRAN_SUCCESS);
        TESTASSERT(sched_tester.get_sched()->get_ul_sched(tti_tx, cc, ul_res) == SRSRAN_SUCCESS);
        auto tp2 = std::chrono::steady_clock::now();
        nano_count[cc].fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(tp2 - tp1).count(),
                                 std::memory_order_relaxed);
        sched_nr_cc_output_res_t out{tti_tx, cc, &dl_res, &ul_res};
        sched_tester.update(out);
        tasks.finish_cc(tti_tx, dl_res, ul_res);
      });
    }
  }

  tasks.wait_task_finish();

  tasks.print_results();

  double final_avg_usec = 0;
  for (uint32_t i = 0; i < nof_sectors; ++i) {
    final_avg_usec += nano_count[i];
  }
  final_avg_usec = final_avg_usec / 1000.0 / max_nof_ttis / nof_sectors;
  printf("Total time taken per slot [usec]: %f\n", final_avg_usec);
}

} // namespace srsenb

int main()
{
  auto& test_logger = srslog::fetch_basic_logger("TEST");
  test_logger.set_level(srslog::basic_levels::info);
  auto& mac_logger = srslog::fetch_basic_logger("MAC");
  mac_logger.set_level(srslog::basic_levels::info);
  auto& pool_logger = srslog::fetch_basic_logger("POOL");
  pool_logger.set_level(srslog::basic_levels::info);

  // Start the log backend.
  srslog::init();

  srsran::get_background_workers().set_nof_workers(6);

  srsenb::sched_nr_cfg_serialized_test();
  srsenb::sched_nr_cfg_parallel_cc_test();
  srsenb::sched_nr_cfg_parallel_sf_test();
}