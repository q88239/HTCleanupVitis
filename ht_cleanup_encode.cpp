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
  int32_t len32 = len;
  for (; len32 > 0;) {
    int32_t available_bits = 8 - (last > 0x8F) - bits;
    int32_t t =
        (available_bits < len32) ? available_bits : len32;  // std::min(available_bits, (int32_t)len);
    tmp |= (cwd & (1 << t) - 1) << bits;
    bits += t;
    available_bits -= t;
    len32 -= t;
    cwd >>= t;
    if (available_bits == 0) {
      if ((last > 0x8f) && tmp != 0x7F) {
        last = 0x00;
        continue;
      }
      buf[pos] = tmp;
      pos--;  // reverse order
      last = tmp;
      tmp  = 0;
      bits = 0;
    }
  }
  //  uint8_t b;
  //  for (; len > 0;) {
  //    b = cwd & 1;
  //    cwd >>= 1;
  //    len--;
  //    tmp |= b << bits;
  //    bits++;
  //    if ((last > 0x8F) && (tmp == 0x7F)) {
  //      bits++;
  //    }
  //    if (bits == 8) {
  //      buf[pos] = tmp;
  //      pos--;  // reverse order
  //      last = tmp;
  //      tmp  = 0;
  //      bits = 0;
  //    }
  //  }
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
  memmove(&MEL.buf[MEL.pos], &VLC.buf[VLC.pos + 1], MAX_Scup - VLC.pos - 1);
  // return Scup
  return (MEL.pos + MAX_Scup - VLC.pos - 1);
}

