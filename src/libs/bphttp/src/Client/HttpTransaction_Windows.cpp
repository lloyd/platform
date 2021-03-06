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
 *  HttpTransaction_Windows.cpp
 *
 *  Implements the Transaction class and related items.
 *
 *  Created by David Grigsby on 7/16/08.
 *  Copyright 2008 Yahoo! Inc. All rights reserved.
 *
 */
#include "HttpTransaction.h"
#include <map>
#include <Windows.h>
#include <Wininet.h>
#include "BPUtils/BPLog.h"
#include "BPUtils/bpconvert.h"
#include "BPUtils/bpthreadhopper.h"
#include "BPUtils/bpfile.h"
#include "BPUtils/bprunloop.h"
#include "BPUtils/bptimer.h"
#include "HttpListener.h"


using namespace std;

// Threading usage:  Everything except wininetCallback and onWininetCallback 
// occurs on the thread which invoked execute() or initiate().  
// wininet callbacks can occur on any thread, so onWininetCallback uses 
// threadhopper to continue processing on the correct thread.  A client may 
// call cancel() from any thread.  This lets us avoid synchronization for
// data access.  Yes, there is a race with m_bCancel, but it is harmless.

using bp::error::lastErrorString;


namespace bp {
namespace http {
namespace client {

///////////////////////////////////////////////////////////////////////////
// class Transaction::Impl
//
class Transaction::Impl : public virtual bp::time::ITimerListener
{
    // Class-scope Items
public:
    static const std::string    ksDefaultUserAgent;
    static const double         kfDefaultTimeoutSecs;
    static const DWORD          kBufferSize;
    
    // Construction/Destruction    
public:
    //  Impl(); // ?
    Impl(RequestPtr ptrRequest);
    ~Impl();
    
    // Class-scope Items
public:

    // What we should do next in our state machine.
    enum TranState { 
        eConnect, eOpenRequest, 
        eSendRequest, eSendRequestWithBody, 
        ePostGetData, ePostSendData, ePostComplete,
        eResponseGetHeaders,
        eResponseReceiveData, eResponseWriteData,
        eDone, eTimedOut, eError
    };

    // Static wininet callback.
    // Forwards immediately to instance method onWininetCallback. 
    static void CALLBACK wininetCallback(HINTERNET hInternet,
                                         DWORD_PTR dwContext,
                                         DWORD dwInternetStatus,
                                         LPVOID pStatusInfo,
                                         DWORD dwStatusInfoLength);   
    
    // Public Methods
public:
    // ITimerListener methods
    virtual void timesUp(bp::time::Timer* t);

    // Asynchronously execute the transaction
    void        initiate(IListenerWeakPtr pListener);

    // Cancel a transaction.
    void        cancel();
    
    // Accesors    
public:
    RequestPtr          request() const;
    const std::string&  userAgent() const;
    void                setUserAgent(const std::string& sUserAgent);
    double              timeoutSec() const;
    void                setTimeoutSec(double fSecs);

    
    // Internal Methods    
private:
    // Advance the state machine
    void        processRequest(DWORD error);

    // Setup the request
    void        openSession();
    DWORD       openConnection();
    DWORD       openRequest();

    // Send the request.
    DWORD       sendRequest();
    DWORD       sendRequestWithBody();

    // Handle posting data
    DWORD       getDataToPost();
    DWORD       postData(bool& done);
    DWORD       completePostRequest();

    // Receive the response.
    Version     receiveResponseVersion();
    Status      receiveResponseStatus();
    Headers     receiveResponseHeaders();
    DWORD       receiveResponseData();
    DWORD       writeResponseData(bool& done);

    // Handle wininet callbacks.
    void        onWininetCallback(HINTERNET hInternet,
                                  DWORD dwInternetStatus,
                                  LPVOID pStatusInformation,
                                  DWORD dwStatusInformationLength);

    // shutdown the connection
    void        closeConnection();

    void        reportSendProgress();
    void        reportReceiveProgress();
    void        finalizeSendProgress();
    void        finalizeReceiveProgress();
    void        doSendProgressReport(size_t bytesProcessed,
                                     size_t totalBytes,
                                     double percent);
    void        doReceiveProgressReport(size_t bytesProcessed,
                                         size_t totalBytes,
                                         double percent);
    
    // Internal Attributes
private:
    // A debug-only runtime consistency check to ensure that:
    // 1. m_id is still valid
    // 2. we're on the same thread as we were constructed on
    void consistencyCheck() const
    {
#ifdef DEBUG
        // valid ids are *even* numbers and they must be
        // less than the current value of s_id
        if (m_id >= s_id || m_id & 0x1 || m_id == 0xDEADBEEF) {
            DebugBreak();
        }

        // consistencyCheck is designed to run on the same thread
        // where the HTTP transaction was allocated
        if (bp::thread::Thread::currentThreadID() != m_threadId) {
            DebugBreak();
        }
#endif
    }
    

    // We asynchronously move thru a state machine in
    // processRequest.  This state represents the 
    // next thing to do.
    TranState       m_eState;

    // cancel() sets this, processRequest() checks it.
    // Don't use state in m_eState to avoid races since
    // cancel() can be called from another thread
    bool            m_bCancel;

    // has connection been closed?
    bool            m_bClosed;

    // Timeout support
    bp::time::Timer m_timer;
    void            setTimer() {
        m_timer.setMsec((unsigned int)(m_fTimeoutSecs*1000));
    }
    void            clearTimer() {
        m_timer.cancel();
    }

    // progress info
    IListenerWeakPtr m_pListener;
    size_t          m_sendTotalBytes;
    size_t          m_bytesSent;
    size_t          m_receiveTotalBytes;
    size_t          m_bytesReceived;
    bool            m_zeroSendProgressSent;
    bool            m_zeroReceiveProgressSent;
    bool            m_hundredSendProgressSent;
    bool            m_hundredReceiveProgressSent;
    double          m_lastSendProgressSent;
    double          m_lastReceiveProgressSent;

    bp::url::Url    m_redirectUrl;
    
    std::string     m_sUserAgent;
    double          m_fTimeoutSecs;
    bool            m_bUseHttps;
 
    RequestPtr      m_pRequest;
    HINTERNET       m_hinetSession;
    HINTERNET       m_hinetConnection;
    HINTERNET       m_hinetRequest;
    
    // Support for our data source/sink
    enum {
        eFromBuffer,
        eFromFile
    } m_ePostSource;
    HANDLE          m_hUploadFile;
    unsigned char*  m_pPostBuffer;
    DWORD           m_bytesToPost;
    DWORD           m_numWritten;
    unsigned char*  m_pReceiveBuffer;
    DWORD           m_bytesInReceiveBuffer;
    INTERNET_BUFFERS m_ibuf;
  
