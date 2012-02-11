/* -*- c++ -*- */
/*
 * Copyright 2004 Free Software Foundation, Inc.
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


%constant int SEEK_SET = 0;     /* Seek from beginning of file. */
%constant int SEEK_CUR = 1;     /* Seek from current position.  */
%constant int SEEK_END = 2;     /* Seek from end of file.       */


GR_SWIG_BLOCK_MAGIC(gr,timed_file_source)

gr_timed_file_source_sptr 
gr_make_timed_file_source (size_t itemsize, const char *filename, uint64_t lts_secs=0, double lts_frac_of_secs=0);

class gr_timed_file_source : public gr_sync_block
{
 protected:
  gr_timed_file_source (size_t itemsize, const char *filename, uint64_t lts_secs, double lts_frac_of_secs);

 public:
  ~gr_timed_file_source ();

  bool seek (long seek_point, int whence);
};
