/* -*- c++ -*- */
/*
 * Copyright 2004,2006,2010,2011 Free Software Foundation, Inc.
 * 
 * This file is part of GNU Radio
 * 
 * GNU Radio is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 * 
 * GNU Radio is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with GNU Radio; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <digital_ofdm_cyclic_prefixer.h>
#include <gr_io_signature.h>

static const pmt::pmt_t EOB_KEY  = pmt::pmt_string_to_symbol("tx_eob");

digital_ofdm_cyclic_prefixer_sptr
digital_make_ofdm_cyclic_prefixer (size_t input_size, size_t output_size)
{
  return gnuradio::get_initial_sptr(new digital_ofdm_cyclic_prefixer (input_size,
								      output_size));
}

digital_ofdm_cyclic_prefixer::digital_ofdm_cyclic_prefixer (size_t input_size,
							    size_t output_size)
  : gr_sync_interpolator ("ofdm_cyclic_prefixer",
			  gr_make_io_signature2 (1, 2, input_size*sizeof(gr_complex), sizeof(char)),
			  gr_make_io_signature (1, 1, sizeof(gr_complex)),
			  output_size), 
    d_input_size(input_size),
    d_output_size(output_size)

{
}

int
digital_ofdm_cyclic_prefixer::work (int noutput_items,
				    gr_vector_const_void_star &input_items,
				    gr_vector_void_star &output_items)
{
  gr_complex *in = (gr_complex *) input_items[0];
  const unsigned char *in_flag = 0;
  if (input_items.size() == 2)
    in_flag = (const unsigned char *) input_items[1];  // the flag indicates whether one packet ends
  gr_complex *out = (gr_complex *) output_items[0];
  size_t cp_size = d_output_size - d_input_size;
  unsigned int i=0, j=0;

  j = cp_size;
  for(i=0; i < d_input_size; i++,j++) {
    out[j] = in[i];
  }

  j = d_input_size - cp_size;
  for(i=0; i < cp_size; i++, j++) {
    out[i] = in[j];
  }

  if (in_flag) {
    if (in_flag[0] & 0x1){
      const pmt::pmt_t _id = pmt::pmt_string_to_symbol(this->name());
      this->add_item_tag(0, nitems_written(0)+d_output_size-1, EOB_KEY, pmt::PMT_T, _id);
      if (false)
        printf(">>> [CP] add EOB flag at %d >>> \n", nitems_written(0)+d_output_size-1);
    }
  }

  return d_output_size;
}
