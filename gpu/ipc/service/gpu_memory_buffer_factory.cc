// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/ipc/service/gpu_memory_buffer_factory.h"

#include <memory>

#include "build/build_config.h"

#if defined(OS_MACOSX)
#include "gpu/ipc/service/gpu_memory_buffer_factory_io_surface.h"
#endif

#if defined(OS_LINUX) || defined(OS_FUCHSIA)
#include "gpu/ipc/service/gpu_memory_buffer_factory_native_pixmap.h"
#endif

#if defined(OS_WIN)
#include "gpu/ipc/service/gpu_memory_buffer_factory_dxgi.h"
#endif

#if defined(OS_ANDROID)
#include "gpu/ipc/service/gpu_memory_buffer_factory_android_hardware_buffer.h"
#endif

namespace gpu {

// static
std::unique_ptr<GpuMemoryBufferFactory>
GpuMemoryBufferFactory::CreateNativeType(
    viz::VulkanContextProvider* vulkan_context_provider) {
#if defined(OS_MACOSX)
  return std::make_unique<GpuMemoryBufferFactoryIOSurface>();
#elif defined(OS_ANDROID) && !defined(CASTANETS)
  return std::make_unique<GpuMemoryBufferFactoryAndroidHardwareBuffer>();
#elif defined(OS_LINUX) || defined(OS_FUCHSIA)
  return std::make_unique<GpuMemoryBufferFactoryNativePixmap>(
      vulkan_context_provider);
#elif defined(OS_WIN)
  return std::make_unique<GpuMemoryBufferFactoryDXGI>();
#else
  return nullptr;
#endif
}

}  // namespace gpu
