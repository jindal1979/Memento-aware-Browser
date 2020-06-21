// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_BROWSER_API_WEB_REQUEST_WEB_REQUEST_API_H_
#define EXTENSIONS_BROWSER_API_WEB_REQUEST_WEB_REQUEST_API_H_

#include <stdint.h>

#include <list>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/unique_ptr_adapters.h"
#include "base/feature_list.h"
#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/optional.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/global_request_id.h"
#include "extensions/browser/api/declarative/rules_registry.h"
#include "extensions/browser/api/declarative_webrequest/request_stage.h"
#include "extensions/browser/api/web_request/web_request_api_helpers.h"
#include "extensions/browser/api/web_request/web_request_permissions.h"
#include "extensions/browser/browser_context_keyed_api_factory.h"
#include "extensions/browser/event_router.h"
#include "extensions/browser/extension_api_frame_id_map.h"
#include "extensions/browser/extension_function.h"
#include "extensions/browser/extension_registry_observer.h"
#include "extensions/common/url_pattern_set.h"
#include "ipc/ipc_sender.h"
#include "net/base/auth.h"
#include "net/base/completion_once_callback.h"
#include "net/http/http_request_headers.h"
#include "services/network/public/mojom/url_loader_factory.mojom.h"
#include "services/network/public/mojom/websocket.mojom.h"

class ExtensionWebRequestTimeTracker;
class GURL;

namespace base {
class DictionaryValue;
}

namespace content {
class BrowserContext;
class RenderFrameHost;
}

namespace net {
class AuthChallengeInfo;
class AuthCredentials;
class HttpRequestHeaders;
class HttpResponseHeaders;
}

