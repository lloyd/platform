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
IF (APPLE)
  # include carbon
  # INCLUDE_DIRECTORIES(/Developer/Headers/FlatCarbon)
  FIND_LIBRARY(CARBON_LIBRARY Carbon)
  MARK_AS_ADVANCED(CARBON_LIBRARY)
  FIND_LIBRARY(COCOA_LIBRARY Cocoa)
  MARK_AS_ADVANCED(COCOA_LIBRARY)
  SET(OS_LIBS ${CARBON_LIBRARY} ${COCOA_LIBRARY})
ELSEIF (WIN32)
  SET(OS_LIBS) # none (!)
ELSEIF (LINUX)
  SET(OS_LIBS crypto ssl) # hack
ENDIF(APPLE)

SET(BrowserPlusCore_LINK_STATIC 
    DistributionClient ServiceManager Permissions
    ServiceRunnerLib BPUtils I18n)

YBT_BUILD(BINARY BrowserPlusCore)
TARGET_LINK_LIBRARIES(BrowserPlusCore ${OS_LIBS})
IF (APPLE)
  # XXX when 10.4 dropped, "else" clause becomes the only one 
  IF (OSX10.4_BUILD) 
    SET(EXPORTED_SYMBOLS exported_symbols_10_4.txt)
  ELSE ()
    SET(EXPORTED_SYMBOLS exported_symbols.txt)
  ENDIF ()
  SET_TARGET_PROPERTIES(BrowserPlusCore PROPERTIES
                        LINK_FLAGS_DEBUG "-exported_symbols_list ${CMAKE_CURRENT_SOURCE_DIR}/${EXPORTED_SYMBOLS} -flat_namespace")
  SET_TARGET_PROPERTIES(BrowserPlusCore PROPERTIES
                        LINK_FLAGS_RELEASE "-exported_symbols_list ${CMAKE_CURRENT_SOURCE_DIR}/${EXPORTED_SYMBOLS} -flat_namespace")
  SET_TARGET_PROPERTIES(BrowserPlusCore PROPERTIES
                        LINK_FLAGS_CODECOVERAGE "-exported_symbols_list ${CMAKE_CURRENT_SOURCE_DIR}/exported_symbols2.txt -flat_namespace")
ENDIF ()
