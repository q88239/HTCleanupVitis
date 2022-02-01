//#include <algorithm>
//#include <memory>
//#include <string.h>
#include <cstdio>
#include "MS_VLC_MEL.h"
#include "bitoperation.h"
#include "ht_tables.h"
#include "j2k_block.h"

#define MAX_Lcup 16834

#define round_up(x, n) (((x) + (n)-1) & (-n))
#define round_down(x, n) ((x) & (-n))
#define ceil_int(a, b) ((a) + ((b)-1)) / (b)

#define SHIFT_SIGMA 0  // J2K and HTJ2K

int32_t my_max_4(int32_t x0, int32_t x1, int32_t x2, int32_t x3) {
  int32_t y0, y1;
  y0 = (x0 > x1) ? x0 : x1;
  y1 = (x2 > x3) ? x2 : x3;
  return (y0 > y1) ? y0 : y1;
}
void j2k_codeblock::set_MagSgn_and_sigma(uint32_t &or_val) {
  const uint32_t height = this->size.y;
  const uint32_t width  = this->size.x;
  const uint32_t stride = this->band_stride;

  for (uint16_t i = 0; i < height; ++i) {
    sprec_t *const sp  = this->i_samples + i * stride;
    int32_t *const dp  = this->sample_buf + i * width;  // kuramochi
    size_t block_index = (i + 1) * (size.x + 2) + 1;
    for (uint16_t j = 0; j < width; ++j) {
      int32_t temp  = sp[j];
      uint32_t sign = static_cast<uint32_t>(temp) & 0x80000000;
      if (temp) {
        or_val |= 1;
        block_states[block_index] |= 1;
        // convert sample value to MagSgn
        temp = (temp < 0) ? -temp : temp;
        temp &= 0x7FFFFFFF;
        temp--;
        temp <<= 1;
        temp += sign >> 31;
        dp[j] = temp;
      }
      block_index++;
    }
  }
}

/********************************************************************************
 * state_MS_enc: member functions
 *******************************************************************************/
#ifdef MSNAIVE
void state_MS_enc::emitMagSgnBits(uint32_t cwd, uint8_t len) {
  /* naive implementation */
  uint8_t b;
  for (; len > 0;) {
    b = cwd & 1;
    cwd >>= 1;
    --len;
    tmp |= b << bits;
    bits++;
    if (bits == max) {
      buf[pos] = tmp;
      pos++;
      max  = (tmp == 0xFF) ? 7 : 8;
      tmp  = 0;
      bits = 0;
    }
  }
  /* slightly faster implementation */
  //  for (; len > 0;) {
  //    int32_t t = std::min(max - bits, (int32_t)len);
  //    tmp |= (cwd & ((1 << t) - 1)) << bits;
  //    bits += t;
  //    cwd >>= t;
  //    len -= t;
  //    if (bits >= max) {
  //      buf[pos] = tmp;
  //      pos++;
  //      max  = (tmp == 0xFF) ? 7 : 8;
  //      tmp  = 0;
  //      bits = 0;
  //    }
  //  }
}
#else
void state_MS_enc::emitMagSgnBits(uint32_t cwd, uint8_t len, uint8_t emb_1) {
  int32_t temp = emb_1 << len;
  cwd -= temp;
  Creg |= static_cast<uint64_t>(cwd) << ctreg;
  ctreg += len;
  while (ctreg >= 32) {
    emit_dword();
  }
}
void state_MS_enc::emit_dword() {
  for (int i = 0; i < 4; ++i) {
    if (last == 0xFF) {
      last = static_cast<uint8_t>(Creg & 0x7F);
      Creg >>= 7;
      ctreg -= 7;
    } else {
      last = static_cast<uint8_t>(Creg & 0xFF);
      Creg >>= 8;
      ctreg -= 8;
    }
    buf[pos++] = last;
  }
}
#endif

int32_t state_MS_enc::termMS() {
#ifdef MSNAIVE
  /* naive implementation */
  if (bits > 0) {
    for (; bits < max; bits++) {
      tmp |= 1 << bits;
    }
    if (tmp != 0xFF) {
      buf[pos] = tmp;
      pos++;
    }
  } else if (max == 7) {
    pos--;
  }
#else
  while (true) {
    if (last == 0xFF) {
      if (ctreg < 7) break;
      last = static_cast<uint8_t>(Creg & 0x7F);
      Creg >>= 7;
      ctreg -= 7;
    } else {
      if (ctreg < 8) break;
      last = static_cast<uint8_t>(Creg & 0xFF);
      Creg >>= 8;
      ctreg -= 8;
    }
    buf[pos++] = last;
  }
  bool last_was_FF = (last == 0xFF);
  uint8_t fill_mask, cwd;
  if (ctreg > 0) {
    fill_mask = static_cast<uint8_t>(0xFF << ctreg);
    if (last_was_FF) {
      fill_mask &= 0x7F;
    }
    cwd = static_cast<uint8_t>(Creg |= fill_mask);
    if (cwd != 0xFF) {
      buf[pos++] = cwd;
    }
  } else if (last_was_FF) {
    pos--;
    buf[pos] = 0x00;  // may be not necessary
  }
#endif
  return pos;  // return current position as Pcup
}

/********************************************************************************
 * state_MEL_enc: member functions
 *******************************************************************************/
void state_MEL_enc::emitMELbit(uint8_t bit) {
  tmp = (tmp << 1) + bit;
  rem--;
  if (rem == 0) {
    buf[pos] = tmp;
    pos++;
    rem = (tmp == 0xFF) ? 7 : 8;
    tmp = 0;
  }
}