namespace extensions {

enum class WebRequestResourceType : uint8_t;

class WebRequestEventDetails;
struct WebRequestInfo;
class WebRequestRulesRegistry;

// Support class for the WebRequest API. Lives on the UI thread. Most of the
// work is done by ExtensionWebRequestEventRouter below. This class observes
// extensions::EventRouter to deal with event listeners. There is one instance
// per BrowserContext which is shared with incognito.
class WebRequestAPI : public BrowserContextKeyedAPI,
                      public EventRouter::Observer,
                      public ExtensionRegistryObserver {
 public:
  // A callback used to asynchronously respond to an intercepted authentication
  // request. If |should_cancel| is true the request will be cancelled.
  // Otherwise any supplied |credentials| will be used. If no credentials are
  // supplied, default browser behavior will follow (e.g. UI prompt for login).
  using AuthRequestCallback = base::OnceCallback<void(
      const base::Optional<net::AuthCredentials>& credentials,
      bool should_cancel)>;

  // An interface which is held by ProxySet defined below.
  class Proxy {
   public:
    virtual ~Proxy() {}

    // Asks the Proxy to handle an auth request on behalf of one of its known
    // in-progress network requests. If the request will *not* be handled by
    // the proxy, |callback| should be invoked with |base::nullopt|.
    virtual void HandleAuthRequest(
        const net::AuthChallengeInfo& auth_info,
        scoped_refptr<net::HttpResponseHeaders> response_headers,
        int32_t request_id,
        AuthRequestCallback callback);
  };

  // A ProxySet is a set of proxies used by WebRequestAPI: It holds Proxy
  // instances, and removes all proxies when it is destroyed.
  class ProxySet {
   public:
    ProxySet();
    ~ProxySet();

    // Add a Proxy.
    void AddProxy(std::unique_ptr<Proxy> proxy);
    // Remove a Proxy. The removed proxy is deleted upon this call.
    void RemoveProxy(Proxy* proxy);

    // Associates |proxy| with |id|. |proxy| must already be registered within
    // this ProxySet.
    //
    // Each Proxy may be responsible for multiple requests, but any given
    // request identified by |id| must be associated with only a single proxy.
    void AssociateProxyWithRequestId(Proxy* proxy,
                                     const content::GlobalRequestID& id);

    // Disassociates |proxy| with |id|. |proxy| must already be registered
    // within this ProxySet.
    void DisassociateProxyWithRequestId(Proxy* proxy,
                                        const content::GlobalRequestID& id);

    Proxy* GetProxyFromRequestId(const content::GlobalRequestID& id);

    void MaybeProxyAuthRequest(
        const net::AuthChallengeInfo& auth_info,
        scoped_refptr<net::HttpResponseHeaders> response_headers,
        const content::GlobalRequestID& request_id,
        AuthRequestCallback callback);

   private:
    // Although these members are initialized on the UI thread, we expect at
    // least one memory barrier before actually calling Generate in the IO
    // thread, so we don't protect them with a lock.
    std::set<std::unique_ptr<Proxy>, base::UniquePtrComparator> proxies_;

    // Bi-directional mapping between request ID and Proxy for faster lookup.
    std::map<content::GlobalRequestID, Proxy*> request_id_to_proxy_map_;
    std::map<Proxy*, std::set<content::GlobalRequestID>>
        proxy_to_request_id_map_;

    DISALLOW_COPY_AND_ASSIGN(ProxySet);
  };

  class RequestIDGenerator {
   public:
    RequestIDGenerator();
    ~RequestIDGenerator();

    // Generates a WebRequest ID. If the same (routing_id,
    // network_service_request_id) pair is passed to this as was previously
    // passed to SaveID(), the |request_id| passed to SaveID() will be returned.
    int64_t Generate(int32_t routing_id, int32_t network_service_request_id);

    // This saves a WebRequest ID mapped to the (routing_id,
    // network_service_request_id) pair. Clients must call Generate() with the
    // same ID pair to retrieve the |request_id|, or else there may be a memory
    // leak.
    void SaveID(int32_t routing_id,
                int32_t network_service_request_id,
                uint64_t request_id);

   private:
    int64_t id_ = 0;
    std::map<std::pair<int32_t, int32_t>, uint64_t> saved_id_map_;
    DISALLOW_COPY_AND_ASSIGN(RequestIDGenerator);
  };

  explicit WebRequestAPI(content::BrowserContext* context);
  ~WebRequestAPI() override;

  // BrowserContextKeyedAPI support:
  static BrowserContextKeyedAPIFactory<WebRequestAPI>* GetFactoryInstance();
  void Shutdown() override;

  // EventRouter::Observer overrides:
  void OnListenerRemoved(const EventListenerInfo& details) override;

  // If any WebRequest event listeners are currently active for this
  // BrowserContext, |*factory_request| is swapped out for a new request which
  // proxies through an internal URLLoaderFactory. This supports lifetime
  // observation and control on behalf of the WebRequest API.
  // |frame| and |render_process_id| are the frame and render process id in
  // which the URLLoaderFactory will be used. |frame| can be nullptr for
  // factories proxied for service worker.
  //
  // Returns |true| if the URLLoaderFactory will be proxied; |false| otherwise.
  bool MaybeProxyURLLoaderFactory(
      content::BrowserContext* browser_context,
      content::RenderFrameHost* frame,
      int render_process_id,
      content::ContentBrowserClient::URLLoaderFactoryType type,
      base::Optional<int64_t> navigation_id,
      mojo::PendingReceiver<network::mojom::URLLoaderFactory>* factory_receiver,
      mojo::PendingRemote<network::mojom::TrustedURLLoaderHeaderClient>*
          header_client);

  // Any request which requires authentication to complete will be bounced
  // through this method.
  //
  // If this returns |true|, |callback| will eventually be invoked on the UI
  // thread.
  bool MaybeProxyAuthRequest(
      content::BrowserContext* browser_context,
      const net::AuthChallengeInfo& auth_info,
      scoped_refptr<net::HttpResponseHeaders> response_headers,
      const content::GlobalRequestID& request_id,
      bool is_main_frame,
      AuthRequestCallback callback);

  // Starts proxying the connection with |factory|. This function can be called
  // only when MayHaveProxies() returns true.
  void ProxyWebSocket(
      content::RenderFrameHost* frame,
      content::ContentBrowserClient::WebSocketFactory factory,
      const GURL& url,
      const GURL& site_for_cookies,
      const base::Optional<std::string>& user_agent,
      mojo::PendingRemote<network::mojom::WebSocketHandshakeClient>
          handshake_client);

  void ForceProxyForTesting();

  // Indicates whether or not the WebRequestAPI may have one or more proxies
  // installed to support the API.
  bool MayHaveProxies() const;

 private:
  friend class BrowserContextKeyedAPIFactory<WebRequestAPI>;

  // BrowserContextKeyedAPI support:
  static const char* service_name() { return "WebRequestAPI"; }
  static const bool kServiceRedirectedInIncognito = true;
  static const bool kServiceIsNULLWhileTesting = true;

  // Checks if |MayHaveProxies()| has changed from false to true, and resets
  // URLLoaderFactories if so.
  void UpdateMayHaveProxies();

  // ExtensionRegistryObserver implementation.
  void OnExtensionLoaded(content::BrowserContext* browser_context,
                         const Extension* extension) override;
  void OnExtensionUnloaded(content::BrowserContext* browser_context,
                           const Extension* extension,
                           UnloadedExtensionReason reason) override;

  // A count of active extensions for this BrowserContext that use web request
  // permissions.
  int web_request_extension_count_ = 0;

  content::BrowserContext* const browser_context_;

  RequestIDGenerator request_id_generator_;
  std::unique_ptr<ProxySet> proxies_;

  // Stores the last result of |MayHaveProxies()|, so it can be used in
  // |UpdateMayHaveProxies()|.
  bool may_have_proxies_;

  DISALLOW_COPY_AND_ASSIGN(WebRequestAPI);
};

// This class observes network events and routes them to the appropriate
// extensions listening to those events. All methods must be called on the IO
// thread unless otherwise specified.
class ExtensionWebRequestEventRouter {
 public:
  struct BlockedRequest;

