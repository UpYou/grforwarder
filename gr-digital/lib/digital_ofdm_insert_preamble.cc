/* -*- c++ -*- */
/*
 * Copyright 2007,2010-2012 Free Software Foundation, Inc.
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <digital_ofdm_insert_preamble.h>
#include <gr_io_signature.h>
#include <stdexcept>
#include <iostream>
#include <string.h>

#define DEBUG 0

static const pmt::pmt_t SOB_KEY  = pmt::pmt_string_to_symbol("tx_sob");
static const pmt::pmt_t TIME_KEY = pmt::pmt_string_to_symbol("tx_time");
static const pmt::pmt_t SYNC_TIME = pmt::pmt_string_to_symbol("sync_time");

digital_ofdm_insert_preamble_sptr
digital_make_ofdm_insert_preamble(int fft_length,
				  const std::vector<std::vector<gr_complex> > &preamble)
{
  return gnuradio::get_initial_sptr(new digital_ofdm_insert_preamble(fft_length,
								     preamble));
}

digital_ofdm_insert_preamble::digital_ofdm_insert_preamble
       (int fft_length,
	const std::vector<std::vector<gr_complex> > &preamble)
  : gr_block("ofdm_insert_preamble",
	     gr_make_io_signature3(3, 3,
				   sizeof(gr_complex)*fft_length,
				   sizeof(char), sizeof(char)),
	     gr_make_io_signature3(3, 3,
				   sizeof(gr_complex)*fft_length,
				   sizeof(char), sizeof(char))),
    d_fft_length(fft_length),
    d_preamble(preamble),
    d_state(ST_IDLE),
    d_nsymbols_output(0),
    d_pending_flag(0)
{
  // sanity check preamble symbols
  for(size_t i = 0; i < d_preamble.size(); i++) {
    if(d_preamble[i].size() != (size_t) d_fft_length)
      throw std::invalid_argument("digital_ofdm_insert_preamble: invalid length for preamble symbol");
  }

  enter_idle();
}


digital_ofdm_insert_preamble::~digital_ofdm_insert_preamble()
{
}

void digital_ofdm_insert_preamble::forecast (int noutput_items, gr_vector_int &ninput_items_required)
{
  ninput_items_required[0] = noutput_items;
}

int
digital_ofdm_insert_preamble::general_work(int noutput_items,
					   gr_vector_int &ninput_items_v,
					   gr_vector_const_void_star &input_items,
					   gr_vector_void_star &output_items)
{
  const pmt::pmt_t _id = pmt::pmt_string_to_symbol(this->name());
 
  if (DEBUG) {
    std::vector<gr_tag_t> rx_tags;
    this->get_tags_in_range(rx_tags, 0, nitems_read(0), nitems_read(0)+input_items.size(), SYNC_TIME);
    // See if there is a RX timestamp (only on first block or after underrun)
    if(rx_tags.size()>0) {
      size_t t = rx_tags.size()-1;
      const uint64_t my_tag_count = rx_tags[t].offset;
      std::cout<<">>> [PR_SYNC ] tag count: "<< my_tag_count << " Range: ["<<nitems_read(0) << ":" <<nitems_read(0)+input_items.size() <<") \n";
    }
  }

  int ninput_items = std::min(ninput_items_v[0], ninput_items_v[1]);
  const gr_complex *in_sym = (const gr_complex *) input_items[0];
  const unsigned char *in_flag = (const unsigned char *) input_items[1];
  const unsigned char *in_flag2 = (const unsigned char *) input_items[2];

  gr_complex *out_sym = (gr_complex *) output_items[0];
  unsigned char *out_flag = (unsigned char *) output_items[1];
  unsigned char *out_flag2 = (unsigned char *) output_items[2];

  int no = 0;	// number items output
  int ni = 0;	// number items read from input


#define write_out_flag() 			\
  do { if (out_flag) 				\
          out_flag[no] = d_pending_flag; 	\
       d_pending_flag = 0; 			\
       if (out_flag2)                           \
          out_flag2[no] = in_flag2[ni];         \
  } while(0)


  while (no < noutput_items && ni < ninput_items){
    switch(d_state){
    case ST_IDLE:
      if (in_flag && in_flag[ni] & 0x1)	// this is first symbol of new payload
	enter_preamble();
      else
	ni++;			// eat one input symbol
      break;
      
    case ST_PREAMBLE:
      assert(!in_flag || in_flag[ni] & 0x1);
      if (d_nsymbols_output >= (int) d_preamble.size()){
	// we've output all the preamble
	enter_first_payload();
      }
      else {
	memcpy(&out_sym[no * d_fft_length],
	       &d_preamble[d_nsymbols_output][0],
	       d_fft_length*sizeof(gr_complex));

        /* -------------------------------------------------------- */
        // add by lzyou: get sync time, then add SOB, TIME tags
        std::vector<gr_tag_t> rx_sync_tags;
        // NOTE: we must add ni
        this->get_tags_in_range(rx_sync_tags, 0, nitems_read(0)+ni, nitems_read(0)+ni+input_items.size(), SYNC_TIME);
        
        if(rx_sync_tags.size()>0) {
            size_t t = rx_sync_tags.size()-1;
            const pmt::pmt_t &value = rx_sync_tags[t].value;
	    uint64_t sync_secs = pmt::pmt_to_uint64(pmt_tuple_ref(value, 0));
	    double sync_frac_of_secs = pmt::pmt_to_double(pmt_tuple_ref(value,1));

            // NOTE: we must add no
            this->add_item_tag(0, nitems_written(0)+no, SOB_KEY, pmt::PMT_T, _id);
            this->add_item_tag(0, nitems_written(0)+no, TIME_KEY, value, _id);
            if (DEBUG) {
                std::cout << ">>> [PREAMBLE] timestamp received at " << rx_sync_tags[t].offset << " | "<<(double)(sync_secs+sync_frac_of_secs) << std::endl;
                std::cout << ">>> [PREAMBLE] add SOB, TIME tags at " << nitems_written(0)+no << " | " << sync_secs+sync_frac_of_secs << std::endl;
                std::cout << ">>> [PREAMBLE] no = " << no << " | ni = " << ni << std::endl;
            }
        } else {
            if (DEBUG) {
                std::cout << ">>> [PREAMBLE] No Timestamp??? Range: ["<<nitems_read(0) << ":" <<nitems_read(0)+input_items.size() <<") \n";
                std::cout << ">>> [PREAMBLE] no = " << no << " | ni = " << ni  << " | d_nsymbols_output = " << d_nsymbols_output << " | " << (int) d_preamble.size() << std::endl;
            }
        }
        /* -------------------------------------------------------- */

	write_out_flag();
	no++;
	d_nsymbols_output++;
      }
      break;
      
    case ST_FIRST_PAYLOAD:
      // copy first payload symbol from input to output
      memcpy(&out_sym[no * d_fft_length],
	     &in_sym[ni * d_fft_length],
	     d_fft_length * sizeof(gr_complex));

      write_out_flag();
      no++;
      ni++;
      enter_payload();
      break;
      
    case ST_PAYLOAD:
      if (in_flag && in_flag[ni] & 0x1){	// this is first symbol of a new payload
	enter_preamble();
	break;
      }

      // copy a symbol from input to output
      memcpy(&out_sym[no * d_fft_length],
	     &in_sym[ni * d_fft_length],
	     d_fft_length * sizeof(gr_complex));

      write_out_flag();
      no++;
      ni++;
      break;

    default:
      std::cerr << "digital_ofdm_insert_preamble: (can't happen) invalid state, resetting\n";
      enter_idle();
    }
  }

  consume_each(ni);
  return no;
}

void
digital_ofdm_insert_preamble::enter_idle()
{
  d_state = ST_IDLE;
  d_nsymbols_output = 0;
  d_pending_flag = 0;
}

void
digital_ofdm_insert_preamble::enter_preamble()
{
  d_state = ST_PREAMBLE;
  d_nsymbols_output = 0;
  d_pending_flag = 1;
}

void
digital_ofdm_insert_preamble::enter_first_payload()
{
  d_state = ST_FIRST_PAYLOAD;
}

void
digital_ofdm_insert_preamble::enter_payload()
{
  d_state = ST_PAYLOAD;
}
