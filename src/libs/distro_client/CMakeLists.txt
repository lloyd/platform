# ***** BEGIN LICENSE BLOCK *****
# The contents of this file are subject to the Mozilla Public License
# Version 1.1 (the "License"); you may not use this file except in
# compliance with the License. You may obtain a copy of the License at
# http://www.mozilla.org/MPL/
# 
# Software distributed under the License is distributed on an "AS IS"
# basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the
# License for the specific language governing rights and limitations
# under the License.
# 
# The Original Code is BrowserPlus (tm).
# 
# The Initial Developer of the Original Code is Yahoo!.
# Portions created by Yahoo! are Copyright (c) 2010 Yahoo! Inc.
# All rights reserved.
# 
# Contributor(s): 
# ***** END LICENSE BLOCK *****
SET(libName "DistributionClient")

SET(${libName}_MAJOR_VERSION 0)
SET(${libName}_MINOR_VERSION 1)
SET(${libName}_LINK_STATIC bphttp BPUtils ArchiveLib yajl)

# explicitly set sources to include QueryCache.cpp which is erb substituted
FILE (GLOB ${libName}_SOURCES *.cpp ${CMAKE_CURRENT_BINARY_DIR}/*.cpp )

LINK_DIRECTORIES("${BP_EXTERNALS}/lib")

YBT_BUILD(LIBRARY_STATIC ${libName})
# for windows build ordering
ADD_DEPENDENCIES( ${libName}_s bphttp_s BPUtils_s ArchiveLib_s)