  // The events denoting the lifecycle of a given network request.
  enum EventTypes {
    kInvalidEvent = 0,
    kOnBeforeRequest = 1 << 0,
    kOnBeforeSendHeaders = 1 << 1,
    kOnSendHeaders = 1 << 2,
    kOnHeadersReceived = 1 << 3,
    kOnBeforeRedirect = 1 << 4,
    kOnAuthRequired = 1 << 5,
    kOnResponseStarted = 1 << 6,
    kOnErrorOccurred = 1 << 7,
    kOnCompleted = 1 << 8,
  };

  // Internal representation of the webRequest.RequestFilter type, used to
  // filter what network events an extension cares about.
  struct RequestFilter {
    RequestFilter();
    ~RequestFilter();

    // TODO(devlin): Make these a move constructor/operator.
    RequestFilter(const RequestFilter& other);
    RequestFilter& operator=(const RequestFilter& other);

    // Returns false if there was an error initializing. If it is a user error,
    // an error message is provided, otherwise the error is internal (and
    // unexpected).
    bool InitFromValue(const base::DictionaryValue& value, std::string* error);

    extensions::URLPatternSet urls;
    std::vector<WebRequestResourceType> types;
    int tab_id;
    int window_id;
  };

  // Contains an extension's response to a blocking event.
  struct EventResponse {
    EventResponse(const std::string& extension_id,
                  const base::Time& extension_install_time);
    ~EventResponse();

    // ID of the extension that sent this response.
    std::string extension_id;

    // The time that the extension was installed. Used for deciding order of
    // precedence in case multiple extensions respond with conflicting
    // decisions.
    base::Time extension_install_time;

    // Response values. These are mutually exclusive.
    bool cancel;
    GURL new_url;
    std::unique_ptr<net::HttpRequestHeaders> request_headers;
    std::unique_ptr<extension_web_request_api_helpers::ResponseHeaders>
        response_headers;

    base::Optional<net::AuthCredentials> auth_credentials;

   private:
    DISALLOW_COPY_AND_ASSIGN(EventResponse);
  };

