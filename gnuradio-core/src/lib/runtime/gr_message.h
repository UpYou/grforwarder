/* -*- c++ -*- */
/*
 * Copyright 2005 Free Software Foundation, Inc.
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
#ifndef INCLUDED_GR_MESSAGE_H
#define INCLUDED_GR_MESSAGE_H

#include <gr_core_api.h>
#include <gr_types.h>
#include <string>

class gr_message;
typedef boost::shared_ptr<gr_message> gr_message_sptr;

/*!
 * \brief public constructor for gr_message
 */
GR_CORE_API gr_message_sptr
gr_make_message(long type = 0, double arg1 = 0, double arg2 = 0, size_t length = 0);

GR_CORE_API gr_message_sptr
gr_make_message_from_string(const std::string s, long type = 0, double arg1 = 0, double arg2 = 0);

/*!
 * \brief Message class.
 *
 * \ingroup misc
 * The ideas and method names for adjustable message length were
 * lifted from the click modular router "Packet" class.
 */
class GR_CORE_API gr_message {
  gr_message_sptr d_next;	// link field for msg queue
  long		  d_type;	// type of the message
  double	  d_arg1;	// optional arg1
  double 	  d_arg2;	// optional arg2

  bool		 d_timestamp_valid;		// whether the timestamp is valid
  double 	 d_timestamp_sec;		// the preamble sync time in seconds
  double 	 d_timestamp_frac_sec;	        // the preamble sync time's fractional seconds
  bool           d_cfo_valid;                   // whether cfo value is valid
  double         d_cfo;                         // the cfo value
  double         d_snr;                         // the snr value for RawOFDM
  std::vector<double> d_power_list;             // the effective power values for PNC
  std::vector<double> d_power_list2;            // the power values for PNC

  unsigned char	 *d_buf_start;	// start of allocated buffer
  unsigned char  *d_msg_start;	// where the msg starts
  unsigned char  *d_msg_end;	// one beyond end of msg
  unsigned char  *d_buf_end;	// one beyond end of allocated buffer

  gr_message (long type, double arg1, double arg2, size_t length);

  friend GR_CORE_API gr_message_sptr
    gr_make_message (long type, double arg1, double arg2, size_t length);

  friend GR_CORE_API gr_message_sptr
    gr_make_message_from_string (const std::string s, long type, double arg1, double arg2);

  friend class gr_msg_queue;

  unsigned char *buf_data() const  { return d_buf_start; }
  size_t buf_len() const 	   { return d_buf_end - d_buf_start; }

public:
  ~gr_message ();

  long type() const   { return d_type; }
  double arg1() const { return d_arg1; }
  double arg2() const { return d_arg2; }
  bool timestamp_valid() const { return d_timestamp_valid; }
  double timestamp_sec() const { return d_timestamp_sec; }
  double timestamp_frac_sec() const { return d_timestamp_frac_sec; }
  bool cfo_valid() const { return d_cfo_valid; }
  double cfo_value() const { return d_cfo; }
  double snr_value() const { return d_snr; }
  std::vector<double> power_list() const { return d_power_list; }
  std::vector<double> power_list2() const { return d_power_list2; }

  void set_type(long type)   { d_type = type; }
  void set_arg1(double arg1) { d_arg1 = arg1; }
  void set_arg2(double arg2) { d_arg2 = arg2; }
  void set_cfo(double cfo)   { d_cfo  = cfo;  d_cfo_valid = true; }
  void set_snr(double snr)    { d_snr = snr;}
  void set_power_list(std::vector<double> power_list) { d_power_list = power_list; }
  void set_power_list2(std::vector<double> power_list) { d_power_list2 = power_list; }
  void set_timestamp(double ps, double pfs);

  unsigned char *msg() const { return d_msg_start; }
  size_t length() const      { return d_msg_end - d_msg_start; }
  std::string to_string() const;

};

GR_CORE_API long gr_message_ncurrently_allocated ();

#endif /* INCLUDED_GR_MESSAGE_H */