    // Wininet callback can be invoked on the wrong thread.
    // Must thread hop.
    static void        processRequestCB(void* ctx);
    static void        redirectCB(void* ctx);
    static void        closedCB(void* ctx);
    bp::thread::Hopper m_hopper;
    // the thread upon which we were allocated
    unsigned int       m_threadId;
    
    DWORD           m_error;

    // Sigh, wininet is happy to invoke callback even after
    // we've closed the handle.  Thus, we keep a map of active
    // impls and only invoke member callback if impl is 
    // still active.  Map manipulated in ctor/dtor.
    // Map must be thread protected since wininet callback
    // can occur on different thread.
    static bp::sync::Mutex s_lock;
    static DWORD           s_id;
    static std::map<DWORD, Transaction::Impl*> s_activeImpls;
    DWORD                  m_id;

    void addToImplMap() {
        s_lock.lock();
        m_id = s_id;
        s_id += 2;
#ifdef DEBUG
        // a runtime debugging check, ensure that this id isn't
        // currently in use!
        if (s_activeImpls.find(m_id) != s_activeImpls.end()) {
            DebugBreak();
        }
#endif
        s_activeImpls[m_id] = this;
        s_lock.unlock();
    }

    void removeFromImplMap() {
        s_lock.lock();
        s_activeImpls.erase(m_id);
        m_id = 0xDEADBEEF;
        s_lock.unlock();
    }

    static bool findImpl(DWORD id, Transaction::Impl * &found) {
        std::map<DWORD, Transaction::Impl*>::const_iterator it;
		bool rv = false;
        s_lock.lock();
        it = s_activeImpls.find(id);
        if (it != s_activeImpls.end()) {
            rv = true;
            found = it->second;
		}
        s_lock.unlock();
        return rv;
    }

    void setError(const std::string& msg) {
        BPLOG_ERROR_STRM(msg);
        m_eState = eError;
        IListenerPtr p = m_pListener.lock();
        if (p) p->onError(msg);
    }

    std::string wininetErrorString(DWORD error) {
        std::string rval;
        switch (error) {
        case ERROR_INTERNET_INVALID_CA:
            rval += "SSL error: valid certificate chain, untrusted root";
            break;
        case ERROR_INTERNET_SEC_CERT_CN_INVALID:
            rval += "SSL error: the certificate common name is incorrect";
            break;
        case ERROR_INTERNET_SEC_CERT_DATE_INVALID:
            rval += "SSL error: certificate expired";
            break;
        case ERROR_INTERNET_SEC_CERT_REVOKED:
            rval += "SSL error: certificate revoked";
            break;
        case ERROR_INTERNET_OUT_OF_HANDLES:
            rval = "No more handles could be generated at this time.";
            break;
        case ERROR_INTERNET_TIMEOUT:
            rval = "The request has timed out.";
            break;
        case ERROR_INTERNET_EXTENDED_ERROR:
            rval = "An extended error was returned from the server.";
            break;
       case ERROR_INTERNET_INTERNAL_ERROR:
           rval = "An internal error has occurred.";
           break;
        case ERROR_INTERNET_INVALID_URL:
            rval = "The URL is invalid.";
            break;
        case ERROR_INTERNET_UNRECOGNIZED_SCHEME:
            rval = "The URL scheme could not be recognized or is not supported.";
            break;
        case ERROR_INTERNET_NAME_NOT_RESOLVED:
            rval = "The server name could not be resolved.";
            break;
        case ERROR_INTERNET_PROTOCOL_NOT_FOUND:
            rval = "The requested protocol could not be located.";
            break;
        case ERROR_INTERNET_INVALID_OPTION:
            rval = "A request to InternetQueryOption or InternetSetOption "
                   "specified an invalid option value.";
            break;
        case ERROR_INTERNET_BAD_OPTION_LENGTH:
            rval = "The length of an option supplied to InternetQueryOption "
                   "or InternetSetOption is incorrect for the type of option "
                   "specified.";
            break;
        case ERROR_INTERNET_OPTION_NOT_SETTABLE:
            rval = "The request option cannot be set, only queried.";
            break;
        case ERROR_INTERNET_SHUTDOWN:
            rval = "The Win32 Internet function support is being shut down "
                   "or unloaded.";
            break;
        case ERROR_INTERNET_INVALID_OPERATION:
            rval = "The requested operation is invalid.";
            break;
        case ERROR_INTERNET_OPERATION_CANCELLED:
            rval = "The operation was canceled, usually because the handle on "
                   "which the request was operating was closed before the "
                   "operation completed.";
            break;
        case ERROR_INTERNET_INCORRECT_HANDLE_TYPE:
            rval = "The type of handle supplied is incorrect for this operation";
            break;
        case ERROR_INTERNET_INCORRECT_HANDLE_STATE:
            rval = "The requested operation cannot be carried out because the "
                   "handle supplied is not in the correct state.";
            break;
        case ERROR_INTERNET_NOT_PROXY_REQUEST:
            rval = "The request cannot be made via a proxy.";
            break;
        case ERROR_INTERNET_REGISTRY_VALUE_NOT_FOUND:
            rval = "A required registry value could not be located.";
            break;
        case ERROR_INTERNET_BAD_REGISTRY_PARAMETER:
            rval = "A required registry value was located but is an incorrect "
                   "type or has an invalid value.";
            break;
        case ERROR_INTERNET_NO_DIRECT_ACCESS:
            rval = "Direct network access cannot be made at this time.";
            break;
        case ERROR_INTERNET_NO_CONTEXT:
            rval = "An asynchronous request could not be made because a zero "
                   " context value was supplied.";
            break;
        case ERROR_INTERNET_NO_CALLBACK:
            rval = "An asynchronous request could not be made because a "
                   "callback function has not been set.";
            break;
        case ERROR_INTERNET_REQUEST_PENDING:
            rval = "The required operation could not be completed because one "
                   "or more requests are pending.";
            break;
        case ERROR_INTERNET_INCORRECT_FORMAT:
            rval = "The format of the request is invalid.";
            break;
        case ERROR_INTERNET_ITEM_NOT_FOUND:
            rval = "The requested item could not be located.";
            break;
        case ERROR_INTERNET_CANNOT_CONNECT:
            rval = "The attempt to connect to the server failed.";
            break;
        case ERROR_INTERNET_CONNECTION_ABORTED:
            rval = "The connection with the server has been terminated.";
            break;
        case ERROR_INTERNET_CONNECTION_RESET:
            rval = "The connection with the server has been reset.";
            break;
        case ERROR_INTERNET_FORCE_RETRY:
            rval = "Calls for the Win32 Internet function to redo the request.";
            break;
        case ERROR_INTERNET_INVALID_PROXY_REQUEST:
            rval = "The request to the proxy was invalid.";
            break;
        case ERROR_INTERNET_HANDLE_EXISTS:
            rval = "The request failed because the handle already exists.";
            break;
        case ERROR_INTERNET_HTTP_TO_HTTPS_ON_REDIR:
            rval = "The application is moving from a non-SSL to an SSL "
                   "connection because of a redirect.";
            break;
        case ERROR_INTERNET_HTTPS_TO_HTTP_ON_REDIR:
            rval = "The application is moving from an SSL to an non-SSL "
                   "connection because of a redirect.";
            break;
        case ERROR_INTERNET_MIXED_SECURITY:
            rval = "Indicates that the content is not entirely secure. "
                   "Some of the content being viewed may have come from "
                   "unsecured servers.";
            break;
        case ERROR_INTERNET_CHG_POST_IS_NON_SECURE:
            rval = "The application is posting and attempting to change "
                   "multiple lines of text on a server that is not secure.";
            break;
        case ERROR_INTERNET_POST_IS_NON_SECURE:
            rval = "The application is posting data to a server that is not secure.";
            break;
        case ERROR_HTTP_HEADER_NOT_FOUND:
            rval = "The requested header could not be located.";
            break;
        case ERROR_HTTP_DOWNLEVEL_SERVER:
            rval = "The server did not return any headers.";
            break;
        case ERROR_HTTP_INVALID_SERVER_RESPONSE:
            rval = "The server response could not be parsed.";
            break;
        case ERROR_HTTP_INVALID_HEADER:
            rval = "The supplied header is invalid.";
            break;
        case ERROR_HTTP_INVALID_QUERY_REQUEST:
            rval = "The request made to HttpQueryInfo is invalid.";
            break;
        case ERROR_HTTP_HEADER_ALREADY_EXISTS:
            rval = "The header could not be added because it already exists.";
            break;
        case ERROR_HTTP_REDIRECT_FAILED:
            rval = "The redirection failed because either the scheme changed "
                   "(for example, HTTP to FTP) or all attempts made to redirect "
                   "failed (default is five attempts).";
            break;
        default: 
            {
            std::stringstream ss;
            ss << "Unknown wininet error " << error;
            rval = ss.str(); 
            }
        }
        return rval;
    }