void state_MEL_enc::encodeMEL(uint8_t smel) {
  uint8_t eval;
  switch (smel) {
    case 0:
      MEL_run++;
      if (MEL_run >= MEL_t) {
        emitMELbit(1);
        MEL_run = 0;
        MEL_k   = (12 < (MEL_k + 1)) ? 12 : (MEL_k + 1);  // std::min(12, MEL_k + 1);
        eval    = MEL_E[MEL_k];
        MEL_t   = 1 << eval;
      }
      break;

    default:
      emitMELbit(0);
      eval = MEL_E[MEL_k];
      while (eval > 0) {
        eval--;
        // (MEL_run >> eval) & 1 = msb
        emitMELbit((MEL_run >> eval) & 1);
      }
      MEL_run = 0;
      MEL_k   = (0 > (MEL_k - 1)) ? 0 : (MEL_k - 1);  // std::max(0, MEL_k - 1);
      eval    = MEL_E[MEL_k];
      MEL_t   = 1 << eval;
      break;
  }
}

void state_MEL_enc::termMEL() {
  if (MEL_run > 0) {
    emitMELbit(1);
  }
}

/********************************************************************************
 * state_VLC_enc: member functions
 *******************************************************************************/
void state_VLC_enc::emitVLCBits(uint16_t cwd, uint8_t len) {
  // int32_t len32 = len;
  // for (; len32 > 0;) {
  //   int32_t available_bits = 8 - (last > 0x8F) - bits;
  //   int32_t t =
  //       (available_bits < len32) ? available_bits : len32;  // std::min(available_bits, (int32_t)len);
  //   tmp |= (cwd & (1 << t) - 1) << bits;
  //   bits += t;
  //   available_bits -= t;
  //   len32 -= t;
  //   cwd >>= t;
  //   if (available_bits == 0) {
  //     if ((last > 0x8f) && tmp != 0x7F) {
  //       last = 0x00;
  //       continue;
  //     }
  //     buf[pos] = tmp;
  //     pos--;  // reverse order
  //     last = tmp;
  //     tmp  = 0;
  //     bits = 0;
  //   }
  // }
  uint8_t b;
  for (; len > 0;) {
    b = cwd & 1;
    cwd >>= 1;
    len--;
    tmp |= b << bits;
    bits++;
    if ((last > 0x8F) && (tmp == 0x7F)) {
      bits++;
    }
    if (bits == 8) {
      buf[pos] = tmp;
      pos--;  // reverse order
      last = tmp;
      tmp  = 0;
      bits = 0;
    }
  }
}

/********************************************************************************
 * HT cleanup encoding: helper functions
 *******************************************************************************/
inline uint8_t Sigma(uint8_t &data) { return (data >> SHIFT_SIGMA) & 1; }
static inline void make_storage(const j2k_codeblock *const block, const uint16_t qy, const uint16_t qx,
                                const uint16_t QH, const uint16_t QW, uint8_t *const sigma_n,
                                uint32_t *const v_n, int32_t *const E_n, uint8_t *const rho_q) {
  // This function shall be called on the assumption that there are two quads
  const int32_t x[8] = {2 * qx,       2 * qx,       2 * qx + 1,       2 * qx + 1,
                        2 * (qx + 1), 2 * (qx + 1), 2 * (qx + 1) + 1, 2 * (qx + 1) + 1};
  const int32_t y[8] = {2 * qy, 2 * qy + 1, 2 * qy, 2 * qy + 1, 2 * qy, 2 * qy + 1, 2 * qy, 2 * qy + 1};

  // First quad
  for (int i = 0; i < 4; ++i) {
    sigma_n[i] = block->get_sigma(y[i], x[i]);  // kuramochi
  }
  rho_q[0] = sigma_n[0] + (sigma_n[1] << 1) + (sigma_n[2] << 2) + (sigma_n[3] << 3);
  // Second quad
  for (int i = 4; i < 8; ++i) {
    sigma_n[i] = block->get_sigma(y[i], x[i]);  // kuramochi
  }
  rho_q[1] = sigma_n[4] + (sigma_n[5] << 1) + (sigma_n[6] << 2) + (sigma_n[7] << 3);

  for (int i = 0; i < 8; ++i) {
    if ((x[i] >= 0 && x[i] < (block->size.x)) && (y[i] >= 0 && y[i] < (block->size.y))) {
      v_n[i] = block->sample_buf[x[i] + y[i] * block->size.x];  // kuramochi
    } else {
      v_n[i] = 0;
    }
  }

  for (int i = 0; i < 8; ++i) {
    E_n[i] = (32 - count_leading_zeros(((v_n[i] >> 1) << 1) + 1)) * sigma_n[i];
  }
}

static inline void make_storage_one(const j2k_codeblock *const block, const uint16_t qy, const uint16_t qx,
                                    const uint16_t QH, const uint16_t QW, uint8_t *const sigma_n,
                                    uint32_t *const v_n, int32_t *const E_n, uint8_t *const rho_q) {
  const int32_t x[4] = {2 * qx, 2 * qx, 2 * qx + 1, 2 * qx + 1};
  const int32_t y[4] = {2 * qy, 2 * qy + 1, 2 * qy, 2 * qy + 1};

  for (int i = 0; i < 4; ++i) {
    sigma_n[i] = block->get_sigma(y[i], x[i]);  // kuramochi
  }
  rho_q[0] = sigma_n[0] + (sigma_n[1] << 1) + (sigma_n[2] << 2) + (sigma_n[3] << 3);

  for (int i = 0; i < 4; ++i) {
    if ((x[i] >= 0 && x[i] < (block->size.x)) && (y[i] >= 0 && y[i] < (block->size.y))) {
      v_n[i] = block->sample_buf[x[i] + y[i] * block->size.x];  // kuramochi
    } else {
      v_n[i] = 0;
    }
  }

  for (int i = 0; i < 4; ++i) {
    E_n[i] = (32 - count_leading_zeros(((v_n[i] >> 1) << 1) + 1)) * sigma_n[i];
  }
}

