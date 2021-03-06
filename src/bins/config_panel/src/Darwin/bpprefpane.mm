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


#import "bpprefpane.h"
#include "BPUtils/BPUtils.h"
#include "platform_utils/bplocalization.h"
#include "platform_utils/LogConfigurator.h"
#include "platform_utils/ProductPaths.h"
#include <iostream>


@implementation BPPrefPane

- (void)                webView: (WebView *) sender
           didClearWindowObject: (WebScriptObject *)wso
                       forFrame: (WebFrame *)frame
{
    BPLOG_INFO_STRM("didClearWindowObject");
    m_so = new ScriptableConfigObject;

    WebScriptObject * scriptable = (WebScriptObject *)
        m_so->getScriptableObject()->scriptableObject((void *) wso);

    [wso setValue:scriptable forKey: @"BPState"];
}

- (void)webView:(WebView *)sender setStatusText:(NSString *)text
{
    BPLOG_INFO_STRM("Status: " << [text UTF8String]);    
}

- (void)webView:(WebView *)webView addMessageToConsole:(NSDictionary *)d
{
    std::stringstream ss;

    ss << "Warning from JavaScript Rendering engine -- " << std::endl;    

	id key;

    NSEnumerator *enumerator = [d keyEnumerator];
    
    while ((key = [enumerator nextObject])) {
        ss << "    " << [key UTF8String] << ": "
           << [[[d objectForKey:key] description] UTF8String];
    }

    BPLOG_ERROR(ss.str());   
}


- (void)webView: (WebView *) sender didFinishLoadForFrame:(WebFrame *)frame
{
    BPLOG_INFO("Did finish load for frame...");    
}

- (void) mainViewDidLoad
{
    BPLOG_INFO_STRM("Config panel UI rendered");
}

- (void) webView: (WebView *) sender
didFailProvisionalLoadWithError: (NSError *) error
        forFrame: (WebFrame *) frame
{
    BPLOG_ERROR("didFailProvisionalLoadWithError");
}

- (void)                    webView: (WebView *) sender
               didFailLoadWithError: (NSError *) error 
                           forFrame: (WebFrame *) frame
{
    BPLOG_ERROR("didFailLoadWithError");
}



- (NSView *) loadMainView
{
    // intialize BP logging
    bp::log::Configurator cfg;
    cfg.loadConfigFile();
    cfg.setPath(bp::paths::getObfuscatedWritableDirectory()/"ConfigPanel.log");
    cfg.configure();
    
    // (lth) on tiger and leopard pref pane width is different.  detect
    // os automatically here
	// thanks to: http://www.codehackers.net/blog/?p=40  
    int prefWidth = 656; // appropriate for leopard
    SInt32 MacOSXVersionNumber;
    if (Gestalt(gestaltSystemVersion, &MacOSXVersionNumber) == noErr) {
        if (MacOSXVersionNumber >= 0X1040 && MacOSXVersionNumber < 0X1050) {
		    prefWidth = 594; 
        }
    }

    // allocate a webview
    NSRect frame = { {0,0}, {prefWidth, 569} };

    m_webView = [[WebView alloc]
                    initWithFrame: frame
                    frameName: nil
                    groupName: nil];

    [m_webView setAutoresizingMask: NSViewHeightSizable | NSViewWidthSizable];

//    NSString * nsPath = [[NSString alloc] initWithUTF8String: path.external_file_string().c_str()];
//    NSURL * url = [[NSURL alloc] initFileURLWithPath: nsPath];
//    [[webView mainFrame] loadRequest: [NSURLRequest requestWithURL: url]];
//    [webView setPolicyDelegate: mso];
//    [m_webView setFrameLoadDelegate: nil];

    // disable scrolling
    [[[m_webView mainFrame] frameView]  setAllowsScrolling: NO];

    // save some resources
    [m_webView setMaintainsBackForwardList: NO];
    [[m_webView preferences] setPrivateBrowsingEnabled: YES];

    // figure out when the load is complete
    [m_webView setFrameLoadDelegate: self];

    // get status messages
    [m_webView setUIDelegate: self];

    boost::filesystem::path resourcePath(
        [[[NSBundle bundleForClass: [self class]] resourcePath] UTF8String]);
    boost::filesystem::path bundlePath(
        [[[NSBundle bundleForClass: [self class]] bundlePath] UTF8String]);
    boost::filesystem::path path(
        [[[NSBundle bundleForClass: [self class]] resourcePath] UTF8String]);

    path /= "ui";
    path = bp::localization::getLocalizedUIPath(
                path, bp::localization::getUsersLocale()) / "index.html";

    BPLOG_INFO_STRM("Rendering config panel ui from path: " << path);
    
    NSString * nsPath = [[NSString alloc] initWithUTF8String: path.string().c_str()];
    NSURL * url = [[NSURL alloc] initFileURLWithPath: nsPath];
    [[m_webView mainFrame] loadRequest: [NSURLRequest requestWithURL: url]];

    [self setMainView: m_webView];

    return m_webView;
}

- (void) dealloc
{
    if (m_so != NULL) {
        delete m_so;
        m_so = NULL;
    }
    [super dealloc];
}


@end
