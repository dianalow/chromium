// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/nacl/renderer/manifest_downloader.h"

#include "base/callback.h"
#include "components/nacl/renderer/histogram.h"
#include "components/nacl/renderer/nexe_load_manager.h"
#include "net/base/net_errors.h"
#include "third_party/WebKit/public/platform/WebURLError.h"
#include "third_party/WebKit/public/platform/WebURLLoader.h"
#include "third_party/WebKit/public/platform/WebURLResponse.h"

namespace nacl {

namespace {
// This is a pretty arbitrary limit on the byte size of the NaCl manifest file.
// Note that the resulting string object has to have at least one byte extra
// for the null termination character.
const size_t kNaClManifestMaxFileBytes = 1024 * 1024;
}  // namespace

ManifestDownloader::ManifestDownloader(
    bool is_installed,
    ManifestDownloaderCallback cb)
    : is_installed_(is_installed),
      cb_(cb),
      status_code_(-1),
      pp_nacl_error_(PP_NACL_ERROR_LOAD_SUCCESS) {
  CHECK(!cb.is_null());
}

ManifestDownloader::~ManifestDownloader() {
}

void ManifestDownloader::didReceiveResponse(
    blink::WebURLLoader* loader,
    const blink::WebURLResponse& response) {
  if (response.httpStatusCode() != 200)
    pp_nacl_error_ = PP_NACL_ERROR_MANIFEST_LOAD_URL;
  status_code_ = response.httpStatusCode();
}

void ManifestDownloader::didReceiveData(
    blink::WebURLLoader* loader,
    const char* data,
    int data_length,
    int encoded_data_length) {
  if (buffer_.size() + data_length > kNaClManifestMaxFileBytes) {
    pp_nacl_error_ = PP_NACL_ERROR_MANIFEST_TOO_LARGE;
    buffer_.clear();
  }

  if (pp_nacl_error_ == PP_NACL_ERROR_LOAD_SUCCESS)
    buffer_.append(data, data_length);
}

void ManifestDownloader::didFinishLoading(
    blink::WebURLLoader* loader,
    double finish_time,
    int64_t total_encoded_data_length) {
  // We log the status code here instead of in didReceiveResponse so that we
  // always log a histogram value, even when we never receive a status code.
  HistogramHTTPStatusCode(
      is_installed_ ? "NaCl.HttpStatusCodeClass.Manifest.InstalledApp" :
                      "NaCl.HttpStatusCodeClass.Manifest.NotInstalledApp",
      status_code_);

  cb_.Run(pp_nacl_error_, buffer_);
  delete this;
}

void ManifestDownloader::didFail(
    blink::WebURLLoader* loader,
    const blink::WebURLError& error) {
  // TODO(teravest): Find a place to share this code with PepperURLLoaderHost.
  pp_nacl_error_ = PP_NACL_ERROR_MANIFEST_LOAD_URL;
  if (error.domain.equals(blink::WebString::fromUTF8(net::kErrorDomain))) {
    switch (error.reason) {
      case net::ERR_ACCESS_DENIED:
      case net::ERR_NETWORK_ACCESS_DENIED:
        pp_nacl_error_ = PP_NACL_ERROR_MANIFEST_NOACCESS_URL;
        break;
    }
  } else {
    // It's a WebKit error.
    pp_nacl_error_ = PP_NACL_ERROR_MANIFEST_NOACCESS_URL;
  }
}

}  // namespace nacl
