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

#include <digital_ofdm_frame_sink.h>
#include <gr_io_signature.h>
#include <gr_expj.h>
#include <gr_math.h>
#include <math.h>
#include <cstdio>
#include <stdexcept>
#include <iostream>
#include <string.h>

#define VERBOSE 0
#define MY_DEBUG 0

static const pmt::pmt_t TIME_KEY = pmt::pmt_string_to_symbol("rx_time");
static const pmt::pmt_t SYNC_TIME = pmt::pmt_string_to_symbol("sync_time");
static const pmt::pmt_t SYNC_CFO = pmt::pmt_string_to_symbol("sync_cfo");

// Keep track of the RX timestamp
double lts_sync_frac_of_secs = 0;
uint64_t lts_sync_secs = 0;
bool sync_cfo_valid = false;
double sync_cfo_value = 0;

inline void
digital_ofdm_frame_sink::enter_search()
{
  if (VERBOSE)
    fprintf(stderr, "@ enter_search\n");

  d_state = STATE_SYNC_SEARCH;

}
    
inline void
digital_ofdm_frame_sink::enter_have_sync()
{
  if (VERBOSE)
    fprintf(stderr, "@ enter_have_sync\n");

  d_state = STATE_HAVE_SYNC;

  // clear state of demapper
  d_byte_offset = 0;
  d_partial_byte = 0;

  d_header = 0;
  d_headerbytelen_cnt = 0;

  // Resetting PLL
  d_freq = 0.0;
  d_phase = 0.0;
  fill(d_dfe.begin(), d_dfe.end(), gr_complex(1.0,0.0));
}

inline void
digital_ofdm_frame_sink::enter_have_header()
{
  d_state = STATE_HAVE_HEADER;

  // header consists of two 16-bit shorts in network byte order
  // payload length is lower 12 bits
  // whitener offset is upper 4 bits
  d_packetlen = (d_header >> 16) & 0x0fff;
  d_packet_whitener_offset = (d_header >> 28) & 0x000f;
  d_packetlen_cnt = 0;

  if (VERBOSE)
    fprintf(stderr, "@ enter_have_header (payload_len = %d) (offset = %d)\n", 
	    d_packetlen, d_packet_whitener_offset);
}


unsigned char digital_ofdm_frame_sink::slicer(const gr_complex x)
{
  unsigned int table_size = d_sym_value_out.size();
  unsigned int min_index = 0;
  float min_euclid_dist = norm(x - d_sym_position[0]);
  float euclid_dist = 0;
  
  for (unsigned int j = 1; j < table_size; j++){
    euclid_dist = norm(x - d_sym_position[j]);
    if (euclid_dist < min_euclid_dist){
      min_euclid_dist = euclid_dist;
      min_index = j;
    }
  }
  return d_sym_value_out[min_index];
}