    // Prevent copying
private:    
    Impl(const Impl&);
    Impl& operator=(const Impl&);
};


///////////////////////////////////////////////////////////////////////////
// Transaction::Impl Static Members
//
const std::string Transaction::Impl::ksDefaultUserAgent = "Yahoo! BrowserPlus (win32)";
const double Transaction::Impl::kfDefaultTimeoutSecs = 30.0;
const DWORD Transaction::Impl::kBufferSize = 16*1024;

bp::sync::Mutex Transaction::Impl::s_lock;
DWORD Transaction::Impl::s_id = 1000;
std::map<DWORD, Transaction::Impl*> Transaction::Impl::s_activeImpls;

// each of these xxxCB calls will be invoked on the thread where the
// HttpTransaction was allocated, their primary purpose is to proxy a 
// call from wininent into the appropriate object.   NOTE:  See bug
// {#6}, because of the threaded nature of wininet, it's possible that
// the instance has been deleted by the time we get here.  To catch
// that case we ensure that the impl is still present in s_activeImpls.
// Final note:  the use of ids rather than memory addresses is important!
// The win32 allocator tends to aggressively reuse memory.

void 
Transaction::Impl::processRequestCB(void* ctx)
{
    Transaction::Impl* self = NULL;

    if (!findImpl((DWORD) ctx, self)) {
        BPLOG_WARN_STRM("Dropping processRequest call, implementation has been free'd");
    } else {
        self->processRequest(self->m_error);
    }
}

void 
Transaction::Impl::redirectCB(void* ctx)
{
    Transaction::Impl* self = NULL;

    if (!findImpl((DWORD) ctx, self)) {
        BPLOG_WARN_STRM("Dropping redirect call, implementation has been free'd");
    } else {
        IListenerPtr p = self->m_pListener.lock();
        if (p) p->onRedirect(self->m_redirectUrl);
    }
}

void 
Transaction::Impl::closedCB(void* ctx)
{
    Transaction::Impl* self = NULL;

    if (!findImpl((DWORD) ctx, self)) {
        BPLOG_WARN_STRM("Dropping closed call, implementation has been free'd");
    } else {
        IListenerPtr p = self->m_pListener.lock();
        if (p) p->onClosed();
    }
}


///////////////////////////////////////////////////////////////////////////
// Transaction::Impl Public Methods
//

Transaction::Impl::Impl(RequestPtr ptrRequest) :
    m_eState(eConnect),
    m_sUserAgent(ksDefaultUserAgent),
    m_fTimeoutSecs(kfDefaultTimeoutSecs),
    m_bUseHttps(false),
    m_pRequest(ptrRequest),
    m_hinetSession(NULL),
    m_hinetConnection(NULL),
    m_hinetRequest(NULL),
    m_hUploadFile(INVALID_HANDLE_VALUE),
    m_pPostBuffer(NULL),
    m_bytesToPost(0),
    m_pListener(),
    m_sendTotalBytes(0),
    m_bytesSent(0),
    m_receiveTotalBytes(0),
    m_bytesReceived(0),
    m_zeroSendProgressSent(false),
    m_zeroReceiveProgressSent(false),
    m_hundredSendProgressSent(false),
    m_hundredReceiveProgressSent(false),
    m_lastSendProgressSent(0),
    m_lastReceiveProgressSent(0),
    m_threadId(bp::thread::Thread::currentThreadID()),
    m_error(ERROR_SUCCESS),
    m_bCancel(false),
    m_bClosed(false),
    m_timer()
{
    m_timer.setListener(this);
    m_hopper.initializeOnCurrentThread();

    m_pReceiveBuffer = new unsigned char[kBufferSize];
    if (!m_pReceiveBuffer) {
        BP_THROW_FATAL("cannot allocate m_pReceiveBuffer");
    }

    // addToImplMap will initialize m_id to a process-wide unique
    // id used to identify this transaction and will add the
    // instance to a static protected map.  This map is used
    // to correlate ids to instances for events posted cross thread
    // from WinINET worker threads.
    addToImplMap();
    BPLOG_DEBUG_STRM(m_id << ": HTTP transaction registered");
}


Transaction::Impl::~Impl()
{
    consistencyCheck();

    BPLOG_DEBUG_STRM(m_id << ":  HTTP transaction unregistered");
    removeFromImplMap();
    m_pListener.reset();

    closeConnection();

    if (m_ePostSource == eFromFile) {
        CloseHandle(m_hUploadFile);
        delete [] m_pPostBuffer;
        m_pPostBuffer = NULL;
    }
    if (m_pReceiveBuffer) {
        delete [] m_pReceiveBuffer;
        m_pReceiveBuffer = NULL;
    }
}


// Class-scoped static function
void CALLBACK
Transaction::Impl::wininetCallback(HINTERNET hInternet,
                                   DWORD_PTR dwContext,
                                   DWORD dwInternetStatus,
                                   LPVOID pStatusInfo,
                                   DWORD dwStatusInfoLength)
{
    // Forward to member function.
    std::map<DWORD, Transaction::Impl*>::const_iterator it;
    s_lock.lock();
    it = s_activeImpls.find((DWORD) dwContext);
    if (it == s_activeImpls.end()) {
        BPLOG_DEBUG_STRM("late wininet callback for deleted HTTP instance "
                         "ignored: " << (DWORD) dwContext);
        s_lock.unlock();
        return;
    }
    Transaction::Impl* pTran = (Transaction::Impl*) it->second;
    pTran->onWininetCallback(hInternet, dwInternetStatus,
                             pStatusInfo, dwStatusInfoLength);
    s_lock.unlock();
}


void
Transaction::Impl::timesUp(bp::time::Timer* t)
{
    consistencyCheck();

    BPLOG_INFO_STRM(m_id << ": timer fired");
    if (t) {
        t->cancel();
    }
    m_eState = eTimedOut;
    m_hopper.invokeOnThread(processRequestCB, (void *) m_id);
}


void
Transaction::Impl::initiate(IListenerWeakPtr pListener)
{
    consistencyCheck();

    m_pListener = pListener;
    processRequest(ERROR_SUCCESS);
}


void 
Transaction::Impl::cancel()
{
    m_bCancel = true;
    // (YIB-2309323) if we're called on the controlling thread,
    // we should invoke the cancel callback before we return for
    // symmetry with the OSX interface.
    if (bp::thread::Thread::currentThreadID() == m_threadId) {
        processRequest(ERROR_SUCCESS);
    }
}


RequestPtr
Transaction::Impl::request() const
{
    consistencyCheck();

    return m_pRequest;
}


const std::string& 
Transaction::Impl::userAgent() const
{
    consistencyCheck();

    return m_sUserAgent;
}


void 
Transaction::Impl::setUserAgent(const std::string& sUserAgent)
{
    consistencyCheck();

    m_sUserAgent = sUserAgent;
}


double
Transaction::Impl::timeoutSec() const
{
    consistencyCheck();

    return m_fTimeoutSecs;
}


void
Transaction::Impl::setTimeoutSec(double fSecs)
{
    consistencyCheck();

    m_fTimeoutSecs = fSecs;
}



///////////////////////////////////////////////////////////////////////////
// Transaction::Impl Internal Methods
//
void
Transaction::Impl::processRequest(DWORD error)
{
    consistencyCheck();

    static const char* stateNames[] = {
        "eConnect", "eOpenRequest", 
        "eSendRequest", "eSendRequestWithBody", 
        "ePostGetData", "ePostSendData", "ePostComplete",
        "eResponseGetHeaders", "eResponseReceiveData", 
        "eResponseWriteData", "eDone", "eTimedOut", "eError"
    };
    
    BPLOG_DEBUG_STRM(m_id << ": processRequest (ec: " << error << ")");

    // do nothing if connection has been closed
    if (m_bClosed) {
        BPLOG_WARN_STRM(m_id
                        << ": connect had been closed, request ignored, "
                        << "state = " << stateNames[m_eState]);
        return;
    }

    // handle failures (which manifest as an error code
    // passed by the REQUEST COMPLETE wininet message 
    if (error != ERROR_SUCCESS && error != ERROR_IO_PENDING) {
        setError(wininetErrorString(error));
        closeConnection();        
        return;
    }

    IListenerPtr listener = m_pListener.lock();
    while (!m_bCancel                    // not cancelled
           && m_eState != eTimedOut      // haven't timed out
           && m_eState != eError         // no error
           && m_eState != eDone          // not finished
           && error == ERROR_SUCCESS) {  // not waiting on i/o
        BPLOG_DEBUG_STRM(m_id << ": state = " << stateNames[m_eState]);
        switch (m_eState) {

        case eConnect:
            m_eState = eOpenRequest;
            if (listener) listener->onConnecting();
            openSession();
            error = openConnection();
            break;

        case eOpenRequest:
            if (m_pRequest->method.code() == Method::HTTP_POST) {
                m_eState = eSendRequestWithBody;
            } else {
                m_eState = eSendRequest;
            }
            error = openRequest();
            break;

        case eSendRequest:
            m_eState = eResponseGetHeaders;
            if (listener) listener->onConnected();
            error = sendRequest();
            break;

        case eSendRequestWithBody:
            m_eState = ePostGetData;
            if (listener) listener->onConnected();
            error = sendRequestWithBody();
            break;
            
        case ePostGetData: {
            m_eState = ePostSendData;
            m_bytesSent += m_bytesToPost;  // catch delayed writes
            reportSendProgress();
            error = getDataToPost();
            break;
        }

        case ePostSendData: {
            m_eState = ePostGetData;
            bool done = false;
            error = postData(done);
            if (done) {
                m_eState = ePostComplete;
            }
            break;
        }

        case ePostComplete:
            m_eState = eResponseGetHeaders;
            error = completePostRequest();
            break;

        case eResponseGetHeaders: {
            m_eState = eResponseReceiveData;
            Version version = receiveResponseVersion();
            Status status = receiveResponseStatus();
            Headers headers = receiveResponseHeaders();
            if (listener) {
                listener->onRequestSent();
                listener->onResponseStatus(status, headers);
            }
            break;
        }

        case eResponseReceiveData:
            // since we use a non-blocking InternetReadFileEx, must only
            // advance state if we actually got data
            error = receiveResponseData();
            if (error != ERROR_IO_PENDING) {
                m_eState = eResponseWriteData;
            }
            break;

        case eResponseWriteData: {
            // {#139} honor 0% and 100% guarantees for send progress.
            // at the point we're fetching the response, we've
            // already sent the request
            finalizeSendProgress();

            m_eState = eResponseReceiveData;
            bool done = false;
            error = writeResponseData(done);

            reportReceiveProgress();
            
            if (listener) listener->onResponseBodyBytes(m_pReceiveBuffer, 
                                                        m_bytesInReceiveBuffer);
            if (done) {
                m_eState = eDone;
                finalizeReceiveProgress();
                if (listener) listener->onComplete();
                // now we'll invoke closed after an async break so that
                // if we're deleted on the onClosed call, we don't
                // go and try to romp around in our memory later.
                m_hopper.invokeOnThread(closedCB, (void *) m_id);
            }
            break;
        }
        }
    }

    // deal with the terminal stuff
    if (m_bCancel) {
        closeConnection();
        if (listener) listener->onCancel();
    } else if (m_eState == eTimedOut) {
        closeConnection();
        if (listener) listener->onTimeout();
    }
}


void
Transaction::Impl::openSession()
{
    consistencyCheck();

    BPLOG_DEBUG_STRM(m_id << ": openSession");
    // TODO: Does a UserAgent header override lpszAgent arg to this func?
    // Note: this call is synchronous.
    wstring wsUserAgent = bp::strutil::utf8ToWide(m_sUserAgent);
    m_hinetSession = InternetOpenW(wsUserAgent.c_str(),
                                   INTERNET_OPEN_TYPE_PRECONFIG,
                                   NULL,
                                   NULL,
                                   INTERNET_FLAG_ASYNC);
    if (m_hinetSession == NULL) {
        setError(lastErrorString("InternetOpen"));
        return;
    }

    INTERNET_STATUS_CALLBACK isc = InternetSetStatusCallback(m_hinetSession,
                                                             wininetCallback);
    if (isc == INTERNET_INVALID_STATUS_CALLBACK) {
        setError("InternetSetStatusCallback failed.");
        return;
    } 
}


DWORD
Transaction::Impl::openConnection()
{
    consistencyCheck();

    BPLOG_INFO_STRM(m_id << ": openConnection");

    // TODO: (longer term) it's possible to cache connection handles
    // into a pool for better performance.
    DWORD rval = ERROR_SUCCESS;
    wstring wsHost = bp::strutil::utf8ToWide(m_pRequest->url.host());
    m_hinetConnection = InternetConnectW(m_hinetSession,
                                         wsHost.c_str(),
                                         (INTERNET_PORT)m_pRequest->url.port(),
                                         NULL,
                                         NULL,
                                         INTERNET_SERVICE_HTTP,
                                         0,
                                         (DWORD_PTR)m_id);
    if (m_hinetConnection != NULL) {
        clearTimer();
    } else {
        rval = GetLastError();
        if (rval == ERROR_IO_PENDING) {
            setTimer();
        } else {
            // Could also call InternetGetLastResponseInfo here.
            setError(lastErrorString("InternetConnect"));
        }
    }
    return rval;
}


DWORD
Transaction::Impl::openRequest()
{
    consistencyCheck();

    BPLOG_DEBUG_STRM(m_id << ": openRequest");
    
    // Bypass the browser cache.
    DWORD dwRequestFlags = INTERNET_FLAG_RELOAD | 
                           INTERNET_FLAG_NO_CACHE_WRITE |
                           INTERNET_FLAG_NO_COOKIES;
    if (m_pRequest->url.scheme() == "https") {
        dwRequestFlags |= INTERNET_FLAG_SECURE;
    }

    wstring wsMethod = bp::strutil::utf8ToWide(m_pRequest->method.toString());
    wstring wsResource = bp::strutil::utf8ToWide(
        m_pRequest->url.pathAndQueryString());

    // Create a Request handle
    DWORD rval = ERROR_SUCCESS;
    m_hinetRequest = HttpOpenRequestW(m_hinetConnection,
                                      wsMethod.c_str(),
                                      wsResource.c_str(),  
                                      NULL, // Use version HTTP/1.1
                                      NULL, // Do not provide any referrer
                                      NULL, // Do not provide Accept types
                                      dwRequestFlags,
                                      (DWORD_PTR)m_id);
    if (m_hinetRequest != NULL) {
        clearTimer();
        // set timeouts
        unsigned long int to = (unsigned long int) (m_fTimeoutSecs * 1000.0);
        InternetSetOption(m_hinetRequest, INTERNET_OPTION_RECEIVE_TIMEOUT,
                          (void *) &to, sizeof(to));
        InternetSetOption(m_hinetRequest, INTERNET_OPTION_SEND_TIMEOUT,
                          (void *) &to, sizeof(to));
        InternetSetOption(m_hinetRequest, INTERNET_OPTION_CONNECT_TIMEOUT,
                          (void *) &to, sizeof(to));
    } else {
        rval = GetLastError();
        if (rval == ERROR_IO_PENDING) {
            setTimer();
        } else {
            setError(lastErrorString("HttpOpenRequest"));
        }
    }
    return rval;
}


DWORD
Transaction::Impl::sendRequest()
{
    consistencyCheck();

    BPASSERT(m_pRequest->method.code() != bp::http::Method::HTTP_POST);
    BPLOG_DEBUG_STRM(m_id << ": sendRequest");
   
    // If we have request headers, add them to request.
    Headers& headers = m_pRequest->headers;
    // We add Connection: close to disable HTTP keepalive.
    // (causing the correct events to be sent to us)
    headers.add("Connection", "close");
    if (!headers.empty()) {
        wstring wsHeaders = bp::strutil::utf8ToWide(headers.toString());
        BOOL bRet = HttpAddRequestHeadersW(m_hinetRequest, 
                                           wsHeaders.c_str(),
                                           DWORD(-1), 
                                           HTTP_ADDREQ_FLAG_ADD_IF_NEW);
        if (!bRet) {
            setError(lastErrorString("HttpAddRequestHeaders"));
            return GetLastError();
        }
    }

    DWORD rval = ERROR_SUCCESS;
    if (HttpSendRequest(m_hinetRequest, NULL, 0, NULL, 0)) {
        clearTimer();
    } else {
        rval = GetLastError();
        if (rval == ERROR_IO_PENDING) {
            setTimer();
        } else {
            setError(lastErrorString("HttpSendRequest"));
        }
    }
    return rval;
}


DWORD
Transaction::Impl::sendRequestWithBody()
{
    consistencyCheck();

    BPASSERT(m_pRequest->method.code() == bp::http::Method::HTTP_POST);
    BPLOG_INFO_STRM(m_id << ": sendRequestWithBody");
    
    // If we have request headers, add them to request.
    Headers& headers = m_pRequest->headers;
    // We disable keepalive by adding Connection: close
    headers.add("Connection", "close");
    if (!headers.empty()) {
        wstring wsHeaders = bp::strutil::utf8ToWide(headers.toString());
        if (!HttpAddRequestHeadersW(m_hinetRequest, 
                                    wsHeaders.c_str(),
                                    DWORD(-1), 
                                    HTTP_ADDREQ_FLAG_ADD_IF_NEW)) {
            setError(lastErrorString("HttpAddRequestHeaders"));
            return GetLastError();
        }
    }

    m_sendTotalBytes = 0;
    boost::filesystem::path path = m_pRequest->body.path();
    if (!path.empty()) {
        m_ePostSource = eFromFile;
        m_hUploadFile = CreateFileW(bp::file::nativeString(path).c_str(),
                                    GENERIC_READ,
                                    FILE_SHARE_READ,
                                    NULL,
                                    OPEN_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL,
                                    NULL);
        if (m_hUploadFile == INVALID_HANDLE_VALUE) {
            setError(lastErrorString("CreateFileW"));
            return GetLastError();
        }
        m_pPostBuffer = new unsigned char[kBufferSize];
        if (m_pPostBuffer == NULL) {
            setError("unable to allocate buffer");
            return GetLastError();
        }
        m_sendTotalBytes = (size_t) bp::file::size(path);
	} else {
        m_ePostSource = eFromBuffer;
        m_sendTotalBytes = m_pRequest->body.size();
    }

    // setup buffer for HttpSendRequestEx
    FillMemory(&m_ibuf, sizeof(m_ibuf), 0);
    m_ibuf.dwStructSize = sizeof(m_ibuf);
    m_ibuf.dwBufferTotal = m_sendTotalBytes;

    DWORD rval = ERROR_SUCCESS;
    if (HttpSendRequestEx(m_hinetRequest, &m_ibuf, NULL, 0, 0)) {
        clearTimer();
    } else {
        rval = GetLastError();
        if (rval == ERROR_IO_PENDING) {
            setTimer();
        } else {
            setError(lastErrorString("HttpSendRequestEx"));
        }
    }
    return rval;
}


DWORD
Transaction::Impl::getDataToPost()
{
    consistencyCheck();

    if (m_bytesSent >= m_sendTotalBytes) {
        m_bytesToPost = 0;
        return ERROR_SUCCESS;
    }

    DWORD rval = ERROR_SUCCESS;
    switch (m_ePostSource) {
    case eFromBuffer: {
        m_pPostBuffer = (unsigned char*) m_pRequest->body.elementAddr(m_bytesSent);
        int toPost = m_pRequest->body.size() - m_bytesSent;
        BPASSERT(toPost >= 0);
        if (toPost >= 0) {
            m_bytesToPost = toPost;
        }
        if (m_bytesToPost > kBufferSize) {
            m_bytesToPost = kBufferSize;
        }
        break;
    }
    case eFromFile:
        if (!ReadFile(m_hUploadFile, m_pPostBuffer, 
                      kBufferSize, &m_bytesToPost, NULL)) {
            setError(lastErrorString("ReadFile"));
            rval = GetLastError();
        }
        break;
    }
    return rval;
}


DWORD
Transaction::Impl::postData(bool& done)
{
    consistencyCheck();

    BPLOG_DEBUG_STRM(m_id << ": postData " << m_bytesToPost << " bytes");
    if (m_bytesToPost == 0) {
        done = true;
        return ERROR_SUCCESS;
    } 

    done = false;
    DWORD rval = ERROR_SUCCESS;
    // {#6} numWritten will be populated upon async completion of
    // internet write file.  doing something like, uh, using a stack
    // allocated DWORD will cause your dog to leave you and your wife
    // to file for divorce.  Likewise, m_pPostBuffer must hang around
    // till completion.  Completion is indicated by wininet throwing
    // us a INTERNET_STATUS_REQUEST_COMPLETE.
    m_numWritten = 0;
    if (InternetWriteFile(m_hinetRequest, m_pPostBuffer,
                          m_bytesToPost, &m_numWritten)) {
        clearTimer();
        BPLOG_DEBUG_STRM(m_id << ":postData wrote " << m_numWritten << " bytes");
        m_bytesToPost = 0;
        m_bytesSent += m_numWritten;
    } else {
        rval = GetLastError();
        if (rval == ERROR_IO_PENDING) {
            setTimer();
            BPLOG_DEBUG_STRM(m_id << ": postData delayed write,  " 
                             << m_bytesToPost << " bytes");
        } else {
            setError(lastErrorString("InternetWriteFile"));
            rval = GetLastError();
        }
    }
    return rval;
}


DWORD
Transaction::Impl::completePostRequest()
{
    consistencyCheck();

    BPLOG_DEBUG_STRM(m_id << ": completePostRequest");
    DWORD rval = ERROR_SUCCESS;
    if (HttpEndRequest(m_hinetRequest, NULL, 0, 0)) {
        clearTimer();
        BPLOG_DEBUG_STRM(m_id << ": request ended");
    } else {
        rval = GetLastError();
        if (rval == ERROR_IO_PENDING) {
            setTimer(); 
        } else {
            setError(lastErrorString("HttpEndRequest"));
            rval = GetLastError();
        }
    }
    return rval;
}


Version
Transaction::Impl::receiveResponseVersion()
{
    consistencyCheck();

    Version rval;
    const int nBUFLEN = 256;
    char szBuf[nBUFLEN];
    DWORD dwSize = nBUFLEN;
    DWORD dwIndex = 0;

    if (HttpQueryInfoA(m_hinetRequest, 
                       HTTP_QUERY_VERSION,
                       szBuf, &dwSize, &dwIndex)) {
        rval = Version(szBuf);
    } else {
        setError(lastErrorString("HttpQueryInfo for version"));
    }
    clearTimer();
    return rval;
}


Status
Transaction::Impl::receiveResponseStatus()
{
    consistencyCheck();

    int nStatusCode = 0;
    DWORD dwSize = sizeof(int);
    DWORD dwIndex = 0;

    if (!HttpQueryInfo(m_hinetRequest, 
                       HTTP_QUERY_STATUS_CODE|HTTP_QUERY_FLAG_NUMBER,
                       &nStatusCode, &dwSize, &dwIndex)) {
        setError(lastErrorString("HttpQueryInfo for status code"));
    } 
    clearTimer();
    return Status(static_cast<Status::Code>(nStatusCode));
}


Headers
Transaction::Impl::receiveResponseHeaders()
{
    consistencyCheck();

    Headers rval;
    ByteVec vb;
    vb.reserve(1000);
    vb.push_back(0); // per pbroman, needed for vs05
    DWORD dwIndex = 0;
    DWORD dwLength = vb.capacity();
    BOOL bStat = HttpQueryInfoA(m_hinetRequest, HTTP_QUERY_RAW_HEADERS_CRLF,
                                &vb[0], &dwLength, &dwIndex);
    if (!bStat && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        // Required size is now in dwLength.
        vb.reserve(dwLength);

        // Try again.
        bStat = HttpQueryInfoA(m_hinetRequest, HTTP_QUERY_RAW_HEADERS_CRLF,
                               &vb[0], &dwLength, &dwIndex);
    }

    if (!bStat) {
        setError(lastErrorString("HttpQueryInfo for response headers"));
        return rval;
    }

    clearTimer();

    // We used a c-api to fill our vector.  So size isn't right.
    // Load a string from the valid portion of the vector.
    std::string sHeaders(&vb[0], &vb[0]+dwLength);

    // For wininet, the first line is the http start line, not a header.
    // So Headers::add() won't consider this a valid input string.
    // Gotta strip the start line.
    std::string::size_type nNewlinePos = sHeaders.find('\n');
    if (nNewlinePos != std::string::npos) {
        sHeaders = sHeaders.substr(nNewlinePos+1);
    }

    // Load the response headers from the now well-formed string.
    rval.add(sHeaders);

    // look for content-length
    Headers::const_iterator it;
    for (it = rval.begin(); it != rval.end(); ++it) {
        if (it->first.compare(Headers::ksContentLength) == 0) {
            try {
                m_receiveTotalBytes = bp::conv::lexical_cast<int>(it->second);
            } catch(const std::bad_cast&) {
                m_receiveTotalBytes = 0;
            }
            break;
        }
    }
    return rval;
}


DWORD
Transaction::Impl::receiveResponseData()
{
    consistencyCheck();

    BPLOG_DEBUG_STRM(m_id << ": receiveResponseData");    
    DWORD rval = ERROR_SUCCESS;

    /* (lth) wininet.dll that ships with IE6 does not implement
     * InternetReadFileExW.  We must use InternetReadFileExA to
     * support winxp machines (any service pack) that have IE6
     * installed.  (IE is the software that provides/updates wininet.
     * yuck). (YIB-2822591) */
    INTERNET_BUFFERSA ibuf;
    FillMemory(&ibuf, sizeof(ibuf), 0);
    ibuf.dwStructSize = sizeof(ibuf);
    ibuf.lpvBuffer = m_pReceiveBuffer;
    ibuf.dwBufferLength = kBufferSize;

    if (InternetReadFileExA(m_hinetRequest, &ibuf, IRF_NO_WAIT,
                            (DWORD_PTR) m_id)) {
        clearTimer();
        m_bytesInReceiveBuffer = ibuf.dwBufferLength;
        BPLOG_DEBUG_STRM(m_id << ": InternetReadFileEx got " 
                         << m_bytesInReceiveBuffer << " bytes");
    } else {
        m_bytesInReceiveBuffer = 0;
        rval = GetLastError();
        if (rval == ERROR_IO_PENDING) {
            setTimer();
        } else {
            setError(lastErrorString("InternetReadFileEx"));
            return rval;
        }
    }
    return rval;
}


DWORD
Transaction::Impl::writeResponseData(bool& done)
{
    consistencyCheck();

    if (m_bytesInReceiveBuffer == 0) {
        done = true;
        return ERROR_SUCCESS;
    }

    done = false;
    m_bytesReceived += m_bytesInReceiveBuffer;
    return ERROR_SUCCESS;
}


void
Transaction::Impl::closeConnection()
{
    m_bClosed = true;
    clearTimer();
    if (m_hinetRequest != INVALID_HANDLE_VALUE) {
        (void) InternetSetStatusCallback(m_hinetRequest, NULL);
        InternetCloseHandle(m_hinetRequest);
        m_hinetRequest = INVALID_HANDLE_VALUE;
    }
    if (m_hinetConnection != INVALID_HANDLE_VALUE) {
        (void) InternetSetStatusCallback(m_hinetConnection, NULL);
        InternetCloseHandle(m_hinetConnection);
        m_hinetConnection = INVALID_HANDLE_VALUE;
    }
    if (m_hinetSession != INVALID_HANDLE_VALUE) {
        (void) InternetSetStatusCallback(m_hinetSession, NULL);
        InternetCloseHandle(m_hinetSession);
        m_hinetSession = INVALID_HANDLE_VALUE;
    }
}


void
Transaction::Impl::onWininetCallback(HINTERNET /* hInternet */,
                                     DWORD dwInternetStatus,
                                     LPVOID pStatusInfo,
                                     DWORD /* dwStatusInfoLength */)
{
    switch (dwInternetStatus) {
    case INTERNET_STATUS_COOKIE_SENT:
        BPLOG_DEBUG_STRM(m_id << ": Status: Cookie found and will be sent with request");
        break;
            
    case INTERNET_STATUS_COOKIE_RECEIVED:
        BPLOG_DEBUG_STRM(m_id << ": Status: Cookie Received");
        break;
            
    case INTERNET_STATUS_COOKIE_HISTORY: {
        InternetCookieHistory cookieHistory;
        BPLOG_DEBUG_STRM(m_id << ": Status: Cookie History");
        cookieHistory = *((InternetCookieHistory*)pStatusInfo);
            
        if(cookieHistory.fAccepted) {
            BPLOG_DEBUG_STRM(m_id << "Cookie Accepted");
        }
        if(cookieHistory.fLeashed) {
            BPLOG_DEBUG_STRM(m_id << "Cookie Leashed");
        }        
        if(cookieHistory.fDowngraded) {
            BPLOG_DEBUG_STRM(m_id << "Cookie Downgraded");
        }        
        if(cookieHistory.fRejected) {
            BPLOG_DEBUG_STRM(m_id << "Cookie Rejected");
        }
        break;
    }
            
    case INTERNET_STATUS_CONNECTED_TO_SERVER: {
        BPLOG_INFO_STRM(m_id << ": HTTP transaction connected to Server");
        break;
    }
            
    case INTERNET_STATUS_CONNECTING_TO_SERVER:
        BPLOG_DEBUG_STRM(m_id << ": HTTP transaction connecting to Server");
        break;
            
    case INTERNET_STATUS_CLOSING_CONNECTION:
        BPLOG_DEBUG_STRM(m_id << ": HTTP transaction - closing Connection (one of perhaps many)");
        break;
            
    case INTERNET_STATUS_CONNECTION_CLOSED:
        BPLOG_INFO_STRM(m_id << ": HTTP transaction - connection closed (one of perhaps many)");
        break;
            
    case INTERNET_STATUS_HANDLE_CLOSING:
        BPLOG_DEBUG_STRM(m_id << ": HTTP transaction - WinINET handle Closing");
        break;
            
    case INTERNET_STATUS_HANDLE_CREATED: {
        INTERNET_ASYNC_RESULT* pRes = (INTERNET_ASYNC_RESULT*) pStatusInfo;
        HINTERNET hinetNew = (HINTERNET)pRes->dwResult;
        switch (m_eState) {
        case eOpenRequest:
            m_hinetConnection = hinetNew;
            break;
        case eSendRequest:
        case eSendRequestWithBody:
            m_hinetRequest = hinetNew;
            break;
        default:
            BPLOG_INFO_STRM(m_id << ": Unexpected HANDLE_CREATED callback");
            break;
        }
        break;
    }
            
    case INTERNET_STATUS_INTERMEDIATE_RESPONSE:
        BPLOG_DEBUG_STRM(m_id << ": HTTP transaction status: Intermediate response");
        break;
            
    case INTERNET_STATUS_RECEIVING_RESPONSE:
        BPLOG_DEBUG_STRM(m_id << ": HTTP transaction status: Receiving Response");    
        break;
            
    case INTERNET_STATUS_RESPONSE_RECEIVED:
        BPLOG_INFO_STRM(m_id << ": HTTP transaction received response of " 
                         << *((LPDWORD)pStatusInfo) << " bytes");
        break;
            
    case INTERNET_STATUS_REDIRECT:
		{
			std::string s = bp::strutil::wideToUtf8((const wchar_t*) pStatusInfo);
			BPLOG_INFO_STRM(m_id << ": HTTP transaction received redirect");
			BPLOG_DEBUG_STRM("Redirect to " << s);
			(void) m_redirectUrl.parse(s);
			m_hopper.invokeOnThread(redirectCB, (void *) m_id);
		}
        break;

    case INTERNET_STATUS_REQUEST_COMPLETE:
        BPLOG_DEBUG_STRM(m_id << ": HTTP transaction request is complete");
        m_error = ((INTERNET_ASYNC_RESULT*)pStatusInfo)->dwError;
        m_hopper.invokeOnThread(processRequestCB, (void *) m_id);
        break;
            
    case INTERNET_STATUS_REQUEST_SENT:
        BPLOG_DEBUG_STRM(m_id << ": HTTP transaction status: Request sent " 
                         << *((LPDWORD)pStatusInfo) << " bytes");
        break;
            
    case INTERNET_STATUS_DETECTING_PROXY:
        BPLOG_DEBUG_STRM(m_id << ": HTTP transaction status: Detecting Proxy");
        break;            
            
    case INTERNET_STATUS_RESOLVING_NAME:
        BPLOG_DEBUG_STRM(m_id << ": HTTP transaction status: Resolving Name");
        break;
            
    case INTERNET_STATUS_NAME_RESOLVED:
        BPLOG_DEBUG_STRM(m_id << ": HTTP transaction status: Name Resolved");
        break;
            
    case INTERNET_STATUS_SENDING_REQUEST:
        BPLOG_DEBUG_STRM(m_id << ": HTTP transaction status: Sending request");
        break;
            
    case INTERNET_STATUS_STATE_CHANGE:
        BPLOG_DEBUG_STRM(m_id << ": HTTP transaction status: State Change");
        break;
            
    case INTERNET_STATUS_P3P_HEADER:
        BPLOG_DEBUG_STRM(m_id << ": HTTP transaction status: Received P3P header");
        break;
            
    default:
        BPLOG_WARN_STRM(m_id << ": HTTP transaction got unknown status: "
                        << dwInternetStatus);
        break;
    }
}


static double
calcPercent( double numerator, double denominator )
{
    // Two policy choices: return 0 if denom = 0
    //                     0 <= return val <= 100
    double percent = denominator ? (numerator/denominator)*100 : 0;
    return min(max(percent, 0.), 100.);
}


void
Transaction::Impl::reportSendProgress()
{
    if (!m_zeroSendProgressSent) {
        doSendProgressReport( 0, m_sendTotalBytes, 0 );
    }

    double percent = calcPercent( m_bytesSent, m_sendTotalBytes );
    if (percent > m_lastSendProgressSent) {
        doSendProgressReport( m_bytesSent, m_sendTotalBytes, percent );
    }
}


void
Transaction::Impl::reportReceiveProgress()
{
    if (!m_zeroReceiveProgressSent) {
        doReceiveProgressReport( 0, m_receiveTotalBytes, 0 );
    }

    // TODO: there's a weirdness here in the chunked encoding case.
    //       m_receiveTotalBytes will be zero in that case.
    double percent = calcPercent( m_bytesReceived, m_receiveTotalBytes );
    if (percent > m_lastReceiveProgressSent) {
        doReceiveProgressReport( m_bytesReceived, m_receiveTotalBytes,
                                 percent );
    }
}


void
Transaction::Impl::finalizeSendProgress()
{
    if (!m_zeroSendProgressSent) {
        doSendProgressReport( 0, m_sendTotalBytes, 0 );
    }
    if (!m_hundredSendProgressSent) {
        doSendProgressReport( m_sendTotalBytes, m_sendTotalBytes, 100 );
    }
}


void
Transaction::Impl::finalizeReceiveProgress()
{
    // m_receiveTotalBytes will be zero when Content-Length header is
    // absent, e.g. for response with chunked encoding.
    // TODO: normalize this with reportReceiveProgress.
    size_t totalBytes = (m_receiveTotalBytes > 0) ? m_receiveTotalBytes
                                                  : m_bytesReceived;

    if (!m_zeroReceiveProgressSent) {
        doReceiveProgressReport( 0, totalBytes, 0 );
    }
    if (!m_hundredReceiveProgressSent) {
        doReceiveProgressReport( totalBytes, totalBytes, 100 );
    }
}


void
Transaction::Impl::doSendProgressReport( size_t bytesProcessed,
                                         size_t totalBytes,
                                         double percent )
{
    IListenerPtr l = m_pListener.lock();
    if (l) l->onSendProgress( bytesProcessed, totalBytes, percent );

    m_lastSendProgressSent = percent;
    
    if (percent == 0) {
        m_zeroSendProgressSent = true;
    } else if (percent == 100) {
        m_hundredSendProgressSent = true;
    }
}


void
Transaction::Impl::doReceiveProgressReport( size_t bytesProcessed,
                                            size_t totalBytes,
                                            double percent )
{
    IListenerPtr l = m_pListener.lock();
    if (l) l->onReceiveProgress( bytesProcessed, totalBytes, percent );

    m_lastReceiveProgressSent = percent;

    if (percent == 0) {
        m_zeroReceiveProgressSent = true;
    } else if (percent == 100) {
        m_hundredReceiveProgressSent = true;
    }
}



///////////////////////////////////////////////////////////////////////////
// Transaction methods
//
// Note here we're using the "pImpl" or "Handle-body" idiom to keep
// os-specific members out of our .h file and instead declare an opaque pointer
// to impl.

Transaction::Transaction(RequestPtr ptrRequest) :
    m_pImpl(new Impl(ptrRequest))
{
    BPLOG_DEBUG_STRM("transaction to " <<  ptrRequest->url.toString());
}


Transaction::~Transaction()
{
}


double Transaction::defaultTimeoutSecs()
{
    return Transaction::Impl::kfDefaultTimeoutSecs;
}

        
void Transaction::initiate(IListenerWeakPtr pListener)
{
    IListenerPtr p = pListener.lock();
    if (p && p == NULL) {
        BP_THROW_FATAL("null listener");
    }
    m_pImpl->initiate(pListener);
}


void Transaction::cancel()
{
    return m_pImpl->cancel();
}


RequestPtr Transaction::request() const
{
    return m_pImpl->request();
}


const std::string& Transaction::userAgent() const
{
    return m_pImpl->userAgent();
}


void Transaction::setUserAgent(const std::string& sUserAgent)
{
    m_pImpl->setUserAgent(sUserAgent);
}


double Transaction::timeoutSec() const
{
    return m_pImpl->timeoutSec();
}


void Transaction::setTimeoutSec(double fSecs)
{
    m_pImpl->setTimeoutSec(fSecs);
}


} // namespace client
} // namespace http
} // namespace bp
