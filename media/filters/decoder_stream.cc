// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/filters/decoder_stream.h"

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/debug/trace_event.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/single_thread_task_runner.h"
#include "media/base/audio_decoder.h"
#include "media/base/bind_to_current_loop.h"
#include "media/base/decoder_buffer.h"
#include "media/base/demuxer_stream.h"
#include "media/base/video_decoder.h"
#include "media/filters/decrypting_demuxer_stream.h"

namespace media {

// TODO(rileya): Devise a better way of specifying trace/UMA/etc strings for
// templated classes such as this.
template <DemuxerStream::Type StreamType>
static const char* GetTraceString();

#define FUNCTION_DVLOG(level) \
  DVLOG(level) << __FUNCTION__ << \
  "<" << DecoderStreamTraits<StreamType>::ToString() << ">"

template <>
const char* GetTraceString<DemuxerStream::VIDEO>() {
  return "DecoderStream<VIDEO>::Decode";
}

template <>
const char* GetTraceString<DemuxerStream::AUDIO>() {
  return "DecoderStream<AUDIO>::Decode";
}

template <DemuxerStream::Type StreamType>
DecoderStream<StreamType>::DecoderStream(
    const scoped_refptr<base::SingleThreadTaskRunner>& task_runner,
    ScopedVector<Decoder> decoders,
    const SetDecryptorReadyCB& set_decryptor_ready_cb)
    : task_runner_(task_runner),
      state_(STATE_UNINITIALIZED),
      stream_(NULL),
      decoder_selector_(
          new DecoderSelector<StreamType>(task_runner,
                                          decoders.Pass(),
                                          set_decryptor_ready_cb)),
      active_splice_(false),
      pending_decode_requests_(0),
      weak_factory_(this) {}

template <DemuxerStream::Type StreamType>
DecoderStream<StreamType>::~DecoderStream() {
  DCHECK(state_ == STATE_UNINITIALIZED || state_ == STATE_STOPPED) << state_;
}

template <DemuxerStream::Type StreamType>
void DecoderStream<StreamType>::Initialize(DemuxerStream* stream,
                                           bool low_delay,
                                           const StatisticsCB& statistics_cb,
                                           const InitCB& init_cb) {
  FUNCTION_DVLOG(2);
  DCHECK(task_runner_->BelongsToCurrentThread());
  DCHECK_EQ(state_, STATE_UNINITIALIZED) << state_;
  DCHECK(init_cb_.is_null());
  DCHECK(!init_cb.is_null());

  statistics_cb_ = statistics_cb;
  init_cb_ = init_cb;
  stream_ = stream;
  low_delay_ = low_delay;

  state_ = STATE_INITIALIZING;
  // TODO(xhwang): DecoderSelector only needs a config to select a decoder.
  decoder_selector_->SelectDecoder(
      stream, low_delay,
      base::Bind(&DecoderStream<StreamType>::OnDecoderSelected,
                 weak_factory_.GetWeakPtr()));
}

template <DemuxerStream::Type StreamType>
void DecoderStream<StreamType>::Read(const ReadCB& read_cb) {
  FUNCTION_DVLOG(2);
  DCHECK(task_runner_->BelongsToCurrentThread());
  DCHECK(state_ == STATE_NORMAL || state_ == STATE_FLUSHING_DECODER ||
         state_ == STATE_ERROR || state_ == STATE_REINITIALIZING_DECODER ||
         state_ == STATE_PENDING_DEMUXER_READ)
      << state_;
  // No two reads in the flight at any time.
  DCHECK(read_cb_.is_null());
  // No read during resetting or stopping process.
  DCHECK(reset_cb_.is_null());
  DCHECK(stop_cb_.is_null());

  read_cb_ = read_cb;

  if (state_ == STATE_ERROR) {
    task_runner_->PostTask(FROM_HERE,
                           base::Bind(base::ResetAndReturn(&read_cb_),
                                      DECODE_ERROR,
                                      scoped_refptr<Output>()));
    return;
  }

  if (!ready_outputs_.empty()) {
    task_runner_->PostTask(FROM_HERE, base::Bind(
        base::ResetAndReturn(&read_cb_), OK, ready_outputs_.front()));
    ready_outputs_.pop_front();
  }

  // Decoder may be in reinitializing state as result of the previous Read().
  if (state_ == STATE_REINITIALIZING_DECODER)
    return;

  if (!CanDecodeMore())
    return;

  if (state_ == STATE_FLUSHING_DECODER) {
    FlushDecoder();
    return;
  }

  if (state_ != STATE_PENDING_DEMUXER_READ)
    ReadFromDemuxerStream();
}

template <DemuxerStream::Type StreamType>
void DecoderStream<StreamType>::Reset(const base::Closure& closure) {
  FUNCTION_DVLOG(2);
  DCHECK(task_runner_->BelongsToCurrentThread());
  DCHECK(state_ != STATE_UNINITIALIZED && state_ != STATE_STOPPED) << state_;
  DCHECK(reset_cb_.is_null());
  DCHECK(stop_cb_.is_null());

  reset_cb_ = closure;

  if (!read_cb_.is_null()) {
    task_runner_->PostTask(FROM_HERE, base::Bind(
        base::ResetAndReturn(&read_cb_), ABORTED, scoped_refptr<Output>()));
  }

  ready_outputs_.clear();

  // During decoder reinitialization, the Decoder does not need to be and
  // cannot be Reset(). |decrypting_demuxer_stream_| was reset before decoder
  // reinitialization.
  if (state_ == STATE_REINITIALIZING_DECODER)
    return;

  // During pending demuxer read and when not using DecryptingDemuxerStream,
  // the Decoder will be reset after demuxer read is returned
  // (in OnBufferReady()).
  if (state_ == STATE_PENDING_DEMUXER_READ && !decrypting_demuxer_stream_)
    return;

  if (decrypting_demuxer_stream_) {
    decrypting_demuxer_stream_->Reset(base::Bind(
        &DecoderStream<StreamType>::ResetDecoder, weak_factory_.GetWeakPtr()));
    return;
  }

  ResetDecoder();
}

template <DemuxerStream::Type StreamType>
void DecoderStream<StreamType>::Stop(const base::Closure& closure) {
  FUNCTION_DVLOG(2);
  DCHECK(task_runner_->BelongsToCurrentThread());
  DCHECK_NE(state_, STATE_STOPPED) << state_;
  DCHECK(stop_cb_.is_null());

  stop_cb_ = closure;

  if (state_ == STATE_INITIALIZING) {
    decoder_selector_->Abort();
    return;
  }

  DCHECK(init_cb_.is_null());

  // All pending callbacks will be dropped.
  weak_factory_.InvalidateWeakPtrs();

  // Post callbacks to prevent reentrance into this object.
  if (!read_cb_.is_null()) {
    task_runner_->PostTask(FROM_HERE, base::Bind(
        base::ResetAndReturn(&read_cb_), ABORTED, scoped_refptr<Output>()));
  }
  if (!reset_cb_.is_null())
    task_runner_->PostTask(FROM_HERE, base::ResetAndReturn(&reset_cb_));

  if (decrypting_demuxer_stream_) {
    decrypting_demuxer_stream_->Stop(base::Bind(
        &DecoderStream<StreamType>::StopDecoder, weak_factory_.GetWeakPtr()));
    return;
  }

  // We may not have a |decoder_| if Stop() was called during initialization.
  if (decoder_) {
    StopDecoder();
    return;
  }

  state_ = STATE_STOPPED;
  stream_ = NULL;
  decoder_.reset();
  decrypting_demuxer_stream_.reset();
  task_runner_->PostTask(FROM_HERE, base::ResetAndReturn(&stop_cb_));
}

template <DemuxerStream::Type StreamType>
bool DecoderStream<StreamType>::CanReadWithoutStalling() const {
  DCHECK(task_runner_->BelongsToCurrentThread());
  return decoder_->CanReadWithoutStalling();
}

template <>
bool DecoderStream<DemuxerStream::AUDIO>::CanReadWithoutStalling() const {
  DCHECK(task_runner_->BelongsToCurrentThread());
  return true;
}

template <DemuxerStream::Type StreamType>
bool DecoderStream<StreamType>::CanDecodeMore() const {
  DCHECK(task_runner_->BelongsToCurrentThread());

  // Limit total number of outputs stored in |ready_outputs_| and being decoded.
  // It only makes sense to saturate decoder completely when output queue is
  // empty.
  int num_decodes =
      static_cast<int>(ready_outputs_.size()) + pending_decode_requests_;
  return num_decodes < decoder_->GetMaxDecodeRequests();
}

template <>
bool DecoderStream<DemuxerStream::AUDIO>::CanDecodeMore() const {
  DCHECK(task_runner_->BelongsToCurrentThread());
  return !pending_decode_requests_ && ready_outputs_.empty();
}

template <DemuxerStream::Type StreamType>
void DecoderStream<StreamType>::OnDecoderSelected(
    scoped_ptr<Decoder> selected_decoder,
    scoped_ptr<DecryptingDemuxerStream> decrypting_demuxer_stream) {
  FUNCTION_DVLOG(2);
  DCHECK(task_runner_->BelongsToCurrentThread());
  DCHECK_EQ(state_, STATE_INITIALIZING) << state_;
  DCHECK(!init_cb_.is_null());
  DCHECK(read_cb_.is_null());
  DCHECK(reset_cb_.is_null());

  decoder_selector_.reset();
  if (decrypting_demuxer_stream)
    stream_ = decrypting_demuxer_stream.get();

  if (!selected_decoder) {
    state_ = STATE_UNINITIALIZED;
    StreamTraits::FinishInitialization(
        base::ResetAndReturn(&init_cb_), selected_decoder.get(), stream_);
  } else {
    state_ = STATE_NORMAL;
    decoder_ = selected_decoder.Pass();
    decrypting_demuxer_stream_ = decrypting_demuxer_stream.Pass();
    StreamTraits::FinishInitialization(
        base::ResetAndReturn(&init_cb_), decoder_.get(), stream_);
  }

  // Stop() called during initialization.
  if (!stop_cb_.is_null()) {
    Stop(base::ResetAndReturn(&stop_cb_));
    return;
  }
}

template <DemuxerStream::Type StreamType>
void DecoderStream<StreamType>::SatisfyRead(
    Status status,
    const scoped_refptr<Output>& output) {
  DCHECK(!read_cb_.is_null());
  base::ResetAndReturn(&read_cb_).Run(status, output);
}

template <DemuxerStream::Type StreamType>
void DecoderStream<StreamType>::Decode(
    const scoped_refptr<DecoderBuffer>& buffer) {
  FUNCTION_DVLOG(2);
  DCHECK(state_ == STATE_NORMAL || state_ == STATE_FLUSHING_DECODER) << state_;
  DCHECK(CanDecodeMore());
  DCHECK(reset_cb_.is_null());
  DCHECK(stop_cb_.is_null());
  DCHECK(buffer);

  int buffer_size = buffer->end_of_stream() ? 0 : buffer->data_size();

  TRACE_EVENT_ASYNC_BEGIN0("media", GetTraceString<StreamType>(), this);
  ++pending_decode_requests_;
  decoder_->Decode(buffer,
                   base::Bind(&DecoderStream<StreamType>::OnDecodeOutputReady,
                              weak_factory_.GetWeakPtr(),
                              buffer_size));
}

template <DemuxerStream::Type StreamType>
void DecoderStream<StreamType>::FlushDecoder() {
  if (pending_decode_requests_ == 0)
    Decode(DecoderBuffer::CreateEOSBuffer());
}

template <DemuxerStream::Type StreamType>
void DecoderStream<StreamType>::OnDecodeOutputReady(
    int buffer_size,
    typename Decoder::Status status,
    const scoped_refptr<Output>& output) {
  FUNCTION_DVLOG(2) << status << " " << output;
  DCHECK(state_ == STATE_NORMAL || state_ == STATE_FLUSHING_DECODER ||
         state_ == STATE_PENDING_DEMUXER_READ || state_ == STATE_ERROR)
      << state_;
  DCHECK(stop_cb_.is_null());
  DCHECK_EQ(status == Decoder::kOk, output != NULL);
  DCHECK_GT(pending_decode_requests_, 0);

  --pending_decode_requests_;

  TRACE_EVENT_ASYNC_END0("media", GetTraceString<StreamType>(), this);

  if (state_ == STATE_ERROR) {
    DCHECK(read_cb_.is_null());
    return;
  }

  if (status == Decoder::kDecodeError) {
    state_ = STATE_ERROR;
    ready_outputs_.clear();
    if (!read_cb_.is_null())
      SatisfyRead(DECODE_ERROR, NULL);
    return;
  }

  if (status == Decoder::kDecryptError) {
    state_ = STATE_ERROR;
    ready_outputs_.clear();
    if (!read_cb_.is_null())
      SatisfyRead(DECRYPT_ERROR, NULL);
    return;
  }

  if (status == Decoder::kAborted) {
    if (!read_cb_.is_null())
      SatisfyRead(ABORTED, NULL);
    return;
  }

  // Any successful decode counts!
  if (buffer_size > 0) {
    StreamTraits::ReportStatistics(statistics_cb_, buffer_size);
  }

  // Drop decoding result if Reset() was called during decoding.
  // The resetting process will be handled when the decoder is reset.
  if (!reset_cb_.is_null())
    return;

  // Decoder flushed. Reinitialize the decoder.
  if (state_ == STATE_FLUSHING_DECODER &&
      status == Decoder::kOk && output->end_of_stream()) {
    ReinitializeDecoder();
    return;
  }

  if (status == Decoder::kNotEnoughData) {
    if (state_ == STATE_NORMAL)
      ReadFromDemuxerStream();
    else if (state_ == STATE_FLUSHING_DECODER)
      FlushDecoder();
    return;
  }

  DCHECK(output);

  // Store decoded output.
  ready_outputs_.push_back(output);
  scoped_refptr<Output> extra_output;
  while ((extra_output = decoder_->GetDecodeOutput()) != NULL) {
    ready_outputs_.push_back(extra_output);
  }

  // Satisfy outstanding read request, if any.
  if (!read_cb_.is_null()) {
    scoped_refptr<Output> read_result = ready_outputs_.front();
    ready_outputs_.pop_front();
    SatisfyRead(OK, output);
  }
}

template <DemuxerStream::Type StreamType>
void DecoderStream<StreamType>::ReadFromDemuxerStream() {
  FUNCTION_DVLOG(2);
  DCHECK_EQ(state_, STATE_NORMAL) << state_;
  DCHECK(CanDecodeMore());
  DCHECK(reset_cb_.is_null());
  DCHECK(stop_cb_.is_null());

  state_ = STATE_PENDING_DEMUXER_READ;
  stream_->Read(base::Bind(&DecoderStream<StreamType>::OnBufferReady,
                           weak_factory_.GetWeakPtr()));
}

template <DemuxerStream::Type StreamType>
void DecoderStream<StreamType>::OnBufferReady(
    DemuxerStream::Status status,
    const scoped_refptr<DecoderBuffer>& buffer) {
  FUNCTION_DVLOG(2) << ": " << status;
  DCHECK(task_runner_->BelongsToCurrentThread());
  DCHECK(state_ == STATE_PENDING_DEMUXER_READ || state_ == STATE_ERROR ||
         state_ == STATE_STOPPED)
      << state_;
  DCHECK_EQ(buffer.get() != NULL, status == DemuxerStream::kOk) << status;
  DCHECK(stop_cb_.is_null());

  // Decoding has been stopped (e.g due to an error).
  if (state_ != STATE_PENDING_DEMUXER_READ) {
    DCHECK(state_ == STATE_ERROR || state_ == STATE_STOPPED);
    DCHECK(read_cb_.is_null());
    return;
  }

  state_ = STATE_NORMAL;

  if (status == DemuxerStream::kConfigChanged) {
    FUNCTION_DVLOG(2) << ": " << "ConfigChanged";
    DCHECK(stream_->SupportsConfigChanges());

    if (!config_change_observer_cb_.is_null())
      config_change_observer_cb_.Run();

    state_ = STATE_FLUSHING_DECODER;
    if (!reset_cb_.is_null()) {
      // If we are using DecryptingDemuxerStream, we already called DDS::Reset()
      // which will continue the resetting process in it's callback.
      if (!decrypting_demuxer_stream_)
        Reset(base::ResetAndReturn(&reset_cb_));
      // Reinitialization will continue after Reset() is done.
    } else {
      FlushDecoder();
    }
    return;
  }

  if (!reset_cb_.is_null()) {
    // If we are using DecryptingDemuxerStream, we already called DDS::Reset()
    // which will continue the resetting process in it's callback.
    if (!decrypting_demuxer_stream_)
      Reset(base::ResetAndReturn(&reset_cb_));
    return;
  }

  if (status == DemuxerStream::kAborted) {
    SatisfyRead(DEMUXER_READ_ABORTED, NULL);
    return;
  }

  if (!splice_observer_cb_.is_null() && !buffer->end_of_stream()) {
    const bool has_splice_ts = buffer->splice_timestamp() != kNoTimestamp();
    if (active_splice_ || has_splice_ts) {
      splice_observer_cb_.Run(buffer->splice_timestamp());
      active_splice_ = has_splice_ts;
    }
  }

  DCHECK(status == DemuxerStream::kOk) << status;
  Decode(buffer);

  // Read more data if the decoder supports multiple parallel decoding requests.
  if (CanDecodeMore() && !buffer->end_of_stream())
    ReadFromDemuxerStream();
}

template <DemuxerStream::Type StreamType>
void DecoderStream<StreamType>::ReinitializeDecoder() {
  FUNCTION_DVLOG(2);
  DCHECK(task_runner_->BelongsToCurrentThread());
  DCHECK_EQ(state_, STATE_FLUSHING_DECODER) << state_;
  DCHECK_EQ(pending_decode_requests_, 0);

  DCHECK(StreamTraits::GetDecoderConfig(*stream_).IsValidConfig());
  state_ = STATE_REINITIALIZING_DECODER;
  DecoderStreamTraits<StreamType>::Initialize(
      decoder_.get(),
      StreamTraits::GetDecoderConfig(*stream_),
      low_delay_,
      base::Bind(&DecoderStream<StreamType>::OnDecoderReinitialized,
                 weak_factory_.GetWeakPtr()));
}

template <DemuxerStream::Type StreamType>
void DecoderStream<StreamType>::OnDecoderReinitialized(PipelineStatus status) {
  FUNCTION_DVLOG(2);
  DCHECK(task_runner_->BelongsToCurrentThread());
  DCHECK_EQ(state_, STATE_REINITIALIZING_DECODER) << state_;
  DCHECK(stop_cb_.is_null());

  // ReinitializeDecoder() can be called in two cases:
  // 1, Flushing decoder finished (see OnDecodeOutputReady()).
  // 2, Reset() was called during flushing decoder (see OnDecoderReset()).
  // Also, Reset() can be called during pending ReinitializeDecoder().
  // This function needs to handle them all!

  state_ = (status == PIPELINE_OK) ? STATE_NORMAL : STATE_ERROR;

  if (!reset_cb_.is_null()) {
    base::ResetAndReturn(&reset_cb_).Run();
    return;
  }

  if (read_cb_.is_null())
    return;

  if (state_ == STATE_ERROR) {
    SatisfyRead(DECODE_ERROR, NULL);
    return;
  }

  ReadFromDemuxerStream();
}

template <DemuxerStream::Type StreamType>
void DecoderStream<StreamType>::ResetDecoder() {
  FUNCTION_DVLOG(2);
  DCHECK(task_runner_->BelongsToCurrentThread());
  DCHECK(state_ == STATE_NORMAL || state_ == STATE_FLUSHING_DECODER ||
         state_ == STATE_ERROR) << state_;
  DCHECK(!reset_cb_.is_null());

  decoder_->Reset(base::Bind(&DecoderStream<StreamType>::OnDecoderReset,
                             weak_factory_.GetWeakPtr()));
}

template <DemuxerStream::Type StreamType>
void DecoderStream<StreamType>::OnDecoderReset() {
  FUNCTION_DVLOG(2);
  DCHECK(task_runner_->BelongsToCurrentThread());
  DCHECK(state_ == STATE_NORMAL || state_ == STATE_FLUSHING_DECODER ||
         state_ == STATE_ERROR) << state_;
  // If Reset() was called during pending read, read callback should be fired
  // before the reset callback is fired.
  DCHECK(read_cb_.is_null());
  DCHECK(!reset_cb_.is_null());
  DCHECK(stop_cb_.is_null());

  if (state_ != STATE_FLUSHING_DECODER) {
    base::ResetAndReturn(&reset_cb_).Run();
    return;
  }

  // The resetting process will be continued in OnDecoderReinitialized().
  ReinitializeDecoder();
}

template <DemuxerStream::Type StreamType>
void DecoderStream<StreamType>::StopDecoder() {
  FUNCTION_DVLOG(2);
  DCHECK(task_runner_->BelongsToCurrentThread());
  DCHECK(state_ != STATE_UNINITIALIZED && state_ != STATE_STOPPED) << state_;
  DCHECK(!stop_cb_.is_null());

  state_ = STATE_STOPPED;
  decoder_->Stop();
  stream_ = NULL;
  decoder_.reset();
  decrypting_demuxer_stream_.reset();
  // Post |stop_cb_| because pending |read_cb_| and/or |reset_cb_| are also
  // posted in Stop().
  task_runner_->PostTask(FROM_HERE, base::ResetAndReturn(&stop_cb_));
}

template class DecoderStream<DemuxerStream::VIDEO>;
template class DecoderStream<DemuxerStream::AUDIO>;

}  // namespace media
