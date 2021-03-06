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

/**
 * bpdefutil.h -- utilities to aid in working with BPDefinition structures
 *                from the Service API
 */

#ifndef __BPDEFUTIL_H__
#define __BPDEFUTIL_H__

// when you compile this file, the service SDK include directory must
// be in the include path
#include <ServiceAPI/bpdefinition.h>
#include <string>
#include "BPUtils/bptypeutil.h"

namespace bp { namespace defutil {

/** build a dynamically allocated json object from a definition */
bp::Map * defToJson(const BPServiceDefinition * def);

/** build a dynamically allocated json object from a definition list */
bp::List * defsToJson(const BPServiceDefinition ** defs,
                      unsigned int numDefs);

}; };

#endif
