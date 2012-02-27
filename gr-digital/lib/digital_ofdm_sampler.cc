/* -*- c++ -*- */
/*
 * Copyright 2007,2008,2010,2011 Free Software Foundation, Inc.
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

#include <iostream>
#include <digital_ofdm_sampler.h>
#include <gr_io_signature.h>
#include <gr_expj.h>
#include <cstdio>
#include <boost/foreach.hpp>
#include <boost/format.hpp>

static const pmt::pmt_t TIME_KEY = pmt::pmt_string_to_symbol("rx_time");
static const pmt::pmt_t SYNC_TIME = pmt::pmt_string_to_symbol("sync_time");
static const pmt::pmt_t SYNC_CFO = pmt::pmt_string_to_symbol("sync_cfo");

#define VERBOSE 0
#define MY_DEBUG 0
    
// Keep track of the RX timestamp
double lts_frac_of_secs;
uint64_t lts_secs;
uint64_t lts_samples_since;

digital_ofdm_sampler_sptr
digital_make_ofdm_sampler (unsigned int fft_length, 
         unsigned int symbol_length,
         unsigned int bandwidth,
         unsigned int timeout)
{
  return gnuradio::get_initial_sptr(new digital_ofdm_sampler (fft_length, symbol_length, bandwidth, timeout));
}

digital_ofdm_sampler::digital_ofdm_sampler (unsigned int fft_length, 
              unsigned int symbol_length,
              unsigned int bandwidth,
              unsigned int timeout)
  : gr_block ("ofdm_sampler",
        gr_make_io_signature2 (2, 2, sizeof (gr_complex), sizeof(char)),
        gr_make_io_signature2 (2, 2, sizeof (gr_complex)*fft_length, sizeof(char)*fft_length)),
    d_state(STATE_NO_SIG), d_timeout_max(timeout), d_fft_length(fft_length), d_symbol_length(symbol_length), d_bandwidth(bandwidth)
{
  lts_samples_since=0;
  set_relative_rate(1.0/(double) fft_length);   // buffer allocator hint
}

void
digital_ofdm_sampler::forecast (int noutput_items, gr_vector_int &ninput_items_required)
{
  // FIXME do we need more
  //int nreqd  = (noutput_items-1) * d_symbol_length + d_fft_length;
  int nreqd  = d_symbol_length + d_fft_length;
  unsigned ninputs = ninput_items_required.size ();
  for (unsigned i = 0; i < ninputs; i++)
    ninput_items_required[i] = nreqd;
}


int
digital_ofdm_sampler::general_work (int noutput_items,
            gr_vector_int &ninput_items,
            gr_vector_const_void_star &input_items,
            gr_vector_void_star &output_items)
{
  // Use the stream tags to the timestamp
  std::vector<gr_tag_t> rx_time_tags;
  const uint64_t nread = this->nitems_read(0); //number of items read on port 0
  this->get_tags_in_range(rx_time_tags, 0, nread, nread+ninput_items[0], TIME_KEY);
  
  // See if there is a RX timestamp (only on first block or after underrun)
  if(rx_time_tags.size()>0) {
    size_t t = rx_time_tags.size()-1;

    // Take the last timestamp
    const uint64_t sample_offset = rx_time_tags[t].offset;  // distance from sample to timestamp in samples
    const pmt::pmt_t &value = rx_time_tags[t].value;

    // If the offset is greater than 0, this is a bit odd and complicated, so let's throw an error
    // and if this is common, George will fix it.
    if(sample_offset>0) {
      std::cerr << "----- ERROR:  RX Time offset > 0, George will fix if this is common\n";
      exit(-1);
    }
		
    // Now, compute the actual time in seconds and fractional seconds of the preamble
    lts_frac_of_secs = pmt::pmt_to_double(pmt_tuple_ref(value,1));
    lts_secs = pmt::pmt_to_uint64(pmt_tuple_ref(value, 0));
    std::cout << " \n Got USRP timestamp: " << lts_secs << " " << lts_frac_of_secs << std::endl;
  }


  /** Test whether we can get sync cfo value
  const uint64_t nread1 = this->nitems_read(1);
  std::cout<<" 1: Read items 0: "<<nread << "\t ninput_items[0]: "<< ninput_items[0] << "\t Read items 1: " << nread1 << " \t ninput_items[1]: " << ninput_items[1] <<std::endl;

  std::vector<gr_tag_t> rx_sync_tags;
  this->get_tags_in_range(rx_sync_tags, 0, nread, nread+ninput_items[0], SYNC_CFO);
  // See if there is a RX timestamp (only on first block or after underrun)
  if(rx_sync_tags.size()>0) {
    size_t t = rx_sync_tags.size()-1;

    // Take the last timestamp
    const uint64_t sample_offset = rx_sync_tags[t].offset;  // distance from sample to timestamp in samples
    const pmt::pmt_t newvalue = rx_sync_tags[t].value;

    // If the offset is greater than 0, this is a bit odd and complicated, so let's throw an error
    // and if this is common, George will fix it.
    if(sample_offset==0) {
      std::cerr << "----- ERROR:  RX Time offset > 0, George will fix if this is common\n";
      exit(-1);
    }

    std::cout << " \n Got CFO Value: " << newvalue << " \t " << sample_offset << std::endl;
  } */


  const gr_complex *iptr = (const gr_complex *) input_items[0];
  const char *trigger = (const char *) input_items[1];

  gr_complex *optr = (gr_complex *) output_items[0];
  char *outsig = (char *) output_items[1];

  //FIXME: we only process a single OFDM symbol at a time; after the preamble, we can 
  // process a few at a time as long as we always look out for the next preamble.
  unsigned int index=d_fft_length;  // start one fft length into the input so we can always look back this far

  outsig[0] = 0; // set output to no signal by default

  // Search for a preamble trigger signal during the next symbol length
  while((d_state != STATE_PREAMBLE) && (index <= (d_symbol_length+d_fft_length))) {
    if(trigger[index]) {
      outsig[0] = 1; // tell the next block there is a preamble coming
      d_state = STATE_PREAMBLE;

      // The analog to digital converter is 100 million samples / sec.  That translates to 
      // 2.5ns of time for every sample. However, we need to account decimation rate.
      double time_per_sample = 1.0 / d_bandwidth; // lts_samples_since counts input samples 
      uint64_t samples_passed = lts_samples_since + index;
      double elapsed = (double)samples_passed * time_per_sample;
			
      // Use the last time stamp to calculate the time of the premable synchronization
      uint64_t sync_sec = (int)elapsed + lts_secs;
      double sync_frac_sec = elapsed - (int)elapsed + lts_frac_of_secs;
      if(sync_frac_sec>1) {
        sync_sec += (uint64_t)sync_frac_sec; 
        sync_frac_sec -= (uint64_t)sync_frac_sec;
      }

      if(VERBOSE) {
	std::cout << "got a preamble.... calculating timestamp of sync\n";
	std::cout << "... relative_rate: " << relative_rate() << "\n";
	std::cout << "... time_per_sample: " << time_per_sample << "\n";
	std::cout << "... samples_passed: " << samples_passed << "\n";
        std::cout << "... bandwidth:" << d_bandwidth << "\n";
	std::cout << "... elapsed: "<< elapsed << "\n";
	std::cout << "... sync_sec: "<< sync_sec << "\n";
	std::cout << "... sync_fs: "<< sync_frac_sec << "\n";
      }

      const pmt::pmt_t _id = pmt::pmt_string_to_symbol(this->name());
      const pmt::pmt_t val = pmt::pmt_make_tuple(
          pmt::pmt_from_uint64(sync_sec),      // FPGA clock in seconds that we found the sync
          pmt::pmt_from_double(sync_frac_sec)  // FPGA clock in fractional seconds that we found the sync
        );
      this->add_item_tag(1, nitems_written(1), SYNC_TIME, val, _id);
      if(false)
        std::cout<<"---- [SAMPLER] Adding timestamp tag, offset: "<<nitems_written(1)<<"\t index: "<<index<<" \n"; //" nitems_written(0) "<<nitems_written(0)<<"\n";


      // With a preamble, let's now check for the preamble sync cfo
      std::vector<gr_tag_t> rx_sync_tags;
      int port = 0;
      const uint64_t nread = this->nitems_read(port);
      this->get_tags_in_range(rx_sync_tags, port, nread, nread+ninput_items[port], SYNC_CFO);
      if(rx_sync_tags.size()>0) {
        size_t t = rx_sync_tags.size()-1;
        const pmt::pmt_t value = rx_sync_tags[t].value;
        double sync_cfo = pmt::pmt_to_double(value);
        this->add_item_tag(1, nitems_written(1), SYNC_CFO, value, _id);
        if(false) {
          std::cout<<"---- [SAMPLER] Adding cfo tag, offset: "<<nitems_written(1)<<"\t index: "<<index<<" \n";
//        std::cout << "---- [SAMPLER] Range: ["<<nread<<":"<<nread+ninput_items[port]<<") \t index: " <<index<<" \t value: "<<sync_cfo<<"\n";
        }
      } else {
        //std::cout<<" 2: Read items: "<<nread << "\t ninput_items[0]: "<< ninput_items[0] << "\t index: " << index << std::endl;
        std::cerr << "---- [SAMPLER] Preamble received, with no CFO?  \t Range: ["<<nread<<":"<<nread+ninput_items[0]<<") \t index: " <<index<<"\n";
      }
    }
    else
      index++;
  }
  
  unsigned int i, pos, ret;
  switch(d_state) {
  case(STATE_PREAMBLE):
    // When we found a preamble trigger, get it and set the symbol boundary here
    for(i = (index - d_fft_length + 1); i <= index; i++) {
      *optr++ = iptr[i];
    }
    
    d_timeout = d_timeout_max; // tell the system to expect at least this many symbols for a frame
    d_state = STATE_FRAME;
    consume_each(index - d_fft_length + 1); // consume up to one fft_length away to keep the history
    lts_samples_since += index - d_fft_length + 1;
    ret = 1;
    break;
    
  case(STATE_FRAME):
    // use this state when we have processed a preamble and are getting the rest of the frames
    //FIXME: we could also have a power squelch system here to enter STATE_NO_SIG if no power is received

    // skip over fft length history and cyclic prefix
    pos = d_symbol_length;         // keeps track of where we are in the input buffer
    while(pos < d_symbol_length + d_fft_length) {
      *optr++ = iptr[pos++];
    }

    if(d_timeout-- == 0) {
      printf("TIMEOUT\n");
      d_state = STATE_NO_SIG;
    }

    consume_each(d_symbol_length); // jump up by 1 fft length and the cyclic prefix length
    lts_samples_since += d_symbol_length;
    ret = 1;
    break;

  case(STATE_NO_SIG):
  default:
    consume_each(index-d_fft_length); // consume everything we've gone through so far leaving the fft length history
    lts_samples_since += index-d_fft_length;
    //std::cout << index << " " << index - d_fft_length << std::endl;
    ret = 0;
    break;
  }

  return ret;
}
