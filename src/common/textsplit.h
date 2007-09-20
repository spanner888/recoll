/*
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
#ifndef _TEXTSPLIT_H_INCLUDED_
#define _TEXTSPLIT_H_INCLUDED_
/* @(#$Id: textsplit.h,v 1.18 2007-09-20 08:45:05 dockes Exp $  (C) 2004 J.F.Dockes */

#include <string>
#ifndef NO_NAMESPACES
using std::string;
#endif

/**
 * Function class whose takeword method is called for every detected word while * splitting text.
 */
class TextSplitCB {
public:
    virtual ~TextSplitCB() {}
    virtual bool takeword(const string& term, 
			  int pos,  // term pos
			  int bts,  // byte offset of first char in term
			  int bte   // byte offset of first char after term
			  ) = 0; 
};

class Utf8Iter;

/** 
 * Split text into words. 
 * See comments at top of .cpp for more explanations.
 * This uses a callback function. It could be done with an iterator instead,
 * but 'ts much simpler this way...
 */
class TextSplit {
public:
    enum Flags {TXTS_NONE = 0, 
		TXTS_ONLYSPANS = 1,  // Only return maximum spans (a@b.com) 
		TXTS_NOSPANS = 2,  // Only return atomic words (a, b, com)
		TXTS_KEEPWILD = 4, // Handle wildcards as letters
		TXTS_NOCJK = 8     // CJK special processing
    };

    /**
     * Constructor: just store callback object
     */
    TextSplit(TextSplitCB *t, Flags flags = Flags(TXTS_NONE))
	: m_flags(flags), m_cb(t), m_maxWordLength(40), 
	  m_nocjk((m_flags & TXTS_NOCJK) != 0),
	  m_prevpos(-1)
    {
    }

    /**
     * Split text, emit words and positions.
     */
    bool text_to_words(const string &in);

    // Utility functions : these does not need the user to setup a callback 
    // etc.
    static int countWords(const string &in, Flags flgs = TXTS_ONLYSPANS);

private:
    Flags         m_flags;
    TextSplitCB  *m_cb;
    int           m_maxWordLength;
    int           m_nocjk;

    // Current span. Might be jf.dockes@wanadoo.f
    string        m_span; 

    // Current word: no punctuation at all in there. Byte offset
    // relative to the current span and byte length
    int           m_wordStart;
    unsigned int  m_wordLen;

    // Currently inside number
    bool          m_inNumber;

    // Term position of current word and span
    int           m_wordpos; 
    int           m_spanpos;

    // It may happen that our cleanup would result in emitting the
    // same term twice. We try to avoid this
    int           m_prevpos;
    unsigned int  m_prevlen;

    // This processes cjk text:
    bool cjk_to_words(Utf8Iter *it, unsigned int *cp);

    bool emitterm(bool isspan, string &term, int pos, int bs, int be);
    bool doemit(bool spanerase, int bp);
};


#endif /* _TEXTSPLIT_H_INCLUDED_ */