// UVLC encoding for initial line pair
void encode_UVLC0(uint16_t &cwd, uint8_t &lw, int32_t u1, int32_t u2 = 0) {
  int32_t tmp;
  tmp = enc_UVLC_table0[u1 + (u2 << 5)];
  lw  = (tmp & 0xFF);
  cwd = tmp >> 8;
};

// UVLC encoding for non-initial line pair
void encode_UVLC1(uint16_t &cwd, uint8_t &lw, int32_t u1, int32_t u2 = 0) {
  int32_t tmp;
  tmp = enc_UVLC_table1[u1 + (u2 << 5)];
  lw  = (tmp & 0xFF);
  cwd = tmp >> 8;
};

// joint termination of MEL and VLC
int32_t termMELandVLC(state_VLC_enc &VLC, state_MEL_enc &MEL) {
  uint8_t MEL_mask, VLC_mask, fuse;
  MEL.tmp <<= MEL.rem;
  MEL_mask = (0xFF << MEL.rem) & 0xFF;
  VLC_mask = 0xFF >> (8 - VLC.bits);
  if ((MEL_mask | VLC_mask) != 0) {
    fuse = MEL.tmp | VLC.tmp;
    if (((((fuse ^ MEL.tmp) & MEL_mask) | ((fuse ^ VLC.tmp) & VLC_mask)) == 0) && (fuse != 0xFF)) {
      MEL.buf[MEL.pos] = fuse;
    } else {
      MEL.buf[MEL.pos] = MEL.tmp;
      VLC.buf[VLC.pos] = VLC.tmp;
      VLC.pos--;  // reverse order
    }
    MEL.pos++;
  }
  // concatenate MEL and VLC buffers
  // memmove(&MEL.buf[MEL.pos], &VLC.buf[VLC.pos + 1], MAX_Scup - VLC.pos - 1);
  // return Scup
  return (MEL.pos + MAX_Scup - VLC.pos - 1);
}

#define MAKE_STORAGE()                                                                                     \
  {                                                                                                        \
    const int32_t x[8] = {2 * qx,       2 * qx,       2 * qx + 1,       2 * qx + 1,                        \
                          2 * (qx + 1), 2 * (qx + 1), 2 * (qx + 1) + 1, 2 * (qx + 1) + 1};                 \
    const int32_t y[8] = {2 * qy, 2 * qy + 1, 2 * qy, 2 * qy + 1, 2 * qy, 2 * qy + 1, 2 * qy, 2 * qy + 1}; \
    for (int i = 0; i < 4; ++i)                                                                            \
      sigma_n[i] = (block.block_states[(y[i] + 1) * (block.size.x + 2) + (x[i] + 1)] >> SHIFT_SIGMA) & 1;  \
    rho_q[0] = sigma_n[0] + (sigma_n[1] << 1) + (sigma_n[2] << 2) + (sigma_n[3] << 3);                     \
    for (int i = 4; i < 8; ++i)                                                                            \
      sigma_n[i] = (block.block_states[(y[i] + 1) * (block.size.x + 2) + (x[i] + 1)] >> SHIFT_SIGMA) & 1;  \
    rho_q[1] = sigma_n[4] + (sigma_n[5] << 1) + (sigma_n[6] << 2) + (sigma_n[7] << 3);                     \
    for (int i = 0; i < 8; ++i) {                                                                          \
      if ((x[i] >= 0 && x[i] < (block.size.x)) && (y[i] >= 0 && y[i] < (block.size.y)))                    \
        v_n[i] = block.sample_buf[x[i] + y[i] * block.size.x];                                             \
      else                                                                                                 \
        v_n[i] = 0;                                                                                        \
    }                                                                                                      \
    for (int i = 0; i < 8; ++i)                                                                            \
      E_n[i] = (32 - count_leading_zeros(((v_n[i] >> 1) << 1) + 1)) * sigma_n[i];                          \
  }  // kuramochi

#define Q0 0
#define Q1 1

