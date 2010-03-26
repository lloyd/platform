/**
 * ***** BEGIN LICENSE BLOCK *****
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 * 
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the
 * License for the specific language governing rights and limitations
 * under the License.
 * 
 * The Original Code is BrowserPlus (tm).
 * 
 * The Initial Developer of the Original Code is Yahoo!.
 * Portions created by Yahoo! are Copyright (c) 2009 Yahoo! Inc.
 * All rights reserved.
 * 
 * Contributor(s): 
 * ***** END LICENSE BLOCK *****
 */

/**
 * HttpStressTest.cpp
 * Stresses the snot out of the HTTP client implementation.
 *
 * Created by Lloyd Hilaiel on 03/26/2010.
 */

#include "HttpStressTest.h"
#include <math.h>
#include "BPUtils/bpconvert.h"
#include "BPUtils/bpfile.h"
#include "BPUtils/BPLog.h"
#include "BPUtils/bprunloop.h"
#include "BPUtils/bpstopwatch.h"
#include "BPUtils/bpurl.h"
#include "BPUtils/HttpQueryString.h"
#include "BPUtils/HttpSyncTransaction.h"
#include "BPUtils/HttpTransaction.h"
#include "BPUtils/OS.h"
#include "BPUtils/bprandom.h"
#include "BPUtils/bpmd5.h"

CPPUNIT_TEST_SUITE_REGISTRATION(HttpStressTest);

// Test overview:
// * ten pieces of content are generated, they're random data referenced by
//   a url that includes their md5
// * a server is allocated on the main thread that will serve this content
// * X runloop threads are spun up 
// * each runloop thread runs a total of Y requests before exiting with
//   Z concurrent HTTP transactions
// * all runloop threads collect statistics
// * upon completion all runloop threads are joined and we tally up how many
//   transactions were completed successfully

// parameters of the test
#define AMOUNT_OF_CONTENT 10
#define SIZE_OF_CONTENT (1024 * 100)
#define RUNLOOP_THREADS 2
#define TRANS_PER_THREAD 100
#define SIMUL_TRANS 10


void HttpStressTest::beatTheSnotOutOfIt()
{
    // first let's generate random content key'd by its md5 value
    std::map<std::string, std::string> content;
    std::vector<std::string> contentMD5s;

    for (unsigned int i = 0; i < AMOUNT_OF_CONTENT; i++) {
        std::string c;
        while (c.length() < SIZE_OF_CONTENT) {
            char ch = (bp::random::generate() % 26) + 'a';
            c.push_back(ch);
        }
        // now md5
        std::string md5 = bp::md5::hash(c);
        
        // and add
        contentMD5s.push_back(md5);
        content[md5] = c;
    }

    // now we need a lil' webserver that will serve this conent

}