  // AuthRequiredResponse indicates how an OnAuthRequired call is handled.
  enum class AuthRequiredResponse {
    // No credenitals were provided.
    AUTH_REQUIRED_RESPONSE_NO_ACTION,
    // AuthCredentials is filled in with a username and password, which should
    // be used in a response to the provided auth challenge.
    AUTH_REQUIRED_RESPONSE_SET_AUTH,
    // The request should be canceled.
    AUTH_REQUIRED_RESPONSE_CANCEL_AUTH,
    // The action will be decided asynchronously. |callback| will be invoked
    // when the decision is made, and one of the other AuthRequiredResponse
    // values will be passed in with the same semantics as described above.
    AUTH_REQUIRED_RESPONSE_IO_PENDING,
  };
  using AuthCallback = base::OnceCallback<void(AuthRequiredResponse)>;

  static ExtensionWebRequestEventRouter* GetInstance();

  // Registers a rule registry. Pass null for |rules_registry| to unregister
  // the rule registry for |browser_context|.
  void RegisterRulesRegistry(
      content::BrowserContext* browser_context,
      int rules_registry_id,
      scoped_refptr<extensions::WebRequestRulesRegistry> rules_registry);

  // Dispatches the OnBeforeRequest event to any extensions whose filters match
  // the given request. Returns net::ERR_IO_PENDING if an extension is
  // intercepting the request and OK if the request should proceed normally.
  // net::ERR_BLOCKED_BY_CLIENT is returned if the request should be blocked. In
  // this case, |should_collapse_initiator| might be set to true indicating
  // whether the DOM element which initiated the request should be blocked.
  int OnBeforeRequest(content::BrowserContext* browser_context,
                      WebRequestInfo* request,
                      net::CompletionOnceCallback callback,
                      GURL* new_url,
                      bool* should_collapse_initiator);

  using BeforeSendHeadersCallback =
      base::OnceCallback<void(const std::set<std::string>& removed_headers,
                              const std::set<std::string>& set_headers,
                              int error_code)>;

  // Dispatches the onBeforeSendHeaders event. This is fired for HTTP(s)
  // requests only, and allows modification of the outgoing request headers.
  // Returns net::ERR_IO_PENDING if an extension is intercepting the request, OK
  // otherwise.
  int OnBeforeSendHeaders(content::BrowserContext* browser_context,
                          const WebRequestInfo* request,
                          BeforeSendHeadersCallback callback,
                          net::HttpRequestHeaders* headers);

  // Dispatches the onSendHeaders event. This is fired for HTTP(s) requests
  // only.
  void OnSendHeaders(content::BrowserContext* browser_context,
                     const WebRequestInfo* request,
                     const net::HttpRequestHeaders& headers);

  // Dispatches the onHeadersReceived event. This is fired for HTTP(s)
  // requests only, and allows modification of incoming response headers.
  // Returns net::ERR_IO_PENDING if an extension is intercepting the request,
  // OK otherwise. |original_response_headers| is reference counted. |callback|
  // |override_response_headers| and |preserve_fragment_on_redirect_url| are not
  // owned but are guaranteed to be valid until |callback| is called or
  // OnRequestWillBeDestroyed is called (whatever comes first).
  // Do not modify |original_response_headers| directly but write new ones
  // into |override_response_headers|.
  int OnHeadersReceived(
      content::BrowserContext* browser_context,
      const WebRequestInfo* request,
      net::CompletionOnceCallback callback,
      const net::HttpResponseHeaders* original_response_headers,
      scoped_refptr<net::HttpResponseHeaders>* override_response_headers,
      GURL* preserve_fragment_on_redirect_url);

  // Dispatches the OnAuthRequired event to any extensions whose filters match
  // the given request. If the listener is not registered as "blocking", then
  // AUTH_REQUIRED_RESPONSE_NO_ACTION is returned. Otherwise,
  // AUTH_REQUIRED_RESPONSE_IO_PENDING is returned and |callback| will be
  // invoked later.
  AuthRequiredResponse OnAuthRequired(content::BrowserContext* browser_context,
                                      const WebRequestInfo* request,
                                      const net::AuthChallengeInfo& auth_info,
                                      AuthCallback callback,
                                      net::AuthCredentials* credentials);

  // Dispatches the onBeforeRedirect event. This is fired for HTTP(s) requests
  // only.
  void OnBeforeRedirect(content::BrowserContext* browser_context,
                        const WebRequestInfo* request,
                        const GURL& new_location);