unsigned int digital_ofdm_frame_sink::demapper(const gr_complex *in,
					       unsigned char *out)
{
  unsigned int i=0, bytes_produced=0;
  gr_complex carrier;

  carrier=gr_expj(d_phase);

  gr_complex accum_error = 0.0;
  //while(i < d_occupied_carriers) {
  while(i < d_subcarrier_map.size()) {
    if(d_nresid > 0) {
      d_partial_byte |= d_resid;
      d_byte_offset += d_nresid;
      d_nresid = 0;
      d_resid = 0;
    }
    
    //while((d_byte_offset < 8) && (i < d_occupied_carriers)) {
    while((d_byte_offset < 8) && (i < d_subcarrier_map.size())) {
      //gr_complex sigrot = in[i]*carrier*d_dfe[i];
      gr_complex sigrot = in[d_subcarrier_map[i]]*carrier*d_dfe[i];
      
      if(d_derotated_output != NULL){
	d_derotated_output[i] = sigrot;
      }
      
      unsigned char bits = slicer(sigrot);

      gr_complex closest_sym = d_sym_position[bits];
      
      accum_error += sigrot * conj(closest_sym);

      // FIX THE FOLLOWING STATEMENT
      if (norm(sigrot)> 0.001) d_dfe[i] +=  d_eq_gain*(closest_sym/sigrot-d_dfe[i]);
      
      i++;

      if((8 - d_byte_offset) >= d_nbits) {
	d_partial_byte |= bits << (d_byte_offset);
	d_byte_offset += d_nbits;
      }
      else {
	d_nresid = d_nbits-(8-d_byte_offset);
	int mask = ((1<<(8-d_byte_offset))-1);
	d_partial_byte |= (bits & mask) << d_byte_offset;
	d_resid = bits >> (8-d_byte_offset);
	d_byte_offset += (d_nbits - d_nresid);
      }
      //printf("demod symbol: %.4f + j%.4f   bits: %x   partial_byte: %x   byte_offset: %d   resid: %x   nresid: %d\n", 
      //     in[i-1].real(), in[i-1].imag(), bits, d_partial_byte, d_byte_offset, d_resid, d_nresid);
    }

    if(d_byte_offset == 8) {
      //printf("demod byte: %x \n\n", d_partial_byte);
      out[bytes_produced++] = d_partial_byte;
      d_byte_offset = 0;
      d_partial_byte = 0;
    }
  }
  //std::cerr << "accum_error " << accum_error << std::endl;

  float angle = arg(accum_error);

  d_freq = d_freq - d_freq_gain*angle;
  d_phase = d_phase + d_freq - d_phase_gain*angle;
  if (d_phase >= 2*M_PI) d_phase -= 2*M_PI;
  if (d_phase <0) d_phase += 2*M_PI;
    
  //if(VERBOSE)
  //  std::cerr << angle << "\t" << d_freq << "\t" << d_phase << "\t" << std::endl;
  
  return bytes_produced;
}


digital_ofdm_frame_sink_sptr
digital_make_ofdm_frame_sink(const std::vector<gr_complex> &sym_position, 
			     const std::vector<unsigned char> &sym_value_out,
			     gr_msg_queue_sptr target_queue, unsigned int occupied_carriers,
			     float phase_gain, float freq_gain)
{
  return gnuradio::get_initial_sptr(new digital_ofdm_frame_sink(sym_position, sym_value_out,
								target_queue, occupied_carriers,
								phase_gain, freq_gain));
}