#define MAKE_STORAGE()                                                                                     \
  {                                                                                                        \
    const int32_t x[8] = {2 * qx,       2 * qx,       2 * qx + 1,       2 * qx + 1,                        \
                          2 * (qx + 1), 2 * (qx + 1), 2 * (qx + 1) + 1, 2 * (qx + 1) + 1};                 \
    const int32_t y[8] = {2 * qy, 2 * qy + 1, 2 * qy, 2 * qy + 1, 2 * qy, 2 * qy + 1, 2 * qy, 2 * qy + 1}; \
    for (int i = 0; i < 4; ++i)                                                                            \
      sigma_n[i] =                                                                                         \
          (block->block_states[(y[i] + 1) * (block->size.x + 2) + (x[i] + 1)] >> SHIFT_SIGMA) & 1;         \
    rho_q[0] = sigma_n[0] + (sigma_n[1] << 1) + (sigma_n[2] << 2) + (sigma_n[3] << 3);                     \
    for (int i = 4; i < 8; ++i)                                                                            \
      sigma_n[i] =                                                                                         \
          (block->block_states[(y[i] + 1) * (block->size.x + 2) + (x[i] + 1)] >> SHIFT_SIGMA) & 1;         \
    rho_q[1] = sigma_n[4] + (sigma_n[5] << 1) + (sigma_n[6] << 2) + (sigma_n[7] << 3);                     \
    for (int i = 0; i < 8; ++i) {                                                                          \
      if ((x[i] >= 0 && x[i] < (block->size.x)) && (y[i] >= 0 && y[i] < (block->size.y)))                  \
        v_n[i] = block->sample_buf[x[i] + y[i] * block->size.x];                                           \
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
  j2k_codeblock *block = new j2k_codeblock(idx, orientation, M_b, R_b, transformation, stepsize,
                                           band_stride, ibuf, offset, numlayers, codeblock_style, p0, p1, s,
                                           g_sample_buffer, g_state_buffer, g_compressed_buffer);

  // length of HT cleanup pass
  int32_t Lcup;
  // length of MagSgn buffer
  int32_t Pcup;
  // length of MEL buffer + VLC buffer
  int32_t Scup;
  // used as a flag to invoke HT Cleanup encoding
  uint32_t or_val = 0;

  const uint16_t QW = ceil_int(block->size.x, 2);
  const uint16_t QH = ceil_int(block->size.y, 2);

  block->set_MagSgn_and_sigma(or_val);

  if (!or_val) {
    // nothing to do here because this codeblock is empty
    // set length of coding passes
    block->length      = 0;
    block->pass_length = 0;
    // set number of coding passes
    block->num_passes = 0;
    // block->layer_passes = 0; // kuramochi
    // block->layer_start  = 0; // kuramochi
    // set number of zero-bitplanes (=Zblk)
    block->num_ZBP = block->get_Mb() - 1;
    return block->length;
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
  // std::unique_ptr<int32_t[]> Eadj = std::make_unique<int32_t[]>(round_up(block->size.x, 2) + 2);
  // memset(Eadj.get(), 0, round_up(block->size.x, 2) + 2);  // kuramochi
  // std::unique_ptr<uint8_t[]> sigma_adj = std::make_unique<uint8_t[]>(round_up(block->size.x, 2) + 2);
  // memset(sigma_adj.get(), 0, round_up(block->size.x, 2) + 2);  // kuramochi

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
  int32_t *p_sample = block->sample_buf;         //.get();
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
    make_storage_one(block, 0, qx, QH, QW, sigma_n, v_n, E_n, rho_q);
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
      make_storage_one(block, qy, qx, QH, QW, sigma_n, v_n, E_n, rho_q);
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

  // transfer Dcup[] to block->compressed_data
  // block->set_compressed_data(fwd_buf.get(), Lcup);
  // set length of compressed data
  block->length      = Lcup;
  block->pass_length = Lcup;
  // set number of coding passes
  block->num_passes = 1;
  // block->layer_passes[0] = 1;
  // block->layer_start[0]  = 0; // kuramochi
  // set number of zero-bit planes (=Zblk) // kuramochi
  block->num_ZBP = block->get_Mb() - 1;
  return block->length;
}

sprec_t data[4096] = {
    32,  29,  26,  24,  27,  39,  38,  5,   -35, -27, -23, -22, -23,  -16, -8,  -2,  1,   1,   1,   1,
    2,   3,   3,   4,   4,   4,   3,   1,   2,   1,   3,   3,   3,    1,   -1,  -1,  -1,  -3,  -8,  -19,
    -12, 19,  29,  22,  25,  24,  24,  23,  25,  24,  47,  90,  63,   -23, -14, -8,  -7,  -8,  -5,  -6,
    -5,  -6,  -9,  12,  27,  26,  26,  23,  32,  40,  35,  -1,  -37,  -30, -26, -25, -25, -18, -9,  -5,
    -1,  1,   1,   0,   2,   2,   2,   3,   2,   2,   3,   0,   1,    0,   3,   2,   1,   0,   -1,  -2,
    -1,  -2,  -8,  -17, -21, 6,   27,  27,  28,  28,  25,  24,  23,   22,  27,  73,  87,  12,  -22, -10,
    -9,  -9,  -8,  -7,  -4,  0,   -27, -76, 26,  27,  26,  27,  36,   36,  30,  1,   -36, -32, -24, -26,
    -27, -18, -11, -6,  -2,  0,   0,   1,   1,   2,   2,   1,   1,    1,   1,   2,   0,   -2,  4,   3,
    1,   0,   -1,  -3,  -1,  -2,  -6,  -13, -23, -7,  23,  31,  31,   32,  28,  27,  24,  22,  19,  40,
    88,  68,  -17, -16, -9,  -8,  -7,  -2,  5,   -29, -81, -83, 27,   28,  27,  33,  37,  31,  29,  1,
    -40, -32, -24, -27, -26, -19, -10, -6,  -3,  -1,  -2,  0,   0,    0,   0,   1,   3,   -2,  -6,  -3,
    -3,  -3,  0,   1,   1,   -1,  -2,  -4,  -3,  -2,  -5,  -11, -22,  -10, 19,  31,  31,  29,  28,  26,
    25,  23,  21,  18,  65,  93,  31,  -21, -9,  -10, -7,  3,   -28,  -82, -86, -78, 30,  29,  34,  38,
    35,  29,  30,  0,   -40, -32, -25, -28, -27, -20, -11, -8,  -5,   -3,  -4,  -3,  -1,  -1,  -1,  -2,
    0,   -4,  -7,  -7,  -7,  -9,  -4,  0,   1,   -3,  -3,  -5,  -5,   -2,  -5,  -11, -19, -10, 13,  26,
    29,  28,  27,  26,  26,  25,  24,  16,  29,  86,  85,  -4,  -20,  -9,  0,   -31, -84, -80, -79, -79,
    31,  32,  36,  29,  29,  31,  30,  -2,  -42, -32, -26, -27, -26,  -19, -11, -6,  -6,  -5,  -5,  -2,
    -3,  -1,  -1,  -2,  -1,  -2,  -1,  2,   7,   2,   -4,  -8,  -5,   -4,  -1,  -2,  -3,  -3,  -6,  -12,
    -18, -12, 9,   19,  24,  26,  24,  24,  25,  23,  19,  15,  11,   60,  97,  49,  -26, -10, -31, -85,
    -82, -75, -80, -84, 31,  37,  28,  12,  25,  33,  32,  0,   -42,  -34, -25, -26, -26, -16, -10, -6,
    -4,  -4,  -1,  0,   -4,  -2,  0,   -1,  11,  14,  23,  38,  49,   48,  46,  34,  9,   -11, -4,  -1,
    -4,  -5,  -8,  -13, -20, -14, 9,   14,  19,  24,  23,  22,  21,   17,  13,  12,  11,  24,  83,  93,
    19,  -38, -85, -85, -77, -77, -83, -84, 34,  36,  7,   -6,  29,   36,  33,  0,   -47, -37, -30, -29,
    -26, -17, -10, -9,  -6,  -6,  -3,  -2,  2,   6,   -3,  2,   10,   14,  22,  32,  41,  46,  53,  60,
    65,  37,  -5,  -12, -7,  -8,  -13, -17, -25, -14, 11,  14,  17,   20,  22,  16,  15,  15,  12,  10,
    11,  8,   47,  103, 56,  -86, -85, -77, -78, -79, -81, -81, 40,   25,  -29, -15, 32,  36,  33,  -1,
    -50, -41, -32, -31, -28, -23, -15, -13, -9,  -7,  -6,  -1,  -10,  -8,  -9,  0,   1,   5,   13,  25,
    39,  48,  54,  58,  62,  70,  57,  7,   -26, -17, -15, -19, -26,  -17, 13,  20,  15,  13,  16,  15,
    12,  15,  12,  12,  13,  14,  21,  70,  -30, -92, -79, -76, -79,  -81, -80, -86, 38,  0,   -49, -12,
    32,  38,  30,  -3,  -51, -40, -30, -29, -29, -23, -16, -13, -10,  -8,  -2,  -7,  -21, -14, -6,  -1,
    2,   5,   7,   20,  34,  49,  55,  61,  63,  63,  68,  72,  35,   -25, -24, -23, -27, -17, 16,  25,
    19,  -2,  -3,  17,  15,  16,  12,  14,  15,  18,  23,  -41, -92,  -82, -78, -82, -77, -78, -77, -52,
    19,  -34, -47, -11, 32,  39,  31,  -4,  -50, -40, -30, -29, -29,  -24, -16, -12, -8,  -7,  -7,  -19,
    -15, -11, -9,  -7,  1,   6,   6,   16,  33,  51,  56,  53,  59,   70,  78,  81,  95,  41,  -40, -31,
    -31, -18, 15,  26,  23,  -11, -44, 3,   17,  14,  14,  15,  14,   24,  -20, -91, -82, -77, -82, -80,
    -79, -68, -51, 15,  -15, -44, -43, -11, 29,  37,  31,  -5,  -48,  -38, -31, -28, -28, -23, -17, -12,
    -7,  -7,  -18, -18, -11, -14, -12, -3,  6,   10,  12,  14,  29,   42,  45,  63,  77,  76,  74,  76,
    81,  94,  7,   -51, -38, -21, 15,  26,  24,  -5,  -74, -29, 16,   16,  14,  14,  19,  6,   -77, -84,
    -76, -79, -79, -81, -73, -43, 7,   19,  -41, -40, -42, -13, 27,   37,  31,  -4,  -49, -38, -31, -30,
    -27, -23, -15, -11, -6,  -12, -18, -17, -14, -13, -5,  3,   7,    11,  10,  9,   20,  40,  65,  72,
    70,  69,  72,  75,  77,  82,  85,  13,  -56, -26, 16,  25,  25,   -4,  -77, -67, -3,  16,  11,  15,
    21,  -55, -91, -75, -78, -81, -78, -79, -59, 2,   19,  30,  -40,  -37, -42, -11, 28,  36,  31,  -3,
    -48, -41, -32, -29, -29, -22, -15, -13, 6,   -23, -20, -17, -15,  -8,  2,   9,   8,   9,   -5,  6,
    43,  63,  63,  61,  64,  72,  74,  75,  76,  76,  82,  92,  -16,  -38, 14,  25,  25,  -4,  -72, -90,
    -52, 7,   18,  18,  -22, -89, -80, -74, -79, -79, -80, -76, -8,   14,  26,  33,  -39, -40, -45, -15,
    27,  35,  31,  -3,  -50, -43, -34, -31, -28, -23, -19, -8,  18,   -28, -19, -18, -13, -3,  5,   4,
    3,   -10, 8,   48,  56,  55,  53,  63,  67,  68,  70,  72,  73,   75,  78,  88,  63,  -42, 10,  24,
    25,  -2,  -71, -96, -44, 58,  78,  69,  -65, -85, -74, -75, -77,  -78, -83, -29, 17,  22,  32,  29,
    -39, -38, -40, -16, 27,  37,  31,  -5,  -53, -45, -33, -31, -29,  -25, -22, 13,  16,  -30, -20, -16,
    -8,  -4,  -4,  -1,  -11, 16,  45,  43,  46,  50,  57,  60,  61,   62,  67,  70,  72,  73,  75,  77,
    88,  7,   1,   27,  26,  -10, -84, -22, 63,  81,  87,  99,  -28,  -88, -75, -82, -81, -80, -52, 17,
    21,  31,  29,  27,  -33, -31, -34, -14, 27,  40,  35,  -2,  -50,  -40, -30, -29, -27, -22, -22, 31,
    3,   -32, -23, -15, -10, -7,  -1,  -9,  15,  33,  34,  43,  50,   53,  54,  49,  53,  63,  66,  68,
    67,  60,  71,  74,  75,  72,  26,  22,  17,  -9,  19,  70,  77,   78,  75,  95,  -19, -87, -78, -84,
    -81, -81, 0,   22,  30,  28,  27,  26,  -30, -30, -33, -11, 27,   40,  39,  2,   -48, -40, -30, -29,
    -30, -24, -25, 38,  -1,  -29, -26, -20, -11, -9,  -10, 12,  22,   17,  33,  44,  43,  47,  42,  46,
    51,  52,  58,  61,  55,  56,  66,  67,  65,  68,  60,  23,  40,   63,  74,  75,  73,  73,  81,  92,
    -41, -84, -80, -83, -81, -37, 22,  26,  31,  28,  26,  24,  -30,  -31, -32, -10, 29,  43,  43,  3,
    -49, -41, -32, -31, -31, -27, -23, 53,  8,   -21, -25, -18, -10,  -8,  12,  16,  9,   20,  28,  38,
    40,  38,  43,  46,  44,  49,  53,  49,  56,  60,  57,  53,  54,   47,  56,  59,  70,  73,  73,  70,
    74,  67,  80,  78,  -71, -82, -82, -84, -60, 12,  22,  31,  29,   27,  26,  25,  -33, -34, -34, -12,
    30,  43,  41,  3,   -48, -39, -30, -31, -32, -29, -21, 55,  21,   -4,  -25, -21, -13, 5,   11,  3,
    18,  19,  19,  30,  35,  41,  39,  40,  38,  38,  42,  49,  54,   50,  48,  42,  42,  52,  63,  68,
    69,  68,  69,  73,  68,  39,  59,  29,  -86, -81, -83, -79, -18,  23,  28,  30,  29,  27,  28,  27,
    -34, -34, -34, -10, 31,  43,  40,  5,   -48, -38, -30, -33, -31,  -32, -23, 53,  29,  7,   -21, -21,
    4,   4,   -2,  12,  12,  15,  19,  26,  32,  33,  30,  33,  31,   35,  38,  41,  42,  36,  34,  43,
    55,  65,  67,  65,  64,  70,  72,  78,  49,  25,  43,  -30, -88,  -79, -84, -48, 18,  24,  31,  28,
    28,  27,  28,  26,  -32, -32, -30, -7,  31,  43,  41,  6,   -49,  -39, -32, -32, -31, -34, -21, 67,
    40,  22,  -7,  -7,  1,   -4,  9,   7,   3,   12,  18,  9,   0,    -1,  11,  9,   24,  27,  28,  13,
    15,  30,  46,  61,  66,  61,  62,  68,  71,  74,  81,  68,  7,    -9,  45,  -74, -83, -78, -76, -9,
    22,  28,  28,  26,  26,  26,  26,  25,  -31, -31, -28, -4,  32,   44,  42,  6,   -51, -41, -31, -31,
    -30, -33, -26, 69,  49,  13,  -3,  -13, -11, 6,   3,   -1,  3,    9,   5,   -4,  -22, -43, 6,   -18,
    -36, -35, -61, -74, -11, 48,  63,  67,  65,  64,  67,  71,  78,   68,  32,  -16, -34, 36,  -11, -90,
    -83, -84, -57, 15,  26,  32,  29,  27,  25,  24,  24,  24,  -27,  -29, -28, -4,  32,  43,  41,  6,
    -49, -41, -33, -31, -30, -32, -36, 57,  58,  5,   1,   -18, 1,    1,   -4,  1,   6,   -1,  -8,  -26,
    -46, -62, -31, -44, -41, -68, -83, -20, 38,  59,  64,  60,  62,   63,  70,  67,  40,  -20, -36, -8,
    43,  -14, -85, -81, -82, -79, -20, 22,  33,  31,  31,  29,  26,   25,  25,  23,  -24, -27, -28, -6,
    31,  41,  41,  7,   -50, -42, -32, -31, -32, -31, -39, 25,  69,   27,  -3,  -7,  -2,  -8,  -4,  -2,
    -3,  -17, -50, -64, -81, -55, -53, -47, -61, -59, -8,  43,  57,   52,  52,  55,  60,  63,  30,  -18,
    -15, 15,  33,  56,  -12, -90, -77, -80, -83, -63, 10,  27,  36,   33,  31,  29,  27,  25,  25,  22,
    -23, -25, -31, -10, 31,  43,  42,  9,   -51, -43, -34, -32, -32,  -29, -29, -16, 67,  45,  -3,  -4,
    -8,  -5,  -3,  -3,  -39, -52, -68, -63, -78, -68, -70, -53, -45,  -16, 44,  63,  52,  50,  46,  53,
    60,  47,  -42, -31, -8,  24,  47,  -26, -89, -80, -75, -82, -80,  -36, 12,  34,  35,  34,  33,  30,
    29,  27,  25,  22,  -23, -26, -35, -16, 30,  43,  44,  9,   -51,  -46, -33, -32, -32, -26, -20, -34,
    38,  63,  -1,  -13, -6,  -4,  -6,  -35, -61, -49, -76, -60, -68,  -80, -76, -63, -27, 19,  56,  49,
    45,  45,  45,  58,  64,  66,  -4,  -51, -22, 20,  -41, -93, -78,  -73, -78, -84, -70, -11, 13,  38,
    34,  33,  32,  31,  31,  29,  27,  22,  -25, -26, -35, -18, 28,   44,  44,  12,  -50, -43, -31, -30,
    -31, -26, -17, -27, 33,  49,  -18, -13, -8,  -5,  -45, -67, -84,  -64, -61, -65, -45, -85, -77, -18,
    9,   47,  54,  37,  42,  38,  56,  69,  70,  77,  25,  -71, -52,  13,  -67, -85, -74, -69, -80, -85,
    -46, 6,   20,  31,  33,  32,  33,  33,  30,  28,  27,  23,  -26,  -27, -34, -18, 29,  44,  44,  13,
    -46, -43, -30, -27, -30, -20, -10, -19, 11,  28,  -21, -6,  -12,  -54, -74, -69, -79, -78, -69, -55,
    -50, -81, -10, 39,  31,  45,  49,  33,  32,  47,  64,  71,  75,   83,  52,  -57, -81, 5,   -59, -79,
    -71, -70, -82, -81, -17, 18,  28,  21,  22,  24,  27,  30,  30,   27,  26,  22,  -27, -30, -36, -19,
    28,  45,  44,  11,  -45, -39, -29, -26, -28, -21, -13, -14, -9,   7,   -6,  -1,  -47, -70, -80, -66,
    -71, -77, -73, -65, -67, -17, 28,  52,  40,  46,  37,  30,  40,   51,  64,  69,  75,  83,  71,  -28,
    -89, -14, -50, -77, -69, -76, -81, -63, 5,   27,  34,  29,  28,   22,  16,  18,  19,  21,  23,  22,
    -28, -30, -35, -19, 29,  45,  46,  12,  -47, -42, -29, -27, -27,  -22, -14, -5,  -13, -28, 5,   -28,
    -65, -63, -77, -65, -78, -75, -67, -73, -50, 23,  40,  49,  54,   7,   5,   23,  47,  49,  58,  64,
    68,  80,  70,  -26, -93, -27, -33, -72, -66, -78, -80, -35, 11,   30,  30,  30,  34,  31,  23,  19,
    15,  13,  13,  14,  -28, -28, -35, -22, 28,  45,  47,  14,  -46,  -41, -27, -29, -29, -26, -13, 16,
    -19, -44, -22, -58, -58, -53, -75, -72, -74, -81, -64, -66, 13,   52,  49,  43,  7,   -5,  -17, -39,
    -4,  37,  50,  62,  61,  25,  -17, -41, -84, -43, -21, -71, -71,  -80, -74, -13, 16,  31,  27,  25,
    27,  29,  26,  25,  25,  20,  14,  5,   -31, -32, -39, -27, 28,   46,  46,  13,  -46, -43, -31, -31,
    -35, -7,  18,  23,  6,   -52, -63, -47, -41, -65, -64, -81, -59,  -77, -88, -12, 45,  67,  35,  -48,
    -71, -69, -54, -53, -8,  14,  44,  71,  2,   -58, -67, -68, -81,  -47, -11, -71, -76, -82, -64, 5,
    24,  27,  24,  25,  25,  25,  24,  23,  24,  19,  14,  7,   -33,  -35, -47, -32, 28,  48,  48,  13,
    -46, -40, -30, -29, -47, -8,  43,  5,   -59, -69, -61, -62, -32,  -77, -76, -69, -83, -83, -58, 26,
    60,  55,  -21, -68, -57, -44, 52,  -24, -17, 5,   51,  51,  -58,  -38, -15, -83, -86, -46, -7,  -63,
    -79, -83, -39, 16,  28,  25,  22,  23,  23,  23,  22,  19,  16,   8,   2,   10,  -29, -33, -48, -34,
    29,  50,  51,  16,  -41, -34, -19, -35, -36, 7,   14,  -58, -53,  -58, -66, -44, -19, -59, -87, -80,
    -86, -84, -4,  52,  52,  -3,  4,   -3,  -16, 7,   42,  20,  -8,   -1,  56,  49,  -4,  5,   -29, -73,
    -80, -52, 0,   -62, -83, -78, -15, 20,  26,  23,  22,  25,  23,   22,  17,  12,  6,   13,  34,  51,
    -30, -38, -54, -41, 28,  50,  53,  19,  -41, -34, -21, -7,  5,    -35, -71, -68, -24, -59, -67, -39,
    -18, -52, -88, -82, -89, -42, 36,  53,  -17, -3,  21,  27,  20,   19,  27,  27,  3,   -2,  47,  72,
    25,  -2,  -25, -52, -80, -57, 8,   -67, -83, -68, 1,   23,  24,   22,  21,  20,  20,  16,  11,  3,
    29,  58,  63,  60,  -40, -46, -61, -45, 26,  48,  51,  18,  -42,  -35, -11, 1,   -17, -51, -64, -30,
    -31, -80, -73, -40, -36, -35, -76, -86, -71, 4,   60,  -26, -27,  2,   21,  37,  45,  45,  46,  29,
    11,  -2,  37,  77,  36,  12,  -1,  -43, -78, -66, 10,  -71, -87,  -50, 12,  27,  24,  22,  21,  18,
    16,  12,  5,   30,  66,  65,  61,  63,  -51, -54, -64, -46, 26,   47,  49,  19,  -42, -34, -16, -18,
    -38, -49, -56, -29, -55, -81, -82, -73, -80, -21, -49, -96, -38,  49,  2,   -72, -15, 0,   15,  31,
    46,  47,  41,  25,  12,  -1,  29,  77,  34,  15,  0,   -50, -76,  -68, 9,   -65, -84, -26, 23,  29,
    22,  21,  19,  16,  14,  6,   15,  62,  64,  64,  69,  73,  -52,  -54, -70, -50, 24,  44,  45,  17,
    -46, -24, -2,  -32, -29, -47, -51, -19, -46, -79, -89, -65, -57,  -2,  -37, -82, 5,   42,  -78, -63,
    -18, 1,   13,  25,  35,  40,  35,  17,  1,   -7,  19,  78,  34,   13,  -3,  -66, -72, -74, 6,   -54,
    -80, -4,  27,  27,  23,  20,  17,  14,  10,  4,   46,  65,  64,   72,  78,  74,  -48, -55, -75, -57,
    24,  44,  48,  18,  -44, -25, -11, -45, -37, -52, -45, -24, -42,  -81, -86, -52, -25, -44, -85, -29,
    31,  -43, -92, -52, -22, -1,  9,   19,  24,  30,  29,  5,   -1,   -1,  11,  76,  35,  12,  -16, -82,
    -67, -80, -4,  -38, -66, 14,  27,  23,  21,  20,  18,  15,  6,    20,  65,  64,  72,  78,  76,  77,
    -46, -53, -69, -44, 26,  43,  49,  20,  -36, -33, -21, -47, -40,  -52, -49, -30, -20, -71, -69, -76,
    -47, -86, -60, 48,  -3,  -89, -81, -41, -20, -3,  6,   17,  22,   26,  27,  8,   -9,  -24, -7,  50,
    25,  8,   -48, -87, -64, -80, -19, -22, -43, 22,  27,  21,  20,   19,  16,  14,  4,   41,  68,  70,
    77,  77,  79,  78,  -53, -53, -62, -22, 29,  41,  48,  22,  -42,  -39, -16, -56, -49, -53, -51, -29,
    -8,  -53, -38, -75, -51, -10, -6,  10,  -74, -78, -77, -37, -16,  -3,  5,   13,  17,  24,  28,  22,
    8,   16,  36,  50,  20,  -5,  -74, -81, -57, -76, -38, -14, -13,  24,  25,  21,  19,  17,  16,  12,
    8,   53,  68,  77,  77,  79,  80,  79,  -57, -60, -64, -19, 30,   41,  48,  24,  -44, -34, -12, -49,
    -44, -71, -60, -48, -31, -14, -55, -65, 3,   29,  -17, -56, -82,  -71, -76, -32, -17, -1,  4,   10,
    11,  19,  22,  20,  22,  55,  68,  53,  15,  -37, -84, -72, -56,  -74, -57, -3,  6,   24,  23,  19,
    19,  15,  15,  9,   10,  60,  74,  78,  77,  78,  81,  81,  -68,  -65, -65, -21, 32,  44,  51,  23,
    -38, -25, -19, -57, -59, -68, -44, -41, -49, -26, -44, 17,  48,   -9,  -43, -80, -77, -71, -75, -47,
    -26, -2,  2,   9,   11,  -6,  -5,  1,   -2,  8,   18,  6,   0,    -73, -83, -70, -58, -71, -65, 6,
    15,  25,  22,  19,  18,  16,  15,  7,   12,  67,  80,  79,  78,   81,  84,  83,  -74, -70, -63, -15,
    33,  47,  50,  24,  -22, -29, -16, -60, -74, -63, -26, -34, -34,  -45, -9,  31,  19,  -13, -77, -83,
    -79, -71, -72, -57, -38, -9,  -5,  5,   15,  2,   -14, -9,  5,    23,  18,  8,   -36, -85, -83, -69,
    -53, -67, -65, 13,  20,  25,  23,  21,  19,  16,  16,  7,   18,   76,  82,  80,  81,  83,  82,  80,
    -78, -76, -62, -20, 28,  47,  51,  26,  -45, -18, -41, -48, -75,  -57, -9,  -27, -36, -15, 1,   26,
    19,  -64, -78, -76, -76, -69, -70, -62, -54, -25, -13, -1,  8,    12,  2,   -2,  2,   10,  17,  -2,
    -78, -82, -77, -70, -51, -61, -65, 16,  25,  25,  24,  23,  21,   17,  14,  2,   20,  85,  84,  84,
    84,  80,  77,  76,  -81, -82, -50, -16, 22,  46,  50,  23,  -46,  -26, -47, -60, -76, -67, -14, -11,
    -20, -24, -25, 27,  -42, -83, -76, -77, -77, -76, -71, -72, -67,  -49, -23, -5,  5,   9,   12,  18,
    26,  26,  21,  -49, -86, -77, -74, -69, -48, -58, -65, 19,  25,   27,  24,  23,  21,  16,  12,  -2,
    21,  89,  85,  84,  83,  80,  80,  83,  -83, -83, -52, -14, 24,   46,  49,  23,  -49, -43, -60, -57,
    -71, -74, -29, -47, -9,  -9,  -8,  -37, -77, -77, -73, -76, -76,  -80, -74, -72, -69, -70, -65, -30,
    -8,  7,   19,  34,  38,  36,  4,   -76, -77, -71, -77, -63, -45,  -55, -61, 19,  22,  23,  21,  22,
    20,  14,  9,   -6,  33,  91,  83,  84,  83,  80,  70,  37,  -83,  -87, -67, -22, 25,  43,  46,  18,
    -18, -26, -68, -62, -69, -85, -14, -39, -39, -13, 6,   -15, -57,  -82, -65, -74, -77, -79, -79, -70,
    -71, -63, -56, -34, -16, -2,  11,  26,  31,  29,  14,  -52, -78,  -78, -80, -60, -42, -51, -53, 15,
    -5,  -5,  4,   9,   15,  14,  6,   -9,  52,  87,  81,  81,  76,   31,  -47, -79, -51, -76, -76, -48,
    18,  42,  44,  32,  -21, -45, -80, -62, -70, -64, -41, -20, -30,  -21, -23, -23, -3,  -66, -66, -72,
    -76, -79, -79, -74, -75, -67, -21, -1,  1,   6,   12,  16,  16,   37,  65,  66,  22,  -46, -89, -61,
    -45, -53, -44, 13,  -2,  -8,  -15, -21, -18, -12, -11, -16, 59,   79,  78,  79,  28,  -66, -78, -51,
    5,   -23, -59, -68, 9,   41,  45,  25,  -12, -57, -93, -54, -57,  -41, -65, -35, -28, -42, -18, -2,
    -26, -42, -68, -74, -75, -78, -75, -75, -66, -69, -24, -8,  -2,   6,   11,  9,   20,  42,  60,  66,
    78,  75,  -2,  -71, -54, -57, -46, 15,  14,  10,  3,   -7,  -21,  -33, -22, -31, 54,  81,  81,  29,
    -60, -61, -46, -44, 0,   19,  -20, -68, 10,  45,  44,  33,  5,    -79, -84, -65, -56, -62, -63, -58,
    -57, -54, -22, 7,   -27, -17, -59, -76, -72, -76, -72, -73, -49,  -64, -26, -8,  1,   9,   10,  13,
    27,  38,  49,  56,  65,  77,  89,  17,  -73, -65, -47, 18,  18,   15,  12,  7,   -1,  -9,  -6,  -4,
    72,  86,  65,  -44, -52, -37, -28, -39, -57, 24,  15,  -63, 11,   48,  47,  33,  -19, -88, -77, -74,
    -78, -55, -81, -64, -53, -51, -34, -6,  0,   -34, -34, -82, -72,  -79, -70, -78, -33, -57, -33, 0,
    6,   10,  11,  16,  25,  33,  42,  50,  61,  71,  80,  92,  -20,  -87, -44, 18,  17,  14,  12,  10,
    5,   -3,  1,   31,  84,  86,  24,  -49, -34, -24, -31, -30, -88,  14,  26,  -68, 9,   49,  50,  30,
    -23, -85, -77, -84, -68, -48, -80, -70, -65, -39, -40, -15, -2,   -53, -22, -79, -66, -81, -76, -73,
    -27, -52, -29, 2,   5,   9,   10,  16,  21,  29,  38,  49,  59,   69,  78,  90,  61,  -89, -39, 21,
    18,  15,  11,  6,   1,   -5,  37,  72,  87,  74,  -14, -40, -23,  -28, -34, -26, -95, 3,   30,  -69,
    6,   50,  51,  33,  -37, -82, -84, -86, -48, -58, -78, -80, -81,  -58, -38, -17, 2,   -7,  -27, -73,
    -71, -76, -79, -55, -9,  -45, -24, 5,   8,   7,   8,   13,  18,   27,  34,  44,  55,  66,  77,  82,
    92,  -47, -36, 21,  16,  13,  9,   4,   -6,  10,  82,  83,  89,   41,  -34, -32, -19, -35, -27, -31,
    -98, -5,  35,  -61, 4,   48,  45,  31,  -37, -84, -84, -76, -63,  -63, -76, -83, -77, -68, -51, 0,
    -22, -28, -43, -47, -53, -75, -76, -32, -3,  -46, -16, 4,   10,   8,   6,   10,  15,  23,  30,  40,
    53,  64,  74,  80,  93,  7,   -27, 21,  14,  10,  10,  5,   -7,   25,  84,  82,  78,  7,   -28, -27,
    -28, -34, -30, -41, -97, -20, 36,  -51, 2,   48,  49,  35,  -33,  -85, -78, -71, -75, -71, -77, -79,
    -73, -59, -49, -14, -24, -44, -77, -69, -61, -78, -63, -17, -7,   -46, -4,  8,   9,   9,   6,   9,
    14,  21,  28,  36,  47,  61,  71,  78,  87,  53,  -8,  17,  14,   14,  12,  6,   -1,  18,  60,  84,
    59,  -12, -29, -35, -40, -30, -36, -36, -96, -32, 36,  -40, -3,   46,  48,  32,  -23, -82, -76, -79,
    -74, -70, -73, -78, -71, -57, -50, -54, -53, -17, -66, -81, -84,  -82, -44, -8,  -26, -40, 5,   10,
    13,  11,  8,   7,   13,  19,  24,  31,  42,  56,  67,  77,  83,   83,  4,   -32, -14, 2,   10,  11,
    15,  7,   24,  87,  39,  -29, -37, -45, -34, -34, -35, -47, -100, -42, 46,  -30, -8,  44,  49,  31,
    -27, -77, -86, -78, -73, -71, -73, -84, -78, -56, -14, -51, -50,  -67, -83, -85, -88, -66, -21, -11,
    -58, -16, 6,   10,  12,  12,  9,   6,   10,  15,  21,  28,  38,   49,  63,  75,  81,  91,  9,   -53,
    -47, -49, -48, -35, 8,   23,  49,  83,  -7,  -54, -45, -38, -34,  -36, -33, -67, -64, -30, 49,  -13,
    -8,  44,  51,  28,  -33, -80, -81, -79, -78, -72, -70, -81, -82,  -79, -42, -5,  -32, -81, -85, -90,
    -82, -39, -10, -49, -46, 5,   8,   8,   10,  13,  11,  7,   7,    11,  19,  25,  33,  45,  58,  70,
    79,  89,  46,  -35, -27, -43, -68, -93, -18, 59,  70,  40,  -61,  -60, -44, -31, -46, -29, -40, -71,
    -40, -6,  47,  -1,  -8,  45,  53,  26,  -38, -82, -81, -80, -79,  -73, -65, -75, -72, -69, -66, -28,
    -42, -82, -87, -88, -58, -33, -53, -53, -7,  4,   8,   9,   10,   14,  14,  11,  9,   9,   14,  21,
    30,  39,  51,  65,  76,  84,  73,  -28, -25, -31, -44, -65, -10,  76,  29,  -52, -67, -50, -31, -52,
    -42, -24, -47, -63, -63, -20, 38,  6,   -7,  44,  54,  29,  -56,  -86, -75, -72, -76, -67, -59, -44,
    -69, -63, -57, -23, -49, -87, -88, -83, -65, -55, -33, -7,  -2,   2,   6,   9,   11,  14,  16,  14,
    11,  10,  13,  18,  26,  35,  47,  60,  71,  80,  86,  -9,  -38,  -25, -28, -28, -8,  16,  -45, -57,
    -52, -30, -48, -57, -22, -26, -59, -69, -76, -49, 45,  32,  -4,   45,  55,  20,  -69, -81, -75, -69,
    -69, -64, -68, -35, -58, -59, -59, -43, -46, -89, -84, -58, -29,  -15, -5,  -2,  -1,  1,   5,   7,
    9,   12,  14,  16,  15,  14,  14,  17,  23,  32,  43,  54,  65,   75,  87,  20,  -47, -24, -16, -12,
    -18, -31, -26, -39, -38, -49, -59, -32, -12, -43, -71, -75, -80,  -62, 55,  54,  0,   42,  54,  15,
    -73, -81, -72, -63, -64, -48, -65, -50, -45, -50, -69, -65, -46,  -60, -64, -32, -14, -9,  -4,  -3,
    -1,  1,   4,   5,   8,   11,  14,  15,  16,  18,  18,  20,  25,   31,  38,  50,  61,  71,  82,  53,
    -41, -33, -15, -5,  -11, -4,  -16, -40, -45, -48, -39, -3,  -31,  -74, -74, -44,
};

uint8_t g_compressed_buffer[65536]                           = {0};
int32_t g_sample_buffer[CBLK_WIDTH * CBLK_HEIGHT]            = {0};
uint8_t g_state_buffer[(CBLK_WIDTH + 2) * (CBLK_HEIGHT + 2)] = {0};

int main() {
  int retval              = 0;
  uint8_t ROIshift        = 0;
  uint32_t idx            = 0;
  uint8_t orientation     = 0;
  uint8_t M_b             = 7;
  uint8_t R_b             = 49;
  uint8_t transformation  = 1;
  float stepsize          = 1.0;
  uint32_t band_stride    = 64;
  uint32_t offset         = 0;
  uint16_t numlayers      = 1;
  uint8_t codeblock_style = 64;
  element_siz p0(0, 0);
  element_siz p1(64, 64);
  element_siz s(64, 64);

  int32_t length = htj2k_encode(idx, orientation, M_b, R_b, transformation, stepsize, band_stride, data,
                                offset, numlayers, codeblock_style, p0, p1, s, g_sample_buffer,
                                g_state_buffer, g_compressed_buffer);

  if (length == 3921) {
    printf("OK!\n");
  } else {
    retval = 1;
    printf("NG!\n");
  }
  return retval;
}