  // Dispatches the onResponseStarted event indicating that the first bytes of
  // the response have arrived.
  void OnResponseStarted(content::BrowserContext* browser_context,
                         const WebRequestInfo* request,
                         int net_error);

  // Dispatches the onComplete event.
  void OnCompleted(content::BrowserContext* browser_context,
                   const WebRequestInfo* request,
                   int net_error);

  // Dispatches an onErrorOccurred event.
  void OnErrorOccurred(content::BrowserContext* browser_context,
                       const WebRequestInfo* request,
                       bool started,
                       int net_error);

  // Notificaties when |request| is no longer being processed, regardless of
  // whether it has gone to completion or merely been cancelled. This is
  // guaranteed to be called eventually for any request observed by this object,
  // and |*request| will be immintently destroyed after this returns.
  void OnRequestWillBeDestroyed(content::BrowserContext* browser_context,
                                const WebRequestInfo* request);

  // Called when an event listener handles a blocking event and responds.
  void OnEventHandled(content::BrowserContext* browser_context,
                      const std::string& extension_id,
                      const std::string& event_name,
                      const std::string& sub_event_name,
                      uint64_t request_id,
                      int render_process_id,
                      int web_view_instance_id,
                      int worker_thread_id,
                      int64_t service_worker_version_id,
                      EventResponse* response);

  // Adds a listener to the given event. |event_name| specifies the event being
  // listened to. |sub_event_name| is an internal event uniquely generated in
  // the extension process to correspond to the given filter and
  // extra_info_spec. It returns true on success, false on failure.
  bool AddEventListener(content::BrowserContext* browser_context,
                        const std::string& extension_id,
                        const std::string& extension_name,
                        events::HistogramValue histogram_value,
                        const std::string& event_name,
                        const std::string& sub_event_name,
                        const RequestFilter& filter,
                        int extra_info_spec,
                        int render_process_id,
                        int web_view_instance_id,
                        int worker_thread_id,
                        int64_t service_worker_version_id);

  // Removes the listeners for a given <webview>.
  void RemoveWebViewEventListeners(content::BrowserContext* browser_context,
                                   int render_process_id,
                                   int web_view_instance_id);

  // Called when an incognito browser_context is created or destroyed.
  void OnOTRBrowserContextCreated(
      content::BrowserContext* original_browser_context,
      content::BrowserContext* otr_browser_context);
  void OnOTRBrowserContextDestroyed(
      content::BrowserContext* original_browser_context,
      content::BrowserContext* otr_browser_context);

  // Registers a |callback| that is executed when the next page load happens.
  // The callback is then deleted.
  void AddCallbackForPageLoad(const base::Closure& callback);

  // Whether there is a listener matching the request that has
  // ExtraInfoSpec::EXTRA_HEADERS set.
  bool HasExtraHeadersListenerForRequest(
      content::BrowserContext* browser_context,
      const WebRequestInfo* request);

  // Whether there are any listeners for this context that have
  // ExtraInfoSpec::EXTRA_HEADERS set.
  bool HasAnyExtraHeadersListener(content::BrowserContext* browser_context);

  void IncrementExtraHeadersListenerCount(
      content::BrowserContext* browser_context);
  void DecrementExtraHeadersListenerCount(
      content::BrowserContext* browser_context);

 private:
  friend class WebRequestAPI;
  friend class base::NoDestructor<ExtensionWebRequestEventRouter>;
  FRIEND_TEST_ALL_PREFIXES(ExtensionWebRequestTest,
                           BlockingEventPrecedenceRedirect);
  FRIEND_TEST_ALL_PREFIXES(ExtensionWebRequestTest,
                           BlockingEventPrecedenceCancel);
  FRIEND_TEST_ALL_PREFIXES(ExtensionWebRequestTest,
                           SimulateChancelWhileBlocked);
  FRIEND_TEST_ALL_PREFIXES(ExtensionWebRequestTest, AccessRequestBodyData);
  FRIEND_TEST_ALL_PREFIXES(ExtensionWebRequestTest,
                           MinimalAccessRequestBodyData);
  FRIEND_TEST_ALL_PREFIXES(ExtensionWebRequestTest,
                           ProperFilteringInPublicSession);
  FRIEND_TEST_ALL_PREFIXES(ExtensionWebRequestTest, NoAccessRequestBodyData);
  FRIEND_TEST_ALL_PREFIXES(ExtensionWebRequestTest, AddAndRemoveListeners);
  FRIEND_TEST_ALL_PREFIXES(ExtensionWebRequestTest, BlockedRequestsAreRemoved);
  FRIEND_TEST_ALL_PREFIXES(ExtensionWebRequestHeaderModificationTest,
                           TestModifications);

