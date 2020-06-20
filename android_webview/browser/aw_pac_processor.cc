// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "android_webview/browser/aw_pac_processor.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <cstddef>
#include <memory>
#include <string>

#include "android_webview/browser_jni_headers/AwPacProcessor_jni.h"
#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/bind.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/task/post_task.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/threading/thread_restrictions.h"
#include "net/base/completion_once_callback.h"
#include "net/base/net_errors.h"
#include "net/base/network_isolation_key.h"
#include "net/proxy_resolution/pac_file_data.h"
#include "net/proxy_resolution/proxy_info.h"

using base::android::AttachCurrentThread;
using base::android::ConvertJavaStringToUTF8;
using base::android::ConvertUTF8ToJavaString;
using base::android::JavaParamRef;
using base::android::JavaRef;
using base::android::ScopedJavaGlobalRef;
using base::android::ScopedJavaLocalRef;
class NetworkIsolationKey;

namespace android_webview {

namespace {

// TODO(amalova): We could use a separate thread or thread pool for executing
// blocking DNS queries, to get better performance.
class HostResolver : public proxy_resolver::ProxyHostResolver {
  class RequestImpl : public Request {
   public:
    RequestImpl(const std::string& hostname,
                net::ProxyResolveDnsOperation operation)
        : hostname_(hostname), operation_(operation) {}
    ~RequestImpl() override = default;

    int Start(net::CompletionOnceCallback callback) override {
      bool success = false;
      switch (operation_) {
        case net::ProxyResolveDnsOperation::DNS_RESOLVE:
          success = DnsResolveImpl(hostname_);
          break;
        case net::ProxyResolveDnsOperation::DNS_RESOLVE_EX:
          success = DnsResolveExImpl(hostname_);
          break;
        case net::ProxyResolveDnsOperation::MY_IP_ADDRESS:
          success = MyIpAddressImpl();
          break;
        case net::ProxyResolveDnsOperation::MY_IP_ADDRESS_EX:
          success = MyIpAddressExImpl();
          break;
      }
      return success ? net::OK : net::ERR_NAME_RESOLUTION_FAILED;
    }

    const std::vector<net::IPAddress>& GetResults() const override {
      return results_;
    }

   private:
    bool MyIpAddressImpl() {
      std::string my_hostname = GetHostName();
      if (my_hostname.empty())
        return false;
      return DnsResolveImpl(my_hostname);
    }

    bool MyIpAddressExImpl() {
      std::string my_hostname = GetHostName();
      if (my_hostname.empty())
        return false;
      return DnsResolveExImpl(my_hostname);
    }

    net::IPAddress StringToIPAddress(const std::string& address) {
      net::IPAddress ip_address;
      if (!ip_address.AssignFromIPLiteral(std::string(address))) {
        LOG(ERROR) << "Not a supported IP literal: " << std::string(address);
      }
      return ip_address;
    }

    bool DnsResolveImpl(const std::string& host) {
      struct hostent* he = gethostbyname(host.c_str());

      // TODO(amalova): handle IPv6 (AF_INET6)
      if (he == nullptr || he->h_addr == nullptr || he->h_addrtype != AF_INET) {
        return false;
      }

      char tmp[INET_ADDRSTRLEN];
      if (inet_ntop(he->h_addrtype, he->h_addr, tmp, sizeof(tmp)) == nullptr) {
        return false;
      }

      results_.push_back(StringToIPAddress(std::string(tmp)));
      return true;
    }

    bool DnsResolveExImpl(const std::string& host) {
      struct hostent* he = gethostbyname(host.c_str());

      // TODO(amalova): handle IPv6 (AF_INET6)
      if (he == nullptr || he->h_addr_list == nullptr ||
          he->h_addrtype != AF_INET) {
        return false;
      }

      char tmp[INET_ADDRSTRLEN];
      for (char** addr = he->h_addr_list; *addr != nullptr; ++addr) {
        if (inet_ntop(he->h_addrtype, *addr, tmp, sizeof(tmp)) == nullptr) {
          return false;
        }
        results_.push_back(StringToIPAddress(std::string(tmp)));
      }

      return true;
    }

    std::string GetHostName() {
      char buffer[HOST_NAME_MAX + 1];

      if (gethostname(buffer, HOST_NAME_MAX + 1) != 0) {
        return std::string();
      }

      // It's unspecified whether gethostname NULL-terminates if the hostname
      // must be truncated and no error is returned if that happens.
      buffer[HOST_NAME_MAX] = '\0';
      return std::string(buffer);
    }

