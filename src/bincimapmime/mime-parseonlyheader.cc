/* -*- mode:c++;c-basic-offset:2 -*- */
/*  --------------------------------------------------------------------
 *  Filename:
 *    mime-parseonlyheader.cc
 *  
 *  Description:
 *    Implementation of main mime parser components
 *  --------------------------------------------------------------------
 *  Copyright 2002-2005 Andreas Aardal Hanssen
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 *  --------------------------------------------------------------------
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "mime.h"
#include "mime-utils.h"
#include "mime-inputsource.h"
#include "convert.h"
#include <string>
#include <vector>
#include <map>
#include <exception>
#include <iostream>

#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>

#ifndef NO_NAMESPACES
using namespace ::std;
#endif /* NO_NAMESPACES */

//------------------------------------------------------------------------
void Binc::MimeDocument::parseOnlyHeader(int fd) const
{
  if (allIsParsed || headerIsParsed)
    return;
  
  headerIsParsed = true;

  if (!mimeSource || mimeSource->getFileDescriptor() != fd) {
    delete mimeSource;
    mimeSource = new MimeInputSource(fd);
  } else {
    mimeSource->reset();
  }


  headerstartoffsetcrlf = 0;
  headerlength = 0;
  bodystartoffsetcrlf = 0;
  bodylength = 0;
  messagerfc822 = false;
  multipart = false;

  nlines = 0;
  nbodylines = 0;

  doParseOnlyHeader("");
}

void Binc::MimeDocument::parseOnlyHeader(istream& s) const
{
  if (allIsParsed || headerIsParsed)
    return;
  
  headerIsParsed = true;

  delete mimeSource;
  mimeSource = new MimeInputSourceStream(s);

  headerstartoffsetcrlf = 0;
  headerlength = 0;
  bodystartoffsetcrlf = 0;
  bodylength = 0;
  messagerfc822 = false;
  multipart = false;

  nlines = 0;
  nbodylines = 0;

  doParseOnlyHeader("");
}

//------------------------------------------------------------------------
int Binc::MimePart::doParseOnlyHeader(const string &toboundary) const
{
  string name;
  string content;
  char cqueue[4];
  memset(cqueue, 0, sizeof(cqueue));

  headerstartoffsetcrlf = mimeSource->getOffset();

  bool quit = false;
  char c = '\0';

  while (!quit) {
    // read name
    while (1) {
      if (!mimeSource->getChar(&c)) {
	quit = true;
	break;
      }

      if (c == '\n') ++nlines;
      if (c == ':') break;
      if (c == '\n') {
	for (int i = name.length() - 1; i >= 0; --i)
	  mimeSource->ungetChar();

	quit = true;
	name.clear();
	break;
      }

      name += c;

      if (name.length() == 2 && name.substr(0, 2) == "\r\n") {
	name.clear();
	quit = true;
	break;
      }
    }

    if (name.length() == 1 && name[0] == '\r') {
      name.clear();
      break;
    }

    if (quit) break;

    while (!quit) {
      if (!mimeSource->getChar(&c)) {
	quit = true;
	break;
      }

      if (c == '\n') ++nlines;

      for (int i = 0; i < 3; ++i)
	cqueue[i] = cqueue[i + 1];
      cqueue[3] = c;

      if (strncmp(cqueue, "\r\n\r\n", 4) == 0) {
	quit = true;
	break;
      }

      if (cqueue[2] == '\n') {

	// guess the mime rfc says what can not appear on the beginning
	// of a line.
	if (!isspace(cqueue[3])) {
	  if (content.length() > 2)
	    content.resize(content.length() - 2);

	  trim(content);
	  h.add(name, content);

	  name = c;
	  content.clear();
	  break;
	}
      }

      content += c;
    }
  }

  if (name != "") {
    if (content.length() > 2)
      content.resize(content.length() - 2);
    h.add(name, content);
  }

  headerlength = mimeSource->getOffset() - headerstartoffsetcrlf;

  return 1;
}