  struct EventListener {
    // TODO(rdevlin.cronin): There are two types of EventListeners - those
    // associated with WebViews and those that are not. The ones associated with
    // WebViews are always identified by all seven properties. The other ones
    // will always have web_view_instance_id = 0. Unfortunately, the
    // callbacks/interfaces for these ones don't specify render_process_id.
    // This is why we need the LooselyMatches method, and the need for a
    // |strict| argument on RemoveEventListener.
    struct ID {
      ID(content::BrowserContext* browser_context,
         const std::string& extension_id,
         const std::string& sub_event_name,
         int render_process_id,
         int web_view_instance_id,
         int worker_thread_id,
         int64_t service_worker_version_id);

      ID(const ID& source);

      // If web_view_instance_id is 0, then ignore render_process_id.
      // TODO(rdevlin.cronin): In a more sane world, LooselyMatches wouldn't be
      // necessary.
      bool LooselyMatches(const ID& that) const;

      bool operator==(const ID& that) const;

      content::BrowserContext* browser_context;
      std::string extension_id;
      std::string sub_event_name;
      // In the case of a webview, this is the process ID of the embedder.
      int render_process_id;
      int web_view_instance_id;
      // The worker_thread_id and service_worker_version_id members are only
      // meaningful for event listeners for ServiceWorker events. Otherwise,
      // they are initialized to sentinel values.
      int worker_thread_id;
      int64_t service_worker_version_id;
    };

    EventListener(ID id);
    ~EventListener();

    const ID id;
    std::string extension_name;
    events::HistogramValue histogram_value = events::UNKNOWN;
    RequestFilter filter;
    int extra_info_spec = 0;
    std::unordered_set<uint64_t> blocked_requests;

   private:
    DISALLOW_COPY_AND_ASSIGN(EventListener);
  };

  using RawListeners = std::vector<EventListener*>;
  using ListenerIDs = std::vector<EventListener::ID>;
  using Listeners = std::vector<std::unique_ptr<EventListener>>;
  using ListenerMapForBrowserContext = std::map<std::string, Listeners>;
  using ListenerMap =
      std::map<content::BrowserContext*, ListenerMapForBrowserContext>;
  using ExtraHeadersListenerCountMap = std::map<content::BrowserContext*, int>;
  using BlockedRequestMap = std::map<uint64_t, BlockedRequest>;
  // Map of request_id -> bit vector of EventTypes already signaled
  using SignaledRequestMap = std::map<uint64_t, int>;
  // For each browser_context: a bool indicating whether it is an incognito
  // browser_context, and a pointer to the corresponding (non-)incognito
  // browser_context.
  using CrossBrowserContextMap =
      std::map<content::BrowserContext*,
               std::pair<bool, content::BrowserContext*>>;
  using CallbacksForPageLoad = std::list<base::Closure>;

  ExtensionWebRequestEventRouter();

  // This instance is leaked.
  ~ExtensionWebRequestEventRouter() = delete;

  // Returns the EventListener with the given |id|, or nullptr. Must be called
  // from the IO thread.
  EventListener* FindEventListener(const EventListener::ID& id);

  // Returns the EventListener with the given |id| from |listeners|.
  EventListener* FindEventListenerInContainer(const EventListener::ID& id,
                                              Listeners& listeners);

  // Removes the listener for the given sub-event. Must be called from the IO
  // thread.
  void RemoveEventListener(const EventListener::ID& id, bool strict);

  // Ensures that future callbacks for |request| are ignored so that it can be
  // destroyed safely.
  void ClearPendingCallbacks(const WebRequestInfo& request);