    const std::string hostname_;
    const net::ProxyResolveDnsOperation operation_;
    std::vector<net::IPAddress> results_;
  };

 public:
  HostResolver() = default;
  std::unique_ptr<Request> CreateRequest(
      const std::string& hostname,
      net::ProxyResolveDnsOperation operation,
      const net::NetworkIsolationKey&) override {
    return std::make_unique<RequestImpl>(hostname, operation);
  }
};

class Bindings : public proxy_resolver::ProxyResolverV8Tracing::Bindings {
 public:
  Bindings(AwPacProcessor* processor) : processor_(processor) {}

  void Alert(const base::string16& message) override {}

  void OnError(int line_number, const base::string16& message) override {}

  proxy_resolver::ProxyHostResolver* GetHostResolver() override {
    return processor_->host_resolver();
  }

  net::NetLogWithSource GetNetLogWithSource() override {
    return net::NetLogWithSource();
  }

 private:
  AwPacProcessor* processor_;
};

scoped_refptr<base::SingleThreadTaskRunner> GetTaskRunner() {
  struct ThreadHolder {
    base::Thread thread_;
    scoped_refptr<base::SingleThreadTaskRunner> task_runner_;

    ThreadHolder() : thread_("AwPacProcessor") {
      thread_.Start();
      task_runner_ = thread_.task_runner();
    }
  };
  static ThreadHolder thread_holder;

  return thread_holder.task_runner_;
}

proxy_resolver::ProxyResolverV8TracingFactory* GetProxyResolverFactory() {
  static std::unique_ptr<proxy_resolver::ProxyResolverV8TracingFactory>
      factory = proxy_resolver::ProxyResolverV8TracingFactory::Create();

  return factory.get();
}

}  // namespace

// Public methods of AwPacProcessor may be called on multiple threads.
// ProxyResolverV8TracingFactory/ProxyResolverV8Tracing
// expects its public interface to always be called on the same thread with
// Chromium task runner so it can post it back to that thread
// with the result of the queries.
//
// Job and its subclasses wrap queries from public methods of AwPacProcessor,
// post them on a special thread and blocks on WaitableEvent
// until the query is finished. |OnSignal| is passed to
// ProxyResolverV8TracingFactory/ProxyResolverV8Tracing methods.
// This callback is called once the request is processed and,
// it signals WaitableEvent and returns result to the calling thread.
//
// ProxyResolverV8TracingFactory/ProxyResolverV8Tracing behaviour is the
// following: if the corresponding request is destroyed,
// the query is cancelled and the callback is never called.
// That means that we need to signal WaitableEvent to unblock calling thread
// when we cancel Job. We keep track of unfinished Jobs in |jobs_|. This field
// is always accessed on the same thread.
//
// All Jobs must be cancelled prior to destruction of |proxy_resolver_| since
// its destructor asserts there are no pending requests.
class Job {
 public:
  virtual ~Job() = default;

  bool ExecSync() {
    GetTaskRunner()->PostTask(
        FROM_HERE, base::BindOnce(&Job::Exec, base::Unretained(this)));
    event_.Wait();
    return net_error_ == net::OK;
  }

  void Exec() {
    processor_->jobs_.insert(this);
    std::move(task_).Run();
  }

  virtual void Cancel() = 0;

  void OnSignal(int net_error) {
    net_error_ = net_error;

    // Both SetProxyScriptJob#request_ and MakeProxyRequestJob#request_
    // have to be destroyed on the same thread on which they are created.
    // If we destroy them before callback is called the request_ is cancelled.
    // Reset them here on the correct thread when the job is already finished
    // so no cancellation occurs.
    Cancel();
  }

  base::OnceClosure task_;
  int net_error_ = net::ERR_ABORTED;
  base::WaitableEvent event_;
  AwPacProcessor* processor_;
};

class SetProxyScriptJob : public Job {
 public:
  SetProxyScriptJob(AwPacProcessor* processor, std::string script) {
    processor_ = processor;
    task_ = base::BindOnce(
        &AwPacProcessor::SetProxyScriptNative, base::Unretained(processor_),
        &request_, std::move(script),
        base::BindOnce(&SetProxyScriptJob::OnSignal, base::Unretained(this)));
  }

