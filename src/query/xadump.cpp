#ifndef lint
static char rcsid[] = "@(#$Id: xadump.cpp,v 1.2 2004-12-17 15:50:48 dockes Exp $ (C) 2004 J.F.Dockes";
#endif

#include <strings.h>

#include <iostream>
#include <string>
#include <vector>

#include "transcode.h"

using namespace std;

#include "xapian.h"

static string thisprog;

static string usage =
    " -d <dbdir> -e <output encoding>"
    "  \n\n"
    ;

static void
Usage(void)
{
    cerr << thisprog  << ": usage:\n" << usage;
    exit(1);
}

static int        op_flags;
#define OPT_d	  0x1 
#define OPT_e     0x2
#define OPT_i     0x4
#define OPT_T     0x8
#define OPT_D     0x10
#define OPT_t     0x20
#define OPT_P     0x40
#define OPT_F     0x80
#define OPT_E     0x100

Xapian::Database db;

int main(int argc, char **argv)
{
    string dbdir = "/home/dockes/tmp/xapiandb";
    string outencoding = "ISO8859-1";
    int docid = 1;
    string aterm;

    thisprog = argv[0];
    argc--; argv++;

    while (argc > 0 && **argv == '-') {
	(*argv)++;
	if (!(**argv))
	    /* Cas du "adb - core" */
	    Usage();
	while (**argv)
	    switch (*(*argv)++) {
	    case 'D':	op_flags |= OPT_D; break;
	    case 'E':	op_flags |= OPT_E; break;
	    case 'F':   op_flags |= OPT_F; break;
	    case 'P':	op_flags |= OPT_P; break;
	    case 'T':	op_flags |= OPT_T; break;
	    case 'd':	op_flags |= OPT_d; if (argc < 2)  Usage();
		dbdir = *(++argv);
		argc--; 
		goto b1;
	    case 'e':	op_flags |= OPT_d; if (argc < 2)  Usage();
		outencoding = *(++argv);
		argc--; 
		goto b1;
	    case 'i':	op_flags |= OPT_i; if (argc < 2)  Usage();
		if (sscanf(*(++argv), "%d", &docid) != 1) Usage();
		argc--; 
		goto b1;
	    case 't':	op_flags |= OPT_t; if (argc < 2)  Usage();
		aterm = *(++argv);
		argc--; 
		goto b1;
	    default: Usage();	break;
	    }
    b1: argc--; argv++;
    }

    if (argc != 0)
	Usage();

    try {
	db = Xapian::Auto::open(dbdir, Xapian::DB_OPEN);

	cout << "DB: ndocs " << db.get_doccount() << " lastdocid " <<
	    db.get_lastdocid() << " avglength " << db.get_avlength() << endl;
	    
	if (op_flags & OPT_T) {
	    Xapian::TermIterator term;
	    string printable;
	    if (op_flags & OPT_i) {
		for (term = db.termlist_begin(docid);
		     term != db.termlist_end(docid);term++) {
		    transcode(*term, printable, "UTF-8", outencoding);
		    cout << printable << endl;
		}
	    } else {
		for (term = db.allterms_begin(); 
		     term != db.allterms_end();term++) {
		    transcode(*term, printable, "UTF-8", outencoding);
		    cout << printable << endl;
		}
	    }
	} else if (op_flags & OPT_D) {
	    Xapian::Document doc = db.get_document(docid);
	    string data = doc.get_data();
	    cout << data << endl;
	} else if (op_flags & OPT_P) {
	    Xapian::PostingIterator doc;
	    for (doc = db.postlist_begin(aterm);
		 doc != db.postlist_end(aterm);doc++) {
		cout << *doc << endl;
	    }
		
	} else if (op_flags & OPT_F) {
	    cout << "FreqFor " << aterm << " : " <<
		db.get_termfreq(aterm) << endl;
	} else if (op_flags & OPT_E) {
	    cout << "Exists " << aterm << " : " <<
		db.term_exists(aterm) << endl;
	} 




    } catch (const Xapian::Error &e) {
	cout << "Exception: " << e.get_msg() << endl;
    } catch (const string &s) {
	cout << "Exception: " << s << endl;
    } catch (const char *s) {
	cout << "Exception: " << s << endl;
    } catch (...) {
	cout << "Caught unknown exception" << endl;
    }
    exit(0);
}
