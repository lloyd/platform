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
 * Portions created by Yahoo! are Copyright (c) 2010 Yahoo! Inc.
 * All rights reserved.
 * 
 * Contributor(s): 
 * ***** END LICENSE BLOCK *****
 */

/*
 *  bpdebug.h
 *  Debugging support.
 *
 *  Created by David Grigsby on 5/3/09.
 *
 */

#ifndef _BPDEBUG_H_
#define _BPDEBUG_H_


#include <string>
#include <list>

namespace bp {
namespace debug {
        

/**
 * invoke os-specific call to offer developer opportunity to attach a debugger.
 */
void attachDebugger();

void breakpoint( const std::string& sName );
        
void setForcedBreakpoints( const std::list<std::string>& breakpoints );

#ifdef MACOSX
void showAlert(const std::string& s);
#endif
        
} // debug
} // bp

#endif
