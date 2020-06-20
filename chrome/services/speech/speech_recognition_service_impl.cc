// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/services/speech/speech_recognition_service_impl.h"

#include "chrome/services/speech/speech_recognition_recognizer_impl.h"

namespace speech {

SpeechRecognitionServiceImpl::SpeechRecognitionServiceImpl(
    mojo::PendingReceiver<media::mojom::SpeechRecognitionService> receiver)
    : receiver_(this, std::move(receiver)) {}

SpeechRecognitionServiceImpl::~SpeechRecognitionServiceImpl() = default;

void SpeechRecognitionServiceImpl::BindContext(
    mojo::PendingReceiver<media::mojom::SpeechRecognitionContext> context) {
  speech_recognition_contexts_.Add(this, std::move(context));
}

void SpeechRecognitionServiceImpl::SetUrlLoaderFactory(
    mojo::PendingRemote<network::mojom::URLLoaderFactory> url_loader_factory) {
  url_loader_factory_ = mojo::Remote<network::mojom::URLLoaderFactory>(
      std::move(url_loader_factory));
  url_loader_factory_.set_disconnect_handler(
      base::BindOnce(&SpeechRecognitionServiceImpl::DisconnectHandler,
                     base::Unretained(this)));
}

void SpeechRecognitionServiceImpl::BindSpeechRecognitionServiceClient(
    mojo::PendingRemote<media::mojom::SpeechRecognitionServiceClient> client) {
  client_ = mojo::Remote<media::mojom::SpeechRecognitionServiceClient>(
      std::move(client));
}

mojo::PendingRemote<network::mojom::URLLoaderFactory>
SpeechRecognitionServiceImpl::GetUrlLoaderFactory() {
  mojo::PendingRemote<network::mojom::URLLoaderFactory> pending_factory_remote;
  url_loader_factory_->Clone(
      pending_factory_remote.InitWithNewPipeAndPassReceiver());

  return pending_factory_remote;
}

void SpeechRecognitionServiceImpl::BindRecognizer(
    mojo::PendingReceiver<media::mojom::SpeechRecognitionRecognizer> receiver,
    mojo::PendingRemote<media::mojom::SpeechRecognitionRecognizerClient> client,
    BindRecognizerCallback callback) {
  SpeechRecognitionRecognizerImpl::Create(
      std::move(receiver), std::move(client), weak_factory_.GetWeakPtr());
  std::move(callback).Run(
      SpeechRecognitionRecognizerImpl::IsMultichannelSupported());
}

void SpeechRecognitionServiceImpl::DisconnectHandler() {
  if (client_.is_bound())
    client_->OnNetworkServiceDisconnect();
}

}  // namespace speech