  void Cancel() override {
    processor_->jobs_.erase(this);
    request_.reset();
    event_.Signal();
  }

 private:
  std::unique_ptr<net::ProxyResolverFactory::Request> request_;
};

class MakeProxyRequestJob : public Job {
 public:
  MakeProxyRequestJob(AwPacProcessor* processor, std::string url) {
    processor_ = processor;
    task_ = base::BindOnce(
        &AwPacProcessor::MakeProxyRequestNative, base::Unretained(processor_),
        &request_, std::move(url), &proxy_info_,
        base::BindOnce(&MakeProxyRequestJob::OnSignal, base::Unretained(this)));
  }

  void Cancel() override {
    processor_->jobs_.erase(this);
    request_.reset();
    event_.Signal();
  }

  net::ProxyInfo proxy_info() { return proxy_info_; }

 private:
  net::ProxyInfo proxy_info_;
  std::unique_ptr<net::ProxyResolver::Request> request_;
};

AwPacProcessor::AwPacProcessor() {
  host_resolver_ = std::make_unique<HostResolver>();
}

AwPacProcessor::~AwPacProcessor() {
  base::WaitableEvent event;
  // |proxy_resolver_| must be destroyed on the same thread it is created.
  GetTaskRunner()->PostTask(
      FROM_HERE,
      base::BindOnce(&AwPacProcessor::Destroy, base::Unretained(this),
                     base::Unretained(&event)));
  event.Wait();
}

void AwPacProcessor::Destroy(base::WaitableEvent* event) {
  // Cancel all unfinished jobs to unblock calling thread.
  for (auto* job : jobs_) {
    job->Cancel();
  }

  proxy_resolver_.reset();
  event->Signal();
}

void AwPacProcessor::DestroyNative(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& obj) {
  delete this;
}

void AwPacProcessor::SetProxyScriptNative(
    std::unique_ptr<net::ProxyResolverFactory::Request>* request,
    const std::string& script,
    net::CompletionOnceCallback complete) {
  DCHECK(GetTaskRunner()->BelongsToCurrentThread());
  GetProxyResolverFactory()->CreateProxyResolverV8Tracing(
      net::PacFileData::FromUTF8(script), std::make_unique<Bindings>(this),
      &proxy_resolver_, std::move(complete), request);
}

void AwPacProcessor::MakeProxyRequestNative(
    std::unique_ptr<net::ProxyResolver::Request>* request,
    const std::string& url,
    net::ProxyInfo* proxy_info,
    net::CompletionOnceCallback complete) {
  DCHECK(GetTaskRunner()->BelongsToCurrentThread());

  if (proxy_resolver_) {
    proxy_resolver_->GetProxyForURL(GURL(url), net::NetworkIsolationKey(),
                                    proxy_info, std::move(complete), request,
                                    std::make_unique<Bindings>(this));
  } else {
    std::move(complete).Run(net::ERR_FAILED);
  }
}

bool AwPacProcessor::SetProxyScript(std::string script) {
  SetProxyScriptJob job(this, script);
  bool success = job.ExecSync();

  DCHECK(proxy_resolver_);
  return success;
}

jboolean AwPacProcessor::SetProxyScript(JNIEnv* env,
                                        const JavaParamRef<jobject>& obj,
                                        const JavaParamRef<jstring>& jscript) {
  std::string script = ConvertJavaStringToUTF8(env, jscript);
  return SetProxyScript(script);
}

std::string AwPacProcessor::MakeProxyRequest(std::string url) {
  MakeProxyRequestJob job(this, url);
  bool success = job.ExecSync();
  return success ? job.proxy_info().ToPacString() : nullptr;
}

ScopedJavaLocalRef<jstring> AwPacProcessor::MakeProxyRequest(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    const JavaParamRef<jstring>& jurl) {
  std::string url = ConvertJavaStringToUTF8(env, jurl);
  return ConvertUTF8ToJavaString(env, MakeProxyRequest(url));
}

static jlong JNI_AwPacProcessor_CreateNativePacProcessor(JNIEnv* env) {
  AwPacProcessor* processor = new AwPacProcessor();
  return reinterpret_cast<intptr_t>(processor);
}

static void JNI_AwPacProcessor_InitializeEnvironment(JNIEnv* env) {
  base::ThreadPoolInstance::CreateAndStartWithDefaultParams("AwPacProcessor");
}

}  // namespace android_webview
