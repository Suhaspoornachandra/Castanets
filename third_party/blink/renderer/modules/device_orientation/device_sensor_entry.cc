// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/device_orientation/device_sensor_entry.h"

#include "services/device/public/cpp/generic_sensor/sensor_reading_shared_buffer_reader.h"
#include "third_party/blink/renderer/modules/device_orientation/device_sensor_event_pump.h"
#include "third_party/blink/renderer/platform/heap/persistent.h"
#include "third_party/blink/renderer/platform/wtf/functional.h"

#if defined(CASTANETS)
#include "base/distributed_chromium_util.h"
#include "mojo/public/cpp/system/sync.h"
#endif

namespace blink {

DeviceSensorEntry::DeviceSensorEntry(DeviceSensorEventPump* event_pump,
                                     device::mojom::blink::SensorType type)
    : event_pump_(event_pump), type_(type) {}

void DeviceSensorEntry::Dispose() {
  client_binding_.Close();
}

DeviceSensorEntry::~DeviceSensorEntry() = default;

void DeviceSensorEntry::Start(
    device::mojom::blink::SensorProvider* sensor_provider) {
  if (state_ == State::NOT_INITIALIZED) {
    state_ = State::INITIALIZING;
    sensor_provider->GetSensor(type_,
                               WTF::Bind(&DeviceSensorEntry::OnSensorCreated,
                                         WrapWeakPersistent(this)));
  } else if (state_ == State::SUSPENDED) {
    sensor_->Resume();
    state_ = State::ACTIVE;
    event_pump_->DidStartIfPossible();
  } else if (state_ == State::SHOULD_SUSPEND) {
    // This can happen when calling Start(), Stop(), Start() in a sequence:
    // After the first Start() call, the sensor state is
    // State::INITIALIZING. Then after the Stop() call, the sensor
    // state is State::SHOULD_SUSPEND, and the next Start() call needs
    // to set the sensor state to be State::INITIALIZING again.
    state_ = State::INITIALIZING;
  } else {
    NOTREACHED();
  }
}

void DeviceSensorEntry::Stop() {
  if (sensor_) {
    sensor_->Suspend();
    state_ = State::SUSPENDED;
  } else if (state_ == State::INITIALIZING) {
    // When the sensor needs to be suspended, and it is still in the
    // State::INITIALIZING state, the sensor creation is not affected
    // (the DeviceSensorEntry::OnSensorCreated() callback will run as usual),
    // but the sensor is marked as State::SHOULD_SUSPEND, and when the sensor is
    // created successfully, it will be suspended and its state will be marked
    // as State::SUSPENDED in the DeviceSensorEntry::OnSensorAddConfiguration().
    state_ = State::SHOULD_SUSPEND;
  }
}

bool DeviceSensorEntry::IsConnected() const {
  return sensor_.is_bound();
}

bool DeviceSensorEntry::ReadyOrErrored() const {
  // When some sensors are not available, the pump still needs to fire
  // events which set the unavailable sensor data fields to null.
  return state_ == State::ACTIVE || state_ == State::NOT_INITIALIZED;
}

bool DeviceSensorEntry::GetReading(device::SensorReading* reading) {
  if (!sensor_)
    return false;

  DCHECK(shared_buffer_);

#if defined(CASTANETS)
  if (base::Castanets::IsEnabled())
    mojo::WaitSyncSharedMemory(shared_buffer_handle_->GetGUID());
#endif

  if (!shared_buffer_handle_->is_valid() ||
      !shared_buffer_reader_->GetReading(reading)) {
    HandleSensorError();
    return false;
  }

  return true;
}

void DeviceSensorEntry::Trace(Visitor* visitor) {
  visitor->Trace(event_pump_);
}

void DeviceSensorEntry::RaiseError() {
  HandleSensorError();
}

void DeviceSensorEntry::SensorReadingChanged() {
  // Since DeviceSensorEventPump::FireEvent is called in a fixed
  // frequency, the |shared_buffer| is read frequently, and
  // Sensor::ConfigureReadingChangeNotifications() is set to false,
  // so this method is not called and doesn't need to be implemented.
  NOTREACHED();
}

void DeviceSensorEntry::OnSensorCreated(
    device::mojom::blink::SensorCreationResult result,
    device::mojom::blink::SensorInitParamsPtr params) {
  // |state_| can be State::SHOULD_SUSPEND if Stop() is called
  // before OnSensorCreated() is called.
  DCHECK(state_ == State::INITIALIZING || state_ == State::SHOULD_SUSPEND);

  if (!params) {
    HandleSensorError();
    event_pump_->DidStartIfPossible();
    return;
  }
  DCHECK_EQ(device::mojom::SensorCreationResult::SUCCESS, result);

  constexpr size_t kReadBufferSize = sizeof(device::SensorReadingSharedBuffer);

  DCHECK_EQ(0u, params->buffer_offset % kReadBufferSize);

  sensor_.Bind(std::move(params->sensor));
  client_binding_.Bind(std::move(params->client_request));

  shared_buffer_handle_ = std::move(params->memory);
  DCHECK(!shared_buffer_);
#if defined(CASTANETS)
  // Partial memory mapping is not supported on shared memory management
  // for castanets. So we map the full memory here and when accessing memory
  // we access based on offset at L.155.
  if (base::Castanets::IsEnabled())
    shared_buffer_ =
        shared_buffer_handle_->MapAtOffset(shared_buffer_handle_->GetSize(), 0);
  else
#endif
    shared_buffer_ = shared_buffer_handle_->MapAtOffset(kReadBufferSize,
                                                        params->buffer_offset);

  if (!shared_buffer_) {
    HandleSensorError();
    event_pump_->DidStartIfPossible();
    return;
  }

#if defined(CASTANETS)
  if (base::Castanets::IsEnabled()) {
    const device::SensorReadingSharedBuffer* buffer =
        (const device::SensorReadingSharedBuffer*)((char*)shared_buffer_.get() +
                                                   params->buffer_offset);
    shared_buffer_reader_.reset(
        new device::SensorReadingSharedBufferReader(buffer));
  } else
#endif
  {
    const device::SensorReadingSharedBuffer* buffer =
        static_cast<const device::SensorReadingSharedBuffer*>(
            shared_buffer_.get());
    shared_buffer_reader_.reset(
        new device::SensorReadingSharedBufferReader(buffer));
  }

  device::mojom::blink::SensorConfigurationPtr config =
      std::move(params->default_configuration);
  config->frequency = std::min(
      static_cast<double>(DeviceSensorEventPump::kDefaultPumpFrequencyHz),
      params->maximum_frequency);

  sensor_.set_connection_error_handler(WTF::Bind(
      &DeviceSensorEntry::HandleSensorError, WrapWeakPersistent(this)));
  sensor_->ConfigureReadingChangeNotifications(/*enabled=*/false);
  sensor_->AddConfiguration(
      std::move(config), WTF::Bind(&DeviceSensorEntry::OnSensorAddConfiguration,
                                   WrapWeakPersistent(this)));
}

void DeviceSensorEntry::OnSensorAddConfiguration(bool success) {
  if (!success)
    HandleSensorError();

  if (state_ == State::INITIALIZING) {
    state_ = State::ACTIVE;
    event_pump_->DidStartIfPossible();
  } else if (state_ == State::SHOULD_SUSPEND) {
    sensor_->Suspend();
    state_ = State::SUSPENDED;
  }
}

void DeviceSensorEntry::HandleSensorError() {
  sensor_.reset();
  state_ = State::NOT_INITIALIZED;
  shared_buffer_handle_.reset();
  shared_buffer_.reset();
  client_binding_.Close();
}

}  // namespace blink