  bool DispatchEvent(content::BrowserContext* browser_context,
                     const WebRequestInfo* request,
                     const RawListeners& listener_ids,
                     std::unique_ptr<WebRequestEventDetails> event_details);

  void DispatchEventToListeners(
      content::BrowserContext* browser_context,
      std::unique_ptr<ListenerIDs> listener_ids,
      std::unique_ptr<WebRequestEventDetails> event_details);

  // Returns a list of event listeners that care about the given event, based
  // on their filter parameters. |extra_info_spec| will contain the combined
  // set of extra_info_spec flags that every matching listener asked for.
  RawListeners GetMatchingListeners(content::BrowserContext* browser_context,
                                    const std::string& event_name,
                                    const WebRequestInfo* request,
                                    int* extra_info_spec);

  // Helper for the above functions. This is called twice: once for the
  // browser_context of the event, the next time for the "cross" browser_context
  // (i.e. the incognito browser_context if the event is originally for the
  // normal browser_context, or vice versa).
  void GetMatchingListenersImpl(content::BrowserContext* browser_context,
                                const WebRequestInfo* request,
                                bool crosses_incognito,
                                const std::string& event_name,
                                bool is_request_from_extension,
                                int* extra_info_spec,
                                RawListeners* matching_listeners);

  // Decrements the count of event handlers blocking the given request. When the
  // count reaches 0, we stop blocking the request and proceed it using the
  // method requested by the extension with the highest precedence. Precedence
  // is decided by extension install time. If |response| is non-NULL, this
  // method assumes ownership.
  void DecrementBlockCount(content::BrowserContext* browser_context,
                           const std::string& extension_id,
                           const std::string& event_name,
                           uint64_t request_id,
                           EventResponse* response,
                           int extra_info_spec);

  // Processes the generated deltas from blocked_requests_ on the specified
  // request. If |call_back| is true, the callback registered in
  // |blocked_requests_| is called.
  // The function returns the error code for the network request. This is
  // mostly relevant in case the caller passes |call_callback| = false
  // and wants to return the correct network error code himself.
  int ExecuteDeltas(content::BrowserContext* browser_context,
                    const WebRequestInfo* request,
                    bool call_callback);

  // Evaluates the rules of the declarative webrequest API and stores
  // modifications to the request that result from WebRequestActions as
  // deltas in |blocked_requests_|. |filtered_response_headers| should only be
  // set for the OnHeadersReceived stage and NULL otherwise. Returns whether any
  // deltas were generated.
  bool ProcessDeclarativeRules(
      content::BrowserContext* browser_context,
      const std::string& event_name,
      const WebRequestInfo* request,
      extensions::RequestStage request_stage,
      const net::HttpResponseHeaders* filtered_response_headers);

  // If the BlockedRequest contains messages_to_extension entries in the event
  // deltas, we send them to subscribers of
  // chrome.declarativeWebRequest.onMessage.
  void SendMessages(content::BrowserContext* browser_context,
                    const BlockedRequest& blocked_request);

  // Called when the RulesRegistry is ready to unblock a request that was
  // waiting for said event.
  void OnRulesRegistryReady(content::BrowserContext* browser_context,
                            const std::string& event_name,
                            uint64_t request_id,
                            extensions::RequestStage request_stage);

  // Sets the flag that |event_type| has been signaled for |request_id|.
  // Returns the value of the flag before setting it.
  bool GetAndSetSignaled(uint64_t request_id, EventTypes event_type);

  // Clears the flag that |event_type| has been signaled for |request_id|.
  void ClearSignaled(uint64_t request_id, EventTypes event_type);

  // Returns whether |request| represents a top level window navigation.
  bool IsPageLoad(const WebRequestInfo& request) const;

  // Called on a page load to process all registered callbacks.
  void NotifyPageLoad();

  // Returns the matching cross browser_context (the regular browser_context if
  // |browser_context| is OTR and vice versa).
  content::BrowserContext* GetCrossBrowserContext(
      content::BrowserContext* browser_context) const;

