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

#ifndef INCLUDED_GR_TIMED_FILE_SOURCE_H
#define INCLUDED_GR_TIMED_FILE_SOURCE_H

#include <gr_core_api.h>
#include <gr_sync_block.h>

class gr_timed_file_source;
typedef boost::shared_ptr<gr_timed_file_source> gr_timed_file_source_sptr;

GR_CORE_API gr_timed_file_source_sptr
gr_make_timed_file_source (size_t itemsize, const char *filename, uint64_t lts_secs=0, double lts_frac_of_secs=0);

/*!
 * \brief Read stream from file
 * \ingroup source_blk
 */

class GR_CORE_API gr_timed_file_source : public gr_sync_block
{
  friend GR_CORE_API gr_timed_file_source_sptr gr_make_timed_file_source (size_t itemsize,
						  const char *filename, uint64_t lts_secs, double lts_frac_of_secs);
 private:
  size_t	d_itemsize;
  void	       *d_fp;
  bool		d_repeat;
  bool		d_state;
  uint64_t	d_lts_secs;
  double	d_lts_frac_of_secs;

 protected:
  gr_timed_file_source (size_t itemsize, const char *filename, uint64_t lts_secs, double lts_frac_of_secs);

 public:
  ~gr_timed_file_source ();

  int work (int noutput_items,
	    gr_vector_const_void_star &input_items,
	    gr_vector_void_star &output_items);

  /*!
   * \brief seek file to \p seek_point relative to \p whence
   *
   * \param seek_point	sample offset in file
   * \param whence	one of SEEK_SET, SEEK_CUR, SEEK_END (man fseek)
   */
  bool seek (long seek_point, int whence);
};

#endif /* INCLUDED_GR_TIMED_FILE_SOURCE_H */
