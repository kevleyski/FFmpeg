/*
 * Copyright (c) 2018, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#include <cstdlib>
#include <new>

#include "config/aom_config.h"
#include "config/aom_dsp_rtcd.h"

#include "aom/aom_codec.h"
#include "aom/aom_integer.h"
#include "aom_dsp/variance.h"
#include "aom_mem/aom_mem.h"
#include "aom_ports/aom_timer.h"
#include "aom_ports/mem.h"
#include "av1/common/reconinter.h"
#include "test/acm_random.h"
#include "test/clear_system_state.h"
#include "test/register_state_check.h"
#include "test/util.h"
#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

namespace AV1CompMaskVariance {
typedef void (*comp_mask_pred_func)(uint8_t *comp_pred, const uint8_t *pred,
                                    int width, int height, const uint8_t *ref,
                                    int ref_stride, const uint8_t *mask,
                                    int mask_stride, int invert_mask);
#if HAVE_SSSE3 || HAVE_AV2
const BLOCK_SIZE kValidBlockSize[] = {
  BLOCK_8X8,   BLOCK_8X16, BLOCK_8X32,  BLOCK_16X8,  BLOCK_16X16,
  BLOCK_16X32, BLOCK_32X8, BLOCK_32X16, BLOCK_32X32,
};
#endif
typedef ::testing::tuple<comp_mask_pred_func, BLOCK_SIZE> CompMaskPredParam;

class AV1CompMaskVarianceTest
    : public ::testing::TestWithParam<CompMaskPredParam> {
 public:
  ~AV1CompMaskVarianceTest();
  void SetUp();

  void TearDown();

 protected:
  void RunCheckOutput(comp_mask_pred_func test_impl, BLOCK_SIZE bsize, int inv);
  void RunSpeedTest(comp_mask_pred_func test_impl, BLOCK_SIZE bsize);
  bool CheckResult(int width, int height) {
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const int idx = y * width + x;
        if (comp_pred1_[idx] != comp_pred2_[idx]) {
          printf("%dx%d mismatch @%d(%d,%d) ", width, height, idx, y, x);
          printf("%d != %d ", comp_pred1_[idx], comp_pred2_[idx]);
          return false;
        }
      }
    }
    return true;
  }

  libaom_test::ACMRandom rnd_;
  uint8_t *comp_pred1_;
  uint8_t *comp_pred2_;
  uint8_t *pred_;
  uint8_t *ref_buffer_;
  uint8_t *ref_;
};

AV1CompMaskVarianceTest::~AV1CompMaskVarianceTest() { ; }

void AV1CompMaskVarianceTest::SetUp() {
  rnd_.Reset(libaom_test::ACMRandom::DeterministicSeed());
  av1_init_wedge_masks();
  comp_pred1_ = (uint8_t *)aom_memalign(16, MAX_SB_SQUARE);
  comp_pred2_ = (uint8_t *)aom_memalign(16, MAX_SB_SQUARE);
  pred_ = (uint8_t *)aom_memalign(16, MAX_SB_SQUARE);
  ref_buffer_ = (uint8_t *)aom_memalign(16, MAX_SB_SQUARE + (8 * MAX_SB_SIZE));
  ref_ = ref_buffer_ + (8 * MAX_SB_SIZE);
  for (int i = 0; i < MAX_SB_SQUARE; ++i) {
    pred_[i] = rnd_.Rand8();
  }
  for (int i = 0; i < MAX_SB_SQUARE + (8 * MAX_SB_SIZE); ++i) {
    ref_buffer_[i] = rnd_.Rand8();
  }
}

void AV1CompMaskVarianceTest::TearDown() {
  aom_free(comp_pred1_);
  aom_free(comp_pred2_);
  aom_free(pred_);
  aom_free(ref_buffer_);
  libaom_test::ClearSystemState();
}

void AV1CompMaskVarianceTest::RunCheckOutput(comp_mask_pred_func test_impl,
                                             BLOCK_SIZE bsize, int inv) {
  const int w = block_size_wide[bsize];
  const int h = block_size_high[bsize];

  int wedge_types = (1 << get_wedge_bits_lookup(bsize));
  for (int wedge_index = 0; wedge_index < wedge_types; ++wedge_index) {
    const uint8_t *mask = av1_get_contiguous_soft_mask(wedge_index, 1, bsize);

    aom_comp_mask_pred_c(comp_pred1_, pred_, w, h, ref_, MAX_SB_SIZE, mask, w,
                         inv);
    test_impl(comp_pred2_, pred_, w, h, ref_, MAX_SB_SIZE, mask, w, inv);

    ASSERT_EQ(CheckResult(w, h), true)
        << " wedge " << wedge_index << " inv " << inv;
  }
}

void AV1CompMaskVarianceTest::RunSpeedTest(comp_mask_pred_func test_impl,
                                           BLOCK_SIZE bsize) {
  const int w = block_size_wide[bsize];
  const int h = block_size_high[bsize];

  int wedge_types = (1 << get_wedge_bits_lookup(bsize));
  int wedge_index = wedge_types / 2;
  const uint8_t *mask = av1_get_contiguous_soft_mask(wedge_index, 1, bsize);
  const int num_loops = 1000000000 / (w + h);

  comp_mask_pred_func funcs[2] = { aom_comp_mask_pred_c, test_impl };
  double elapsed_time[2] = { 0 };
  for (int i = 0; i < 2; ++i) {
    aom_usec_timer timer;
    aom_usec_timer_start(&timer);
    comp_mask_pred_func func = funcs[i];
    for (int j = 0; j < num_loops; ++j) {
      func(comp_pred1_, pred_, w, h, ref_, MAX_SB_SIZE, mask, w, 0);
    }
    aom_usec_timer_mark(&timer);
    double time = static_cast<double>(aom_usec_timer_elapsed(&timer));
    elapsed_time[i] = 1000.0 * time / num_loops;
  }
  printf("compMask %3dx%-3d: %7.2f/%7.2fns", w, h, elapsed_time[0],
         elapsed_time[1]);
  printf("(%3.2f)\n", elapsed_time[0] / elapsed_time[1]);
}

TEST_P(AV1CompMaskVarianceTest, CheckOutput) {
  // inv = 0, 1
  RunCheckOutput(GET_PARAM(0), GET_PARAM(1), 0);
  RunCheckOutput(GET_PARAM(0), GET_PARAM(1), 1);
}

TEST_P(AV1CompMaskVarianceTest, DISABLED_Speed) {
  RunSpeedTest(GET_PARAM(0), GET_PARAM(1));
}

#if HAVE_SSSE3
INSTANTIATE_TEST_CASE_P(
    SSSE3, AV1CompMaskVarianceTest,
    ::testing::Combine(::testing::Values(&aom_comp_mask_pred_ssse3),
                       ::testing::ValuesIn(kValidBlockSize)));
#endif

#if HAVE_AVX2
INSTANTIATE_TEST_CASE_P(
    AVX2, AV1CompMaskVarianceTest,
    ::testing::Combine(::testing::Values(&aom_comp_mask_pred_avx2),
                       ::testing::ValuesIn(kValidBlockSize)));
#endif

#ifndef aom_comp_mask_pred
// can't run this test if aom_comp_mask_pred is defined to aom_comp_mask_pred_c
class AV1CompMaskUpVarianceTest : public AV1CompMaskVarianceTest {
 public:
  ~AV1CompMaskUpVarianceTest();

 protected:
  void RunCheckOutput(comp_mask_pred_func test_impl, BLOCK_SIZE bsize, int inv);
  void RunSpeedTest(comp_mask_pred_func test_impl, BLOCK_SIZE bsize,
                    int havSub);
};

AV1CompMaskUpVarianceTest::~AV1CompMaskUpVarianceTest() { ; }

void AV1CompMaskUpVarianceTest::RunCheckOutput(comp_mask_pred_func test_impl,
                                               BLOCK_SIZE bsize, int inv) {
  const int w = block_size_wide[bsize];
  const int h = block_size_high[bsize];
  int wedge_types = (1 << get_wedge_bits_lookup(bsize));

  // loop through subx and suby
  for (int sub = 0; sub < 8 * 8; ++sub) {
    int subx = sub & 0x7;
    int suby = (sub >> 3);
    for (int wedge_index = 0; wedge_index < wedge_types; ++wedge_index) {
      const uint8_t *mask = av1_get_contiguous_soft_mask(wedge_index, 1, bsize);

      aom_comp_mask_pred = aom_comp_mask_pred_c;  // ref
      aom_comp_mask_upsampled_pred(NULL, NULL, 0, 0, NULL, comp_pred1_, pred_,
                                   w, h, subx, suby, ref_, MAX_SB_SIZE, mask, w,
                                   inv);

      aom_comp_mask_pred = test_impl;  // test
      aom_comp_mask_upsampled_pred(NULL, NULL, 0, 0, NULL, comp_pred2_, pred_,
                                   w, h, subx, suby, ref_, MAX_SB_SIZE, mask, w,
                                   inv);
      ASSERT_EQ(CheckResult(w, h), true)
          << " wedge " << wedge_index << " inv " << inv << "sub (" << subx
          << "," << suby << ")";
    }
  }
}

void AV1CompMaskUpVarianceTest::RunSpeedTest(comp_mask_pred_func test_impl,
                                             BLOCK_SIZE bsize, int havSub) {
  const int w = block_size_wide[bsize];
  const int h = block_size_high[bsize];
  const int subx = havSub ? 3 : 0;
  const int suby = havSub ? 4 : 0;

  int wedge_types = (1 << get_wedge_bits_lookup(bsize));
  int wedge_index = wedge_types / 2;
  const uint8_t *mask = av1_get_contiguous_soft_mask(wedge_index, 1, bsize);

  const int num_loops = 1000000000 / (w + h);
  comp_mask_pred_func funcs[2] = { &aom_comp_mask_pred_c, test_impl };
  double elapsed_time[2] = { 0 };
  for (int i = 0; i < 2; ++i) {
    aom_usec_timer timer;
    aom_usec_timer_start(&timer);
    aom_comp_mask_pred = funcs[i];
    for (int j = 0; j < num_loops; ++j) {
      aom_comp_mask_upsampled_pred(NULL, NULL, 0, 0, NULL, comp_pred1_, pred_,
                                   w, h, subx, suby, ref_, MAX_SB_SIZE, mask, w,
                                   0);
    }
    aom_usec_timer_mark(&timer);
    double time = static_cast<double>(aom_usec_timer_elapsed(&timer));
    elapsed_time[i] = 1000.0 * time / num_loops;
  }
  printf("CompMaskUp[%d] %3dx%-3d:%7.2f/%7.2fns", havSub, w, h, elapsed_time[0],
         elapsed_time[1]);
  printf("(%3.2f)\n", elapsed_time[0] / elapsed_time[1]);
}

TEST_P(AV1CompMaskUpVarianceTest, CheckOutput) {
  // inv mask = 0, 1
  RunCheckOutput(GET_PARAM(0), GET_PARAM(1), 0);
  RunCheckOutput(GET_PARAM(0), GET_PARAM(1), 1);
}

TEST_P(AV1CompMaskUpVarianceTest, DISABLED_Speed) {
  RunSpeedTest(GET_PARAM(0), GET_PARAM(1), 1);
}

#if HAVE_SSSE3
INSTANTIATE_TEST_CASE_P(
    SSSE3, AV1CompMaskUpVarianceTest,
    ::testing::Combine(::testing::Values(&aom_comp_mask_pred_ssse3),
                       ::testing::ValuesIn(kValidBlockSize)));
#endif

#if HAVE_AVX2
INSTANTIATE_TEST_CASE_P(
    AVX2, AV1CompMaskUpVarianceTest,
    ::testing::Combine(::testing::Values(&aom_comp_mask_pred_avx2),
                       ::testing::ValuesIn(kValidBlockSize)));
#endif

#endif  // ifndef aom_comp_mask_pred
}  // namespace AV1CompMaskVariance