  // Determines whether the specified browser_context is an incognito
  // browser_context (based on the contents of the cross-browser_context table
  // and without dereferencing the browser_context pointer).
  bool IsIncognitoBrowserContext(
      content::BrowserContext* browser_context) const;

  // Returns true if |request| was already signaled to some event handlers.
  bool WasSignaled(const WebRequestInfo& request) const;

  // Helper for |HasAnyExtraHeadersListener()|.
  bool HasAnyExtraHeadersListenerImpl(content::BrowserContext* browser_context);

  // Get the number of listeners - for testing only.
  size_t GetListenerCountForTesting(content::BrowserContext* browser_context,
                                    const std::string& event_name);

  // TODO(karandeepb): The below code should be refactored to have a single map
  // to store per-browser-context data.

  // A map for each browser_context that maps an event name to a set of
  // extensions that are listening to that event.
  ListenerMap listeners_;

  // Count of listeners per browser context which request extra headers. Must be
  // modified through [Increment/Decrement]ExtraHeadersListenerCount.
  ExtraHeadersListenerCountMap extra_headers_listener_count_;

  // A map of network requests that are waiting for at least one event handler
  // to respond.
  BlockedRequestMap blocked_requests_;

  // A map of request ids to a bitvector indicating which events have been
  // signaled and should not be sent again.
  SignaledRequestMap signaled_requests_;

  // A map of original browser_context -> corresponding incognito
  // browser_context (and vice versa).
  CrossBrowserContextMap cross_browser_context_map_;

  // Keeps track of time spent waiting on extensions using the blocking
  // webRequest API.
  std::unique_ptr<ExtensionWebRequestTimeTracker> request_time_tracker_;

  CallbacksForPageLoad callbacks_for_page_load_;

  typedef std::pair<content::BrowserContext*, int> RulesRegistryKey;
  // Maps each browser_context (and OTRBrowserContext) and a webview key to its
  // respective rules registry.
  std::map<RulesRegistryKey,
      scoped_refptr<extensions::WebRequestRulesRegistry> > rules_registries_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionWebRequestEventRouter);
};

class WebRequestInternalFunction : public ExtensionFunction {
 public:
  WebRequestInternalFunction() {}

 protected:
  ~WebRequestInternalFunction() override {}

  const std::string& extension_id_safe() const {
    return extension() ? extension_id() : base::EmptyString();
  }
};

class WebRequestInternalAddEventListenerFunction
    : public WebRequestInternalFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("webRequestInternal.addEventListener",
                             WEBREQUESTINTERNAL_ADDEVENTLISTENER)

 protected:
  ~WebRequestInternalAddEventListenerFunction() override {}

  // ExtensionFunction:
  ResponseAction Run() override;
};

class WebRequestInternalEventHandledFunction
    : public WebRequestInternalFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("webRequestInternal.eventHandled",
                             WEBREQUESTINTERNAL_EVENTHANDLED)

 protected:
  ~WebRequestInternalEventHandledFunction() override {}

 private:
  // Unblocks the network request. Use this function when handling incorrect
  // requests from the extension that cannot be detected by the schema
  // validator.
  void OnError(
      const std::string& event_name,
      const std::string& sub_event_name,
      uint64_t request_id,
      int render_process_id,
      int web_view_instance_id,
      std::unique_ptr<ExtensionWebRequestEventRouter::EventResponse> response);

  // ExtensionFunction:
  ResponseAction Run() override;
};

class WebRequestHandlerBehaviorChangedFunction
    : public WebRequestInternalFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("webRequest.handlerBehaviorChanged",
                             WEBREQUEST_HANDLERBEHAVIORCHANGED)

 protected:
  ~WebRequestHandlerBehaviorChangedFunction() override {}

  // ExtensionFunction:
  void GetQuotaLimitHeuristics(
      extensions::QuotaLimitHeuristics* heuristics) const override;
  // Handle quota exceeded gracefully: Only warn the user but still execute the
  // function.
  void OnQuotaExceeded(std::string error) override;
  ResponseAction Run() override;
};

}  // namespace extensions

#endif  // EXTENSIONS_BROWSER_API_WEB_REQUEST_WEB_REQUEST_API_H_