digital_ofdm_frame_sink::digital_ofdm_frame_sink(const std::vector<gr_complex> &sym_position, 
						 const std::vector<unsigned char> &sym_value_out,
						 gr_msg_queue_sptr target_queue, unsigned int occupied_carriers,
						 float phase_gain, float freq_gain)
  : gr_sync_block ("ofdm_frame_sink",
		   gr_make_io_signature2 (2, 2, sizeof(gr_complex)*occupied_carriers, sizeof(char)),
		   gr_make_io_signature (1, 1, sizeof(gr_complex)*occupied_carriers)),
    d_target_queue(target_queue), d_occupied_carriers(occupied_carriers), 
    d_byte_offset(0), d_partial_byte(0),
    d_resid(0), d_nresid(0),d_phase(0),d_freq(0),d_phase_gain(phase_gain),d_freq_gain(freq_gain),
    d_eq_gain(0.05)
{
  std::string carriers = "FE7F";

  // A bit hacky to fill out carriers to occupied_carriers length
  int diff = (d_occupied_carriers - 4*carriers.length()); 
  while(diff > 7) {
    carriers.insert(0, "f");
    carriers.insert(carriers.length(), "f");
    diff -= 8;
  }
  
  // if there's extras left to be processed
  // divide remaining to put on either side of current map
  // all of this is done to stick with the concept of a carrier map string that
  // can be later passed by the user, even though it'd be cleaner to just do this
  // on the carrier map itself
  int diff_left=0;
  int diff_right=0;

  // dictionary to convert from integers to ascii hex representation
  char abc[16] = {'0', '1', '2', '3', '4', '5', '6', '7', 
		  '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
  if(diff > 0) {
    char c[2] = {0,0};

    diff_left = (int)ceil((float)diff/2.0f);  // number of carriers to put on the left side
    c[0] = abc[(1 << diff_left) - 1];         // convert to bits and move to ASCI integer
    carriers.insert(0, c);
    
    diff_right = diff - diff_left;	      // number of carriers to put on the right side
    c[0] = abc[0xF^((1 << diff_right) - 1)];  // convert to bits and move to ASCI integer
    carriers.insert(carriers.length(), c);
  }

  // It seemed like such a good idea at the time...
  // because we are only dealing with the occupied_carriers
  // at this point, the diff_left in the following compensates
  // for any offset from the 0th carrier introduced
  unsigned int i,j,k;
  for(i = 0; i < (d_occupied_carriers/4)+diff_left; i++) {
    char c = carriers[i];
    for(j = 0; j < 4; j++) {
      k = (strtol(&c, NULL, 16) >> (3-j)) & 0x1;
      if(k) {
	d_subcarrier_map.push_back(4*i + j - diff_left);
      }
    }
  }
  
  // make sure we stay in the limit currently imposed by the occupied_carriers
  if(d_subcarrier_map.size() > d_occupied_carriers) {
    throw std::invalid_argument("digital_ofdm_mapper_bcv: subcarriers allocated exceeds size of occupied carriers");
  }

  d_bytes_out = new unsigned char[d_occupied_carriers];
  d_dfe.resize(occupied_carriers);
  fill(d_dfe.begin(), d_dfe.end(), gr_complex(1.0,0.0));

  set_sym_value_out(sym_position, sym_value_out);
  
  enter_search();
}

digital_ofdm_frame_sink::~digital_ofdm_frame_sink ()
{
  delete [] d_bytes_out;
}

bool
digital_ofdm_frame_sink::set_sym_value_out(const std::vector<gr_complex> &sym_position, 
					   const std::vector<unsigned char> &sym_value_out)
{
  if (sym_position.size() != sym_value_out.size())
    return false;

  if (sym_position.size()<1)
    return false;

  d_sym_position  = sym_position;
  d_sym_value_out = sym_value_out;
  d_nbits = (unsigned long)ceil(log10(float(d_sym_value_out.size())) / log10(2.0));

  return true;
}


int
digital_ofdm_frame_sink::work (int noutput_items,
			       gr_vector_const_void_star &input_items,
			       gr_vector_void_star &output_items)
{
  const gr_complex *in = (const gr_complex *) input_items[0];
  const char *sig = (const char *) input_items[1];
  unsigned int j = 0;
  unsigned int bytes=0;

  /**
  const uint64_t nread_0 = this->nitems_read(0);
  const uint64_t nread_1 = this->nitems_read(1);
  std::cout<<" 1: Read items 0: "<< nread_0 << "\t ninput_items: "<< noutput_items << "\t Read items 1: " << nread_1 <<std::endl;
 
  std::vector<gr_tag_t> rx_tags;
  this->get_tags_in_range(rx_tags, 0, nread_0, nread_0+noutput_items, SYNC_CFO);
  // See if there is a RX timestamp (only on first block or after underrun)
  if(rx_tags.size()>0) {
    size_t t = rx_tags.size()-1;

    // Take the last timestamp
    const uint64_t sample_offset = rx_tags[t].offset;  // distance from sample to timestamp in samples
    const pmt::pmt_t newvalue = rx_tags[t].value;

    // If the offset is greater than 0, this is a bit odd and complicated, so let's throw an error
    // and if this is common, George will fix it.
    if(sample_offset==0) {
      std::cerr << "----- ERROR:  RX Time offset > 0, George will fix if this is common\n";
      exit(-1);
    }

    std::cout << " \n Got CFO Value: " << newvalue << " \t " << sample_offset << std::endl;
  } */

  // If the output is connected, send it the derotated symbols
  if(output_items.size() >= 1)
    d_derotated_output = (gr_complex *)output_items[0];
  else
    d_derotated_output = NULL;
  
  if (VERBOSE)
    fprintf(stderr,">>> Entering state machine\n");

  switch(d_state) {
      
  case STATE_SYNC_SEARCH:    // Look for flag indicating beginning of pkt
    if (VERBOSE)
      fprintf(stderr,"SYNC Search, noutput=%d\n", noutput_items);
    
    if (sig[0]) {  // Found it, set up for header decode
      enter_have_sync();

      int port = 1;
      // With a preamble, let's now check for the preamble sync timestamp
      std::vector<gr_tag_t> rx_sync_tags;
      const uint64_t nread1 = this->nitems_read(port);
      this->get_tags_in_range(rx_sync_tags, port, nread1, nread1+input_items.size(), SYNC_TIME);
      if(rx_sync_tags.size()>0) {
        size_t t = rx_sync_tags.size()-1;
        const pmt::pmt_t &value = rx_sync_tags[t].value;
	lts_sync_secs = pmt::pmt_to_uint64(pmt_tuple_ref(value, 0));
	lts_sync_frac_of_secs = pmt::pmt_to_double(pmt_tuple_ref(value,1));
        if(MY_DEBUG) {
	    std::cout << "---- [SINK]  Timestamp received, lts = "<<lts_sync_secs<<"\t fts = "<<lts_sync_frac_of_secs<<"\n";
	}
      } else {
	std::cerr << "---- [SINK][STATE_HAVE_SYNC] Header received, with no sync timestamp? \t Range: ["<<nread1<<":"<<nread1+input_items.size()<<")\n";
      }

      this->get_tags_in_range(rx_sync_tags, port, nread1, nread1+input_items.size(), SYNC_CFO);
      if(rx_sync_tags.size()>0) {
        size_t t = rx_sync_tags.size()-1;
        const pmt::pmt_t cfo_value = rx_sync_tags[t].value;
        sync_cfo_value = pmt::pmt_to_double(cfo_value);
        sync_cfo_valid = true;
        if(false) {
          std::cout << "---- [SINK] CFO receiverd, value: "<< sync_cfo_value <<" Range: [" << nread1 << ":" << nread1+input_items.size() <<") \n";
        }
      } else {
        std::cerr << "---- [SINK] Preamble received, with no CFO? \t Range: ["<< nread1 <<":"<< nread1+input_items.size() << ") \n";
      }

      // With a preamble, let's now check for the preamble sync cfo
      /**
      port = 1;
      std::vector<gr_tag_t> rx_sync_cfo_tags;
      const uint64_t nread2 = this->nitems_read(port);
      this->get_tags_in_range(rx_sync_cfo_tags, port, nread2, nread2+input_items.size(), SYNC_CFO);
      if(rx_sync_cfo_tags.size()>0) {
        size_t t = rx_sync_cfo_tags.size()-1;
        const pmt::pmt_t cfo_value = rx_sync_cfo_tags[t].value;
        sync_cfo_value = pmt::pmt_to_double(cfo_value);
        sync_cfo_valid = true;
        if(true) {
          std::cout << "---- [SINK] CFO receiverd, value: "<< sync_cfo_value <<" Range: [" << nread2 << ":" << nread2+input_items.size() <<") \n";
        }
      } else {
        std::cerr << "---- [SINK] Preamble received, with no CFO? \t Range: ["<< nread2 <<":"<< nread2+input_items.size() << ") \n";
      } */

    }
    break;

  case STATE_HAVE_SYNC:
    // only demod after getting the preamble signal; otherwise, the 
    // equalizer taps will screw with the PLL performance
    bytes = demapper(&in[0], d_bytes_out);
    
    if (VERBOSE) {
      if(sig[0])
	printf("ERROR -- Found SYNC in HAVE_SYNC\n");
      fprintf(stderr,"Header Search bitcnt=%d, header=0x%08x\n",
	      d_headerbytelen_cnt, d_header);
    }

    j = 0;
    while(j < bytes) {
      d_header = (d_header << 8) | (d_bytes_out[j] & 0xFF);
      j++;
      
      if (++d_headerbytelen_cnt == HEADERBYTELEN) {
	
	if (VERBOSE)
	  fprintf(stderr, "got header: 0x%08x\n", d_header);
	
	// we have a full header, check to see if it has been received properly
	if (header_ok()){
	  enter_have_header();
	  
	  if (VERBOSE)
	    printf("\nPacket Length: %d\n", d_packetlen);	
	  
	  while((j < bytes) && (d_packetlen_cnt < d_packetlen)) {
	    d_packet[d_packetlen_cnt++] = d_bytes_out[j++];
	  }
	  // std::cout<<"j="<<j<<" bytes="<<bytes<<" d_packetlen_cnt="<<d_packetlen_cnt<<std::endl;
	  if(d_packetlen_cnt == d_packetlen) {
	    	gr_message_sptr msg =
	      		gr_make_message(0, d_packet_whitener_offset, 0, d_packetlen);
	    	memcpy(msg->msg(), d_packet, d_packetlen_cnt);

		// With a good header, let's now check for the preamble sync timestamp
//		std::vector<gr_tag_t> rx_sync_tags;
//      		const uint64_t nread = this->nitems_read(1);
//		this->get_tags_in_range(rx_sync_tags, 1, nread, nread+input_items.size(), SYNC_TIME);
//		if(rx_sync_tags.size()>0) {
//			size_t t = rx_sync_tags.size()-1;
//			const pmt::pmt_t &value = rx_sync_tags[t].value;
//			uint64_t sync_secs = pmt::pmt_to_uint64(pmt_tuple_ref(value, 0));
//			double sync_frac_of_secs = pmt::pmt_to_double(pmt_tuple_ref(value,1));
//			msg->set_timestamp(sync_secs, sync_frac_of_secs);
//		} else {
//			std::cerr << "---- [STATE_HAVE_SYNC] Range: ["<<nread<<":"<<nread+input_items.size()<<"]\n";
//			std::cerr << "---- [STATE_HAVE_SYNC] Header received, with no sync timestamp? "<<"\n";
//		}
	    std::cout << "---- [FRAME_SINK_2]  nread: "<<nitems_read(1)<<"\n";
	    msg->set_timestamp(lts_sync_secs, lts_sync_frac_of_secs);
            if(sync_cfo_valid)  msg->set_cfo(sync_cfo_value);
	    d_target_queue->insert_tail(msg);		// send it
	    msg.reset();  				// free it up
	    
	    enter_search();				
	  }
	}
	else {
	  enter_search();				// bad header
	}
      }
    }
    break;
      
  case STATE_HAVE_HEADER:
    bytes = demapper(&in[0], d_bytes_out);

    if (VERBOSE) {
      if(sig[0])
	printf("ERROR -- Found SYNC in HAVE_HEADER at %d, length of %d\n", d_packetlen_cnt, d_packetlen);
      fprintf(stderr,"Packet Build\n");
    }
    
    j = 0;
    while(j < bytes) {
      d_packet[d_packetlen_cnt++] = d_bytes_out[j++];
      
      if (d_packetlen_cnt == d_packetlen){		// packet is filled
	// build a message
	// NOTE: passing header field as arg1 is not scalable
	gr_message_sptr msg =
	  gr_make_message(0, d_packet_whitener_offset, 0, d_packetlen_cnt);
	memcpy(msg->msg(), d_packet, d_packetlen_cnt);

	// NOTE: let's now check for the preamble sync timestamp if we can not run the branch above
//	std::vector<gr_tag_t> rx_sync_tags;
//	const uint64_t nread = this->nitems_read(1);
//	this->get_tags_in_range(rx_sync_tags, 1, nread-17, nread+input_items.size(), SYNC_TIME);
//	if(rx_sync_tags.size()>0) {
//		size_t t = rx_sync_tags.size()-1;
//		const pmt::pmt_t &value = rx_sync_tags[t].value;
//		uint64_t sync_secs = pmt::pmt_to_uint64(pmt_tuple_ref(value, 0));
//		double sync_frac_of_secs = pmt::pmt_to_double(pmt_tuple_ref(value,1));
//		std::cout << "---- [STATE_HAVE_HEADER] Range: ["<<nread-17<<":"<<nread+input_items.size()<<")\n";
//		std::cout << "---- [STATE_HAVE_HEADER] "<<sync_secs<<":"<<sync_frac_of_secs<<"\n";
//		msg->set_timestamp(sync_secs, sync_frac_of_secs);
//	} else {
//		std::cerr << "---- [STATE_HAVE_HEADER] Range: ["<<nread-17<<":"<<nread+input_items.size()<<"]\n";
//		std::cerr << "---- [STATE_HAVE_HEADER] Header received, with no sync timestamp? "<<"\n";
//	}
//	std::cout << "---- [FRAME_SINK_3]  nread: "<<nitems_read(1)<<"\t Packet Length: "<<d_packetlen<<"\n";
	msg->set_timestamp(lts_sync_secs, lts_sync_frac_of_secs);
        if(sync_cfo_valid)  msg->set_cfo(sync_cfo_value);
	d_target_queue->insert_tail(msg);		// send it
	msg.reset();  				        // free it up
	
	enter_search();
	break;
      }
    }
    break;
    
  default:
    assert(0);
    
  } // switch

  return 1;
}
