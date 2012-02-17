/* -*- c++ -*- */
/*
 * Copyright 2005,2010 Free Software Foundation, Inc.
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

#include <gr_message_source.h>
#include <gr_io_signature.h>
#include <cstdio>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdexcept>
#include <string.h>

static const pmt::pmt_t SOB_KEY  = pmt::pmt_string_to_symbol("tx_sob");
static const pmt::pmt_t EOB_KEY  = pmt::pmt_string_to_symbol("tx_eob");
static const pmt::pmt_t TIME_KEY = pmt::pmt_string_to_symbol("tx_time");

// public constructor that returns a shared_ptr

gr_message_source_sptr
gr_make_message_source(size_t itemsize, int msgq_limit)
{
  return gnuradio::get_initial_sptr(new gr_message_source(itemsize, msgq_limit));
}

// public constructor that takes existing message queue
gr_message_source_sptr
gr_make_message_source(size_t itemsize, gr_msg_queue_sptr msgq)
{
  return gnuradio::get_initial_sptr(new gr_message_source(itemsize, msgq));
}

gr_message_source::gr_message_source (size_t itemsize, int msgq_limit)
  : gr_sync_block("message_source",
		  gr_make_io_signature(0, 0, 0),
		  gr_make_io_signature(1, 1, itemsize)),
    d_itemsize(itemsize), d_msgq(gr_make_msg_queue(msgq_limit)), d_msg_offset(0), d_eof(false)
{
}

gr_message_source::gr_message_source (size_t itemsize, gr_msg_queue_sptr msgq)
  : gr_sync_block("message_source",
		  gr_make_io_signature(0, 0, 0),
		  gr_make_io_signature(1, 1, itemsize)),
    d_itemsize(itemsize), d_msgq(msgq), d_msg_offset(0), d_eof(false)
{
}

gr_message_source::~gr_message_source()
{
}

int
gr_message_source::work(int noutput_items,
			gr_vector_const_void_star &input_items,
			gr_vector_void_star &output_items)
{
  char *out = (char *) output_items[0];
  int nn = 0;
  const pmt::pmt_t _id = pmt::pmt_string_to_symbol(this->name());

  while (nn < noutput_items){
    if (d_msg){
      //
      // Consume whatever we can from the current message
      //
      int mm = std::min(noutput_items - nn, (int)((d_msg->length() - d_msg_offset) / d_itemsize));

      if( (d_msg_offset == 0) && (d_msg->timestamp_valid()) ) {  // start of the message
        const pmt::pmt_t val = pmt::pmt_make_tuple(
          pmt::pmt_from_uint64(d_msg->timestamp_sec()),      // FPGA clock in seconds that we found the sync
          pmt::pmt_from_double(d_msg->timestamp_frac_sec())  // FPGA clock in fractional seconds that we found the sync
        );
//        printf(">>> set SOB/TIME tag, timestamp %f at %d | noutput_items = %d | d_itemsize = %d \n", d_msg->timestamp_sec()+d_msg->timestamp_frac_sec(), nitems_written(0)+nn, noutput_items, d_itemsize);
        this->add_item_tag(0, nitems_written(0)+nn, TIME_KEY, val, _id);  // nn denotes the starting point of a message
        this->add_item_tag(0, nitems_written(0)+nn, SOB_KEY, pmt::PMT_T, _id);
      }

      memcpy (out, &(d_msg->msg()[d_msg_offset]), mm * d_itemsize);

      nn += mm;
      out += mm * d_itemsize;
      d_msg_offset += mm * d_itemsize;
      assert(d_msg_offset <= d_msg->length());

      if (d_msg_offset == d_msg->length()){
        if(d_msg->timestamp_valid()) {        // end of the message
          this->add_item_tag(0, nitems_written(0)+nn-1, EOB_KEY, pmt::PMT_T, _id);  // nn-1 denotes the end point of a message
//          printf(" >>> set EOB tag at %d | d_msg_offset = %d | d_msg_length = %d \n", nitems_written(0)+nn-1, d_msg_offset, d_msg->length());
        }

	if (d_msg->type() == 1) {	           // type == 1 sets EOF
	  d_eof = true;
//          printf(" >>> set eof true | nn = %d \n", nn);
        }
	d_msg.reset();
      }
    }
    else {
      //
      // No current message
      //
      if (d_msgq->empty_p() && nn > 0){    // no more messages in the queue, return what we've got
	break;
      }

      if (d_eof)
	return -1;

      d_msg = d_msgq->delete_head();	   // block, waiting for a message
      d_msg_offset = 0;

      if ((d_msg->length() % d_itemsize) != 0)
	throw std::runtime_error("msg length is not a multiple of d_itemsize");
    }
  }

  return nn;
}