/******************************************************************************/
int32_t htj2k_encode(const uint32_t &idx, uint8_t orientation, uint8_t M_b, uint8_t R_b,
                     uint8_t transformation, float stepsize, uint32_t band_stride, sprec_t *ibuf,
                     uint32_t offset, const uint16_t &numlayers, const uint8_t &codeblock_style,
                     const element_siz &p0, const element_siz &p1, const element_siz &s,
                     int32_t *g_sample_buffer, uint8_t *g_state_buffer, uint8_t *g_compressed_buffer) {
  j2k_codeblock block(idx, orientation, M_b, R_b, transformation, stepsize, band_stride, ibuf, offset,
                      numlayers, codeblock_style, p0, p1, s, g_sample_buffer, g_state_buffer,
                      g_compressed_buffer);

  // length of HT cleanup pass
  int32_t Lcup;
  // length of MagSgn buffer
  int32_t Pcup;
  // length of MEL buffer + VLC buffer
  int32_t Scup;
  // used as a flag to invoke HT Cleanup encoding
  uint32_t or_val = 0;

  const uint16_t QW = ceil_int(block.size.x, 2);
  const uint16_t QH = ceil_int(block.size.y, 2);

  block.set_MagSgn_and_sigma(or_val);

  if (!or_val) {
    // nothing to do here because this codeblock is empty
    // set length of coding passes
    block.length      = 0;
    block.pass_length = 0;
    // set number of coding passes
    block.num_passes = 0;
    // block.layer_passes = 0; // kuramochi
    // block.layer_start  = 0; // kuramochi
    // set number of zero-bitplanes (=Zblk)
    block.num_ZBP = block.get_Mb() - 1;
    return block.length;
  }

  // buffers shall be zeroed.
  uint8_t fwd_buf[MAX_Lcup] = {0};
  uint8_t rev_buf[MAX_Scup] = {0};  // kuramochi
  // memset(fwd_buf.get(), 0, sizeof(uint8_t) * (MAX_Lcup));
  // memset(rev_buf.get(), 0, sizeof(uint8_t) * MAX_Scup);

  state_MS_enc MagSgn_encoder(fwd_buf);
  state_MEL_enc MEL_encoder(rev_buf);
  state_VLC_enc VLC_encoder(rev_buf);

  uint32_t v_n[8];
  // std::unique_ptr<int32_t[]> Eadj = std::make_unique<int32_t[]>(round_up(block.size.x, 2) + 2);
  // memset(Eadj.get(), 0, round_up(block.size.x, 2) + 2);  // kuramochi
  // std::unique_ptr<uint8_t[]> sigma_adj = std::make_unique<uint8_t[]>(round_up(block.size.x, 2) + 2);
  // memset(sigma_adj.get(), 0, round_up(block.size.x, 2) + 2);  // kuramochi

  int32_t Eadj[CBLK_WIDTH + 2]      = {0};
  uint8_t sigma_adj[CBLK_WIDTH + 2] = {0};
  uint8_t sigma_n[8] = {0}, rho_q[2] = {0}, gamma[2] = {0}, emb_k, emb_1, lw, m_n[8] = {0};
  uint16_t c_q[2] = {0, 0}, n_q[2] = {0}, CxtVLC[2] = {0}, cwd;
  int32_t E_n[8] = {0}, Emax_q[2] = {0}, U_q[2] = {0}, u_q[2] = {0}, uoff_q[2] = {0}, emb[2] = {0},
          kappa = 1;

  // Initial line pair
  int32_t *ep = Eadj;
  ep++;
  uint8_t *sp = sigma_adj;
  sp++;
  int32_t *p_sample = block.sample_buf;          //.get();
  for (uint16_t qx = 0; qx < QW - 1; qx += 2) {  // kuramochi
    const int16_t qy = 0;
    MAKE_STORAGE()

    // MEL encoding for the first quad
    if (c_q[Q0] == 0) {
      MEL_encoder.encodeMEL((rho_q[Q0] != 0));
    }

    Emax_q[Q0] = my_max_4(E_n[0], E_n[1], E_n[2], E_n[3]);
    U_q[Q0]    = (Emax_q[Q0] > kappa) ? Emax_q[Q0] : kappa;  // std::max((int32_t)Emax_q[Q0], kappa);
    u_q[Q0]    = U_q[Q0] - kappa;
    uoff_q[Q0] = (u_q[Q0]) ? 1 : 0;
#ifdef HTSIMD
    __m128i a = _mm_cmpeq_epi32(_mm_set_epi32(E_n[0], E_n[1], E_n[2], E_n[3]), _mm_set1_epi32(Emax_q[Q0]));
    __m128i b = _mm_sllv_epi32(_mm_set1_epi32(uoff_q[Q0]), _mm_set_epi32(0, 1, 2, 3));
    a         = _mm_and_si128(a, b);
    b         = _mm_hadd_epi32(a, a);
    a         = _mm_hadd_epi32(b, b);
    emb[Q0]   = _mm_cvtsi128_si32(a);
#else
    emb[Q0] = (E_n[0] == Emax_q[Q0]) ? uoff_q[Q0] : 0;
    emb[Q0] += (E_n[1] == Emax_q[Q0]) ? uoff_q[Q0] << 1 : 0;
    emb[Q0] += (E_n[2] == Emax_q[Q0]) ? uoff_q[Q0] << 2 : 0;
    emb[Q0] += (E_n[3] == Emax_q[Q0]) ? uoff_q[Q0] << 3 : 0;
#endif

    n_q[Q0]    = emb[Q0] + (rho_q[Q0] << 4) + (c_q[Q0] << 8);
    CxtVLC[Q0] = enc_CxtVLC_table0[n_q[Q0]];
    emb_k      = CxtVLC[Q0] & 0xF;
    emb_1      = n_q[Q0] % 16 & emb_k;

    for (int i = 0; i < 4; ++i) {
      m_n[i] = sigma_n[i] * U_q[Q0] - ((emb_k >> i) & 1);
#ifdef MSNAIVE
      MagSgn_encoder.emitMagSgnBits(v_n[i], m_n[i]);
#else
      MagSgn_encoder.emitMagSgnBits(v_n[i], m_n[i], (emb_1 >> i) & 1);
#endif
    }

    CxtVLC[Q0] >>= 4;
    lw = CxtVLC[Q0] & 0x07;
    CxtVLC[Q0] >>= 3;
    cwd = CxtVLC[Q0];

    ep[2 * qx]     = E_n[1];
    ep[2 * qx + 1] = E_n[3];

    sp[2 * qx]     = sigma_n[1];
    sp[2 * qx + 1] = sigma_n[3];

    VLC_encoder.emitVLCBits(cwd, lw);

    // context for 1st quad of next quad-pair
    c_q[Q0] = (sigma_n[4] | sigma_n[5]) + (sigma_n[6] << 1) + (sigma_n[7] << 2);
    // context for 2nd quad of current quad pair
    c_q[Q1] = (sigma_n[0] | sigma_n[1]) + (sigma_n[2] << 1) + (sigma_n[3] << 2);

    Emax_q[Q1]     = my_max_4(E_n[4], E_n[5], E_n[6], E_n[7]);
    U_q[Q1]        = (Emax_q[Q1] > kappa) ? Emax_q[Q1] : kappa;  // std::max((int32_t)Emax_q[Q1], kappa);
    u_q[Q1]        = U_q[Q1] - kappa;
    uoff_q[Q1]     = (u_q[Q1]) ? 1 : 0;
    int32_t uq_min = (u_q[Q0] < u_q[Q1]) ? u_q[Q0] : u_q[Q1];
    // MEL encoding of the second quad
    if (c_q[Q1] == 0) {
      if (rho_q[Q1] != 0) {
        MEL_encoder.encodeMEL(1);
      } else {
        if (uq_min > 2) {
          MEL_encoder.encodeMEL(1);
        } else {
          MEL_encoder.encodeMEL(0);
        }
      }
    } else if (uoff_q[Q0] == 1 && uoff_q[Q1] == 1) {
      if (uq_min > 2) {
        MEL_encoder.encodeMEL(1);
      } else {
        MEL_encoder.encodeMEL(0);
      }
    }
#ifdef HTSIMD
    a       = _mm_cmpeq_epi32(_mm_set_epi32(E_n[4], E_n[5], E_n[6], E_n[7]), _mm_set1_epi32(Emax_q[Q1]));
    b       = _mm_sllv_epi32(_mm_set1_epi32(uoff_q[Q1]), _mm_set_epi32(0, 1, 2, 3));
    a       = _mm_and_si128(a, b);
    b       = _mm_hadd_epi32(a, a);
    a       = _mm_hadd_epi32(b, b);
    emb[Q1] = _mm_cvtsi128_si32(a);
#else
    emb[Q1] = (E_n[4] == Emax_q[Q1]) ? uoff_q[Q1] : 0;
    emb[Q1] += (E_n[5] == Emax_q[Q1]) ? uoff_q[Q1] << 1 : 0;
    emb[Q1] += (E_n[6] == Emax_q[Q1]) ? uoff_q[Q1] << 2 : 0;
    emb[Q1] += (E_n[7] == Emax_q[Q1]) ? uoff_q[Q1] << 3 : 0;
#endif
    n_q[Q1]    = emb[Q1] + (rho_q[Q1] << 4) + (c_q[Q1] << 8);
    CxtVLC[Q1] = enc_CxtVLC_table0[n_q[Q1]];
    emb_k      = CxtVLC[Q1] & 0xF;
    emb_1      = n_q[Q1] % 16 & emb_k;
    for (int i = 0; i < 4; ++i) {
      m_n[4 + i] = sigma_n[4 + i] * U_q[Q1] - ((emb_k >> i) & 1);
#ifdef MSNAIVE
      MagSgn_encoder.emitMagSgnBits(v_n[4 + i], m_n[4 + i]);
#else
      MagSgn_encoder.emitMagSgnBits(v_n[4 + i], m_n[4 + i], (emb_1 >> i) & 1);
#endif
    }

    CxtVLC[Q1] >>= 4;
    lw = CxtVLC[Q1] & 0x07;
    CxtVLC[Q1] >>= 3;
    cwd = CxtVLC[Q1];

    VLC_encoder.emitVLCBits(cwd, lw);
    encode_UVLC0(cwd, lw, u_q[Q0], u_q[Q1]);
    VLC_encoder.emitVLCBits(cwd, lw);
    ep[2 * (qx + 1)]     = E_n[5];
    ep[2 * (qx + 1) + 1] = E_n[7];

    sp[2 * (qx + 1)]     = sigma_n[5];
    sp[2 * (qx + 1) + 1] = sigma_n[7];
  }
  if (QW & 1) {
    uint16_t qx = QW - 1;
    make_storage_one(&block, 0, qx, QH, QW, sigma_n, v_n, E_n, rho_q);
    // MEL encoding for the first quad
    if (c_q[Q0] == 0) {
      MEL_encoder.encodeMEL((rho_q[Q0] != 0));
    }
    Emax_q[Q0] = my_max_4(E_n[0], E_n[1], E_n[2], E_n[3]);
    U_q[Q0]    = (Emax_q[Q0] > kappa) ? Emax_q[Q0] : kappa;  // std::max((int32_t)Emax_q[Q0], kappa);
    u_q[Q0]    = U_q[Q0] - kappa;
    uoff_q[Q0] = (u_q[Q0]) ? 1 : 0;
#ifdef HTSIMD
    __m128i a = _mm_cmpeq_epi32(_mm_set_epi32(E_n[0], E_n[1], E_n[2], E_n[3]), _mm_set1_epi32(Emax_q[Q0]));
    __m128i b = _mm_sllv_epi32(_mm_set1_epi32(uoff_q[Q0]), _mm_set_epi32(0, 1, 2, 3));
    a         = _mm_and_si128(a, b);
    b         = _mm_hadd_epi32(a, a);
    a         = _mm_hadd_epi32(b, b);
    emb[Q0]   = _mm_cvtsi128_si32(a);
#else
    emb[Q0] = (E_n[0] == Emax_q[Q0]) ? uoff_q[Q0] : 0;
    emb[Q0] += (E_n[1] == Emax_q[Q0]) ? uoff_q[Q0] << 1 : 0;
    emb[Q0] += (E_n[2] == Emax_q[Q0]) ? uoff_q[Q0] << 2 : 0;
    emb[Q0] += (E_n[3] == Emax_q[Q0]) ? uoff_q[Q0] << 3 : 0;
#endif
    n_q[Q0]    = emb[Q0] + (rho_q[Q0] << 4) + (c_q[Q0] << 8);
    CxtVLC[Q0] = enc_CxtVLC_table0[n_q[Q0]];
    emb_k      = CxtVLC[Q0] & 0xF;
    emb_1      = n_q[Q0] % 16 & emb_k;
    for (int i = 0; i < 4; ++i) {
      m_n[i] = sigma_n[i] * U_q[Q0] - ((emb_k >> i) & 1);
#ifdef MSNAIVE
      MagSgn_encoder.emitMagSgnBits(v_n[i], m_n[i]);
#else
      MagSgn_encoder.emitMagSgnBits(v_n[i], m_n[i], (emb_1 >> i) & 1);
#endif
    }

    CxtVLC[Q0] >>= 4;
    lw = CxtVLC[Q0] & 0x07;
    CxtVLC[Q0] >>= 3;
    cwd = CxtVLC[Q0];

    ep[2 * qx]     = E_n[1];
    ep[2 * qx + 1] = E_n[3];

    sp[2 * qx]     = sigma_n[1];
    sp[2 * qx + 1] = sigma_n[3];

    VLC_encoder.emitVLCBits(cwd, lw);
    encode_UVLC0(cwd, lw, u_q[Q0]);
    VLC_encoder.emitVLCBits(cwd, lw);
  }

  // Non-initial line pair
  for (uint16_t qy = 1; qy < QH; qy++) {
    ep = Eadj;
    ep++;
    sp = sigma_adj;
    sp++;
    E_n[7]     = 0;
    sigma_n[6] = sigma_n[7] = 0;
    for (uint16_t qx = 0; qx < QW - 1; qx += 2) {
      // E_n[7] shall be saved because ep[2*qx-1] can't be changed before kappa
      // calculation
      int32_t E7     = E_n[7];
      uint8_t sigma7 = sigma_n[7];
      // context for 1st quad of current quad pair
      c_q[Q0] = (sp[2 * qx + 1] | sp[2 * qx + 2]) << 2;
      c_q[Q0] += (sigma_n[6] | sigma_n[7]) << 1;
      c_q[Q0] += sp[2 * qx - 1] | sp[2 * qx];

      MAKE_STORAGE()

      // context for 2nd quad of current quad pair
      c_q[Q1] = (sp[2 * (qx + 1) + 1] | sp[2 * (qx + 1) + 2]) << 2;
      c_q[Q1] += (sigma_n[2] | sigma_n[3]) << 1;
      c_q[Q1] += sp[2 * (qx + 1) - 1] | sp[2 * (qx + 1)];
      // MEL encoding of the first quad
      if (c_q[Q0] == 0) {
        MEL_encoder.encodeMEL((rho_q[Q0] != 0));
      }

      gamma[Q0] = (popcount32((uint32_t)rho_q[Q0]) > 1) ? 1 : 0;

      kappa = ((my_max_4(ep[2 * qx - 1], ep[2 * qx], ep[2 * qx + 1], ep[2 * qx + 2]) - 1) * gamma[Q0] > 1)
                  ? (my_max_4(ep[2 * qx - 1], ep[2 * qx], ep[2 * qx + 1], ep[2 * qx + 2]) - 1) * gamma[Q0]
                  : 1;

      ep[2 * qx] = E_n[1];
      // if (qx > 0) {
      ep[2 * qx - 1] = E7;  // put back saved E_n
      //}

      sp[2 * qx] = sigma_n[1];
      // if (qx > 0) {
      sp[2 * qx - 1] = sigma7;  // put back saved E_n
      //}

      Emax_q[Q0] = my_max_4(E_n[0], E_n[1], E_n[2], E_n[3]);
      U_q[Q0]    = (Emax_q[Q0] > kappa) ? Emax_q[Q0] : kappa;  // std::max((int32_t)Emax_q[Q0], kappa);
      u_q[Q0]    = U_q[Q0] - kappa;
      uoff_q[Q0] = (u_q[Q0]) ? 1 : 0;
#ifdef HTSIMD
      __m128i a =
          _mm_cmpeq_epi32(_mm_set_epi32(E_n[0], E_n[1], E_n[2], E_n[3]), _mm_set1_epi32(Emax_q[Q0]));
      __m128i b = _mm_sllv_epi32(_mm_set1_epi32(uoff_q[Q0]), _mm_set_epi32(0, 1, 2, 3));
      a         = _mm_and_si128(a, b);
      b         = _mm_hadd_epi32(a, a);
      a         = _mm_hadd_epi32(b, b);
      emb[Q0]   = _mm_cvtsi128_si32(a);
#else
      emb[Q0] = (E_n[0] == Emax_q[Q0]) ? uoff_q[Q0] : 0;
      emb[Q0] += (E_n[1] == Emax_q[Q0]) ? uoff_q[Q0] << 1 : 0;
      emb[Q0] += (E_n[2] == Emax_q[Q0]) ? uoff_q[Q0] << 2 : 0;
      emb[Q0] += (E_n[3] == Emax_q[Q0]) ? uoff_q[Q0] << 3 : 0;
#endif
      n_q[Q0]    = emb[Q0] + (rho_q[Q0] << 4) + (c_q[Q0] << 8);
      CxtVLC[Q0] = enc_CxtVLC_table1[n_q[Q0]];
      emb_k      = CxtVLC[Q0] & 0xF;
      emb_1      = n_q[Q0] % 16 & emb_k;
      for (int i = 0; i < 4; ++i) {
        m_n[i] = sigma_n[i] * U_q[Q0] - ((emb_k >> i) & 1);
#ifdef MSNAIVE
        MagSgn_encoder.emitMagSgnBits(v_n[i], m_n[i]);
#else
        MagSgn_encoder.emitMagSgnBits(v_n[i], m_n[i], (emb_1 >> i) & 1);
#endif
      }

      CxtVLC[Q0] >>= 4;
      lw = CxtVLC[Q0] & 0x07;
      CxtVLC[Q0] >>= 3;
      cwd = CxtVLC[Q0];

      VLC_encoder.emitVLCBits(cwd, lw);

      // MEL encoding of the second quad
      if (c_q[Q1] == 0) {
        MEL_encoder.encodeMEL((rho_q[Q1] != 0));
      }
      gamma[Q1] = (popcount32((uint32_t)rho_q[Q1]) > 1) ? 1 : 0;
      kappa = ((my_max_4(ep[2 * (qx + 1) - 1], ep[2 * (qx + 1)], ep[2 * (qx + 1) + 1], ep[2 * (qx + 1) + 2])
                - 1)
                   * gamma[Q1]
               > 1)
                  ? (my_max_4(ep[2 * (qx + 1) - 1], ep[2 * (qx + 1)], ep[2 * (qx + 1) + 1],
                              ep[2 * (qx + 1) + 2])
                     - 1)
                        * gamma[Q1]
                  : 1;

      ep[2 * (qx + 1) - 1] = E_n[3];
      ep[2 * (qx + 1)]     = E_n[5];
      if (qx + 1 == QW - 1) {  // if this quad (2nd quad) is the end of the line-pair
        ep[2 * (qx + 1) + 1] = E_n[7];
      }
      sp[2 * (qx + 1) - 1] = sigma_n[3];
      sp[2 * (qx + 1)]     = sigma_n[5];
      if (qx + 1 == QW - 1) {  // if this quad (2nd quad) is the end of the line-pair
        sp[2 * (qx + 1) + 1] = sigma_n[7];
      }

      Emax_q[Q1] = my_max_4(E_n[4], E_n[5], E_n[6], E_n[7]);
      U_q[Q1]    = (Emax_q[Q1] > kappa) ? Emax_q[Q1] : kappa;  // std::max((int32_t)Emax_q[Q1], kappa);
      u_q[Q1]    = U_q[Q1] - kappa;
      uoff_q[Q1] = (u_q[Q1]) ? 1 : 0;
#ifdef HTSIMD
      a       = _mm_cmpeq_epi32(_mm_set_epi32(E_n[4], E_n[5], E_n[6], E_n[7]), _mm_set1_epi32(Emax_q[Q1]));
      b       = _mm_sllv_epi32(_mm_set1_epi32(uoff_q[Q1]), _mm_set_epi32(0, 1, 2, 3));
      a       = _mm_and_si128(a, b);
      b       = _mm_hadd_epi32(a, a);
      a       = _mm_hadd_epi32(b, b);
      emb[Q1] = _mm_cvtsi128_si32(a);
#else
      emb[Q1] = (E_n[4] == Emax_q[Q1]) ? uoff_q[Q1] : 0;
      emb[Q1] += (E_n[5] == Emax_q[Q1]) ? uoff_q[Q1] << 1 : 0;
      emb[Q1] += (E_n[6] == Emax_q[Q1]) ? uoff_q[Q1] << 2 : 0;
      emb[Q1] += (E_n[7] == Emax_q[Q1]) ? uoff_q[Q1] << 3 : 0;
#endif
      n_q[Q1]    = emb[Q1] + (rho_q[Q1] << 4) + (c_q[Q1] << 8);
      CxtVLC[Q1] = enc_CxtVLC_table1[n_q[Q1]];
      emb_k      = CxtVLC[Q1] & 0xF;
      emb_1      = n_q[Q1] % 16 & emb_k;
      for (int i = 0; i < 4; ++i) {
        m_n[4 + i] = sigma_n[4 + i] * U_q[Q1] - ((emb_k >> i) & 1);
#ifdef MSNAIVE
        MagSgn_encoder.emitMagSgnBits(v_n[4 + i], m_n[4 + i]);
#else
        MagSgn_encoder.emitMagSgnBits(v_n[4 + i], m_n[4 + i], (emb_1 >> i) & 1);
#endif
      }

      CxtVLC[Q1] >>= 4;
      lw = CxtVLC[Q1] & 0x07;
      CxtVLC[Q1] >>= 3;
      cwd = CxtVLC[Q1];

      VLC_encoder.emitVLCBits(cwd, lw);
      encode_UVLC1(cwd, lw, u_q[Q0], u_q[Q1]);
      VLC_encoder.emitVLCBits(cwd, lw);
    }
    if (QW & 1) {
      uint16_t qx = QW - 1;
      // E_n[7] shall be saved because ep[2*qx-1] can't be changed before kappa
      // calculation
      int32_t E7     = E_n[7];
      uint8_t sigma7 = sigma_n[7];
      // context for current quad
      c_q[Q0] = (sp[2 * qx + 1] | sp[2 * qx + 2]) << 2;
      c_q[Q0] += (sigma_n[6] | sigma_n[7]) << 1;
      c_q[Q0] += sp[2 * qx - 1] | sp[2 * qx];
      make_storage_one(&block, qy, qx, QH, QW, sigma_n, v_n, E_n, rho_q);
      // MEL encoding of the first quad
      if (c_q[Q0] == 0) {
        MEL_encoder.encodeMEL((rho_q[Q0] != 0));
      }

      gamma[Q0] = (popcount32((uint32_t)rho_q[Q0]) > 1) ? 1 : 0;

      kappa = ((my_max_4(ep[2 * qx - 1], ep[2 * qx], ep[2 * qx + 1], ep[2 * qx + 2]) - 1) * gamma[Q0] > 1)
                  ? (my_max_4(ep[2 * qx - 1], ep[2 * qx], ep[2 * qx + 1], ep[2 * qx + 2]) - 1) * gamma[Q0]
                  : 1;

      ep[2 * qx] = E_n[1];
      // if (qx > 0) {
      ep[2 * qx - 1] = E7;  // put back saved E_n
      //}
      // this quad (first) is the end of the line-pair
      ep[2 * qx + 1] = E_n[3];

      sp[2 * qx] = sigma_n[1];
      // if (qx > 0) {
      sp[2 * qx - 1] = sigma7;  // put back saved E_n
      //}
      // this quad (first) is the end of the line-pair
      sp[2 * qx + 1] = sigma_n[3];

      Emax_q[Q0] = my_max_4(E_n[0], E_n[1], E_n[2], E_n[3]);
      U_q[Q0]    = (Emax_q[Q0] > kappa) ? Emax_q[Q0] : kappa;  // std::max((int32_t)Emax_q[Q0], kappa);
      u_q[Q0]    = U_q[Q0] - kappa;
      uoff_q[Q0] = (u_q[Q0]) ? 1 : 0;
#ifdef HTSIMD
      __m128i a =
          _mm_cmpeq_epi32(_mm_set_epi32(E_n[0], E_n[1], E_n[2], E_n[3]), _mm_set1_epi32(Emax_q[Q0]));
      __m128i b = _mm_sllv_epi32(_mm_set1_epi32(uoff_q[Q0]), _mm_set_epi32(0, 1, 2, 3));
      a         = _mm_and_si128(a, b);
      b         = _mm_hadd_epi32(a, a);
      a         = _mm_hadd_epi32(b, b);
      emb[Q0]   = _mm_cvtsi128_si32(a);
#else
      emb[Q0] = (E_n[0] == Emax_q[Q0]) ? uoff_q[Q0] : 0;
      emb[Q0] += (E_n[1] == Emax_q[Q0]) ? uoff_q[Q0] << 1 : 0;
      emb[Q0] += (E_n[2] == Emax_q[Q0]) ? uoff_q[Q0] << 2 : 0;
      emb[Q0] += (E_n[3] == Emax_q[Q0]) ? uoff_q[Q0] << 3 : 0;
#endif
      n_q[Q0]    = emb[Q0] + (rho_q[Q0] << 4) + (c_q[Q0] << 8);
      CxtVLC[Q0] = enc_CxtVLC_table1[n_q[Q0]];
      emb_k      = CxtVLC[Q0] & 0xF;
      emb_1      = n_q[Q0] % 16 & emb_k;
      for (int i = 0; i < 4; ++i) {
        m_n[i] = sigma_n[i] * U_q[Q0] - ((emb_k >> i) & 1);
#ifdef MSNAIVE
        MagSgn_encoder.emitMagSgnBits(v_n[i], m_n[i]);
#else
        MagSgn_encoder.emitMagSgnBits(v_n[i], m_n[i], (emb_1 >> i) & 1);
#endif
      }

      CxtVLC[Q0] >>= 4;
      lw = CxtVLC[Q0] & 0x07;
      CxtVLC[Q0] >>= 3;
      cwd = CxtVLC[Q0];

      VLC_encoder.emitVLCBits(cwd, lw);
      encode_UVLC1(cwd, lw, u_q[Q0]);
      VLC_encoder.emitVLCBits(cwd, lw);
    }
  }

  Pcup = MagSgn_encoder.termMS();
  MEL_encoder.termMEL();
  Scup = termMELandVLC(VLC_encoder, MEL_encoder);
  // memcpy(&fwd_buf[Pcup], &rev_buf[0], Scup);
  Lcup = Pcup + Scup;

  fwd_buf[Lcup - 1] = Scup >> 4;
  fwd_buf[Lcup - 2] = (fwd_buf[Lcup - 2] & 0xF0) | (Scup & 0x0f);

  // printf("Lcup %d\n", Lcup);

  // transfer Dcup[] to block.compressed_data
  // block.set_compressed_data(fwd_buf.get(), Lcup);
  // set length of compressed data
  block.length      = Lcup;
  block.pass_length = Lcup;
  // set number of coding passes
  block.num_passes = 1;
  // block.layer_passes[0] = 1;
  // block.layer_start[0]  = 0; // kuramochi
  // set number of zero-bit planes (=Zblk) // kuramochi
  block.num_ZBP = block.get_Mb() - 1;
  return block.length;
}
