#pragma once
#include <stdint.h>
#include <string.h>
#include <functional>
//#include <memory>
//#include <vector>

// NONEED: #include "buf_chain.h"
#define CBLK_WIDTH 64
#define CBLK_HEIGHT 64
typedef int32_t sprec_t;

class element_siz {
 public:
  uint32_t x;
  uint32_t y;
  element_siz() : x(0), y(0) {}
  element_siz(uint32_t x0, uint32_t y0) {
    x = x0;
    y = y0;
  }
};

/********************************************************************************
 * j2k_region
 *******************************************************************************/
class j2k_region {
 public:
  // top-left coordinate (inclusive) of a region in the reference grid
  element_siz pos0;
  // bottom-right coordinate (exclusive) of a region in the reference grid
  element_siz pos1;
  // return top-left coordinate (inclusive)
  element_siz get_pos0() const { return pos0; }
  // return bottom-right coordinate (exclusive)
  element_siz get_pos1() const { return pos1; }
  // get size of a region
  void get_size(element_siz &out) const {
    out.x = pos1.x - pos0.x;
    out.y = pos1.y - pos0.y;
  }
  // set top-left coordinate (inclusive)
  void set_pos0(element_siz in) { pos0 = in; }
  // set bottom-right coordinate (exclusive)
  void set_pos1(element_siz in) { pos1 = in; }
  j2k_region() = default;
  j2k_region(element_siz p0, element_siz p1) : pos0(p0), pos1(p1) {}
};
/********************************************************************************
 * j2k_codeblock
 *******************************************************************************/
class j2k_codeblock : public j2k_region {
 public:
  const element_siz size;

 private:
  const uint32_t index;
  const uint8_t band;
  const uint8_t M_b;
  uint8_t *compressed_data;  // kuramochi
  uint8_t *current_address;

 public:
  uint8_t *block_states;  // kuramochi
  const uint8_t R_b;
  const uint8_t transformation;
  const float stepsize;
  const uint32_t band_stride;
  const uint16_t num_layers;
  int32_t *sample_buf;  // kuramochi
  sprec_t *const i_samples;
  uint32_t length;
  uint16_t Cmodes;
  uint8_t num_passes;
  uint8_t num_ZBP;
  uint8_t fast_skip_passes;
  uint32_t Lblock;
  uint32_t pass_length;  // kuramochi
  // std::unique_ptr<uint8_t[]> layer_start;   // kuramochi
  // std::unique_ptr<uint8_t[]> layer_passes;  // kuramochi
  bool already_included;

  j2k_codeblock(const uint32_t &idx, uint8_t orientation, uint8_t M_b, uint8_t R_b, uint8_t transformation,
                float stepsize, uint32_t band_stride, sprec_t *ibuf, uint32_t offset,
                const uint16_t &numlayers, const uint8_t &codeblock_style, const element_siz &p0,
                const element_siz &p1, const element_siz &s, int32_t *g_sample_buffer,
                uint8_t *g_state_buffer, uint8_t *g_compressed_buffer)
      : j2k_region(p0, p1),
        // public
        size(s),
        // private
        index(idx),
        band(orientation),
        M_b(M_b),
        compressed_data(nullptr),      // kuramochi
        current_address(nullptr),      // kuramochi
        block_states(g_state_buffer),  // kuramochi
        // public
        R_b(R_b),
        transformation(transformation),
        stepsize(stepsize),
        band_stride(band_stride),
        num_layers(numlayers),
        sample_buf(g_sample_buffer),  // kuramochi
        i_samples(ibuf + offset),
        length(0),
        Cmodes(codeblock_style),
        num_passes(0),
        num_ZBP(0),
        fast_skip_passes(0),
        Lblock(0),
        already_included(false) {
    // memset(sample_buf.get(), 0, sizeof(int32_t) * size.x * size.y);  // kuramochi
    // memset(block_states.get(), 0, (size.x + 2) * (size.y + 2));      // kuramochi
    // this->layer_start  = std::make_unique<uint8_t[]>(num_layers);    // kuramochi
    // this->layer_passes = std::make_unique<uint8_t[]>(num_layers);    // kuramochi
    // this->pass_length.reserve(109);                                  /// kuramochi
    this->pass_length = 0;  // std::vector<uint32_t>(num_layers, 0);        // critical section//kuramochi
  }

  void modify_state(const std::function<void(uint8_t &, uint8_t)> &callback, uint8_t val,
                    int16_t j1,  // kuramochi
                    int16_t j2) {
    callback(block_states[(j1 + 1) * (size.x + 2) + (j2 + 1)], val);  // kuramochi
  }
  uint8_t get_state(const std::function<uint8_t(uint8_t &)> &callback, int16_t j1,
                    int16_t j2) const {                                 // kuramochi
    return callback(block_states[(j1 + 1) * (size.x + 2) + (j2 + 1)]);  // kuramochi
  }
  // DEBUG FUNCTION, SOON BE DELETED
  uint8_t get_orientation() const { return band; }
  uint8_t get_context_label_sig(const uint16_t &j1, const uint16_t &j2) const;
  uint8_t get_signLUT_index(const uint16_t &j1, const uint16_t &j2) const;
  uint8_t get_Mb() const { return this->M_b; }
  uint8_t *get_compressed_data();
  // void set_compressed_data(uint8_t *buf, uint16_t bufsize) {
  //   if (this->compressed_data != nullptr) {  // kuramochi
  //     exit(EXIT_FAILURE);                    // kuramochi
  //   }
  //   this->compressed_data = std::make_unique<uint8_t[]>(bufsize);  // kuramochi
  //   memcpy(this->compressed_data.get(), buf, bufsize);             // kuramochi
  //   this->current_address = this->compressed_data.get();           // kuramochi
  // }
  // NONEED: void create_compressed_buffer(buf_chain *tile_buf, uint16_t buf_limit, const uint16_t &layer);
  void update_sample(const uint8_t &symbol, const uint8_t &p, const uint16_t &j1, const uint16_t &j2) const;
  void update_sign(const int8_t &val, const uint16_t &j1, const uint16_t &j2) const;
  uint8_t get_sign(const uint16_t &j1, const uint16_t &j2) const;
  void set_MagSgn_and_sigma(uint32_t &or_val);
  void calc_mbr(uint8_t &mbr, uint16_t i, uint16_t j, uint32_t mbr_info, uint8_t causal_cond) const;
};
