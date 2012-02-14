/* -*- c++ -*- */
/*
 * Copyright 2004,2010 Free Software Foundation, Inc.
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

#include <gr_timed_file_source.h>
#include <gr_io_signature.h>
#include <cstdio>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdexcept>
#include <stdio.h>

// win32 (mingw/msvc) specific
#ifdef HAVE_IO_H
#include <io.h>
#endif
#ifdef O_BINARY
#define	OUR_O_BINARY O_BINARY
#else
#define	OUR_O_BINARY 0
#endif
// should be handled via configure
#ifdef O_LARGEFILE
#define	OUR_O_LARGEFILE	O_LARGEFILE
#else
#define	OUR_O_LARGEFILE 0
#endif

static const pmt::pmt_t SOB_KEY  = pmt::pmt_string_to_symbol("tx_sob");
static const pmt::pmt_t EOB_KEY  = pmt::pmt_string_to_symbol("tx_eob");
static const pmt::pmt_t TIME_KEY = pmt::pmt_string_to_symbol("tx_time");

gr_timed_file_source::gr_timed_file_source (size_t itemsize, const char *filename, uint64_t lts_secs, double lts_frac_of_secs)
  : gr_sync_block ("file_source",
		   gr_make_io_signature (0, 0, 0),
		   gr_make_io_signature (1, 1, itemsize)),
    d_itemsize (itemsize), d_fp (0), d_repeat (false), d_state(false), d_lts_secs(lts_secs), d_lts_frac_of_secs(lts_frac_of_secs)
{
  // we use "open" to use to the O_LARGEFILE flag
  
  int fd;
  if ((fd = open (filename, O_RDONLY | OUR_O_LARGEFILE | OUR_O_BINARY)) < 0){
    perror (filename);
    throw std::runtime_error ("can't open file");
  }

  if ((d_fp = fdopen (fd, "rb")) == NULL){
    perror (filename);
    throw std::runtime_error ("can't open file");
  }
}

// public constructor that returns a shared_ptr

gr_timed_file_source_sptr
gr_make_timed_file_source (size_t itemsize, const char *filename, uint64_t lts_secs, double lts_frac_of_secs)
{
  return gnuradio::get_initial_sptr(new gr_timed_file_source (itemsize, filename, lts_secs, lts_frac_of_secs));
}

gr_timed_file_source::~gr_timed_file_source ()
{
  fclose ((FILE *) d_fp);
}

int 
gr_timed_file_source::work (int noutput_items,
		      gr_vector_const_void_star &input_items,
		      gr_vector_void_star &output_items)
{
  char *o = (char *) output_items[0];
  int i;
  int size = noutput_items;
  const pmt::pmt_t _id = pmt::pmt_string_to_symbol(this->name());

  if(!d_state) {
      d_state = true;

      
      const pmt::pmt_t val = pmt::pmt_make_tuple(
          pmt::pmt_from_uint64(d_lts_secs),      // FPGA clock in seconds that we found the sync
          pmt::pmt_from_double(d_lts_frac_of_secs)  // FPGA clock in fractional seconds that we found the sync
        );
      this->add_item_tag(0, nitems_written(0), TIME_KEY, val, _id);
      this->add_item_tag(0, nitems_written(0), SOB_KEY, pmt::PMT_T, _id);
      printf("Start of Burst | TIME: %d \n", nitems_written(0));
  }

  while (size) {
    i = fread(o, d_itemsize, size, (FILE *) d_fp);
    
    size -= i;
    o += i * d_itemsize;

    if (size == 0)		// done
      break;

    if (i > 0)			// short read, try again
      continue;

    // We got a zero from fread.  This is either EOF or error.  In
    // any event, if we're in repeat mode, seek back to the beginning
    // of the file and try again, else break

    if (!d_repeat) {
      if (size == noutput_items)
          printf("End of Burst: %d \t %d\n", nitems_written(0), nitems_written(0)+noutput_items);
      this->add_item_tag(0, nitems_written(0)+1, EOB_KEY, pmt::PMT_T, _id);  
      break;
    }

    if (fseek ((FILE *) d_fp, 0, SEEK_SET) == -1) {
      fprintf(stderr, "[%s] fseek failed\n", __FILE__);
      exit(-1);
    }
  }

  if (size > 0){			// EOF or error
    if (size == noutput_items)		// we didn't read anything; say we're done
      return -1;
    return noutput_items - size;	// else return partial result
  }

  return noutput_items;
}

bool
gr_timed_file_source::seek (long seek_point, int whence)
{
   return fseek ((FILE *) d_fp, seek_point * d_itemsize, whence) == 0;
}
