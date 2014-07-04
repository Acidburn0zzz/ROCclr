//
// Copyright (c) 2010 Advanced Micro Devices, Inc. All rights reserved.
//

#include "platform/commandqueue.hpp"
#include "device/device.hpp"
#include "device/blit.hpp"
#include "utils/debug.hpp"

namespace device {

HostBlitManager::HostBlitManager(VirtualDevice& vDev, Setup setup)
    : BlitManager(setup)
    , vDev_(vDev)
    , dev_(vDev.device())
{ }

bool
HostBlitManager::readBuffer(
    device::Memory& srcMemory,
    void*       dstHost,
    const amd::Coord3D& origin,
    const amd::Coord3D& size,
    bool        entire) const
{
    // Map the device memory to CPU visible
    void*   src = srcMemory.cpuMap(vDev_, Memory::CpuReadOnly);
    if (NULL == src) {
        LogError("Couldn't map device memory for host read");
        return false;
    }

    // Copy memory
    amd::Os::fastMemcpy(dstHost,
        reinterpret_cast<const_address>(src) + origin[0], size[0]);

    // Unmap device memory
    srcMemory.cpuUnmap(vDev_);

    return true;
}

bool
HostBlitManager::readBufferRect(
    device::Memory& srcMemory,
    void*       dstHost,
    const amd::BufferRect&   bufRect,
    const amd::BufferRect&   hostRect,
    const amd::Coord3D& size,
    bool        entire) const
{
    // Map source memory
    void *src = srcMemory.cpuMap(vDev_, Memory::CpuReadOnly);
    if (src == NULL) {
        LogError("Couldn't map source memory");
        return false;
    }

    size_t  srcOffset;
    size_t  dstOffset;

    for (size_t z = 0; z < size[2]; ++z) {
        for (size_t y = 0; y < size[1]; ++y) {
            srcOffset   = bufRect.offset(0, y, z);
            dstOffset   = hostRect.offset(0, y, z);

            // Copy memory line by line
            amd::Os::fastMemcpy(
                (reinterpret_cast<address>(dstHost) + dstOffset),
                (reinterpret_cast<const_address>(src) + srcOffset),
                size[0]);
        }
    }

    // Unmap source memory
    srcMemory.cpuUnmap(vDev_);

    return true;
}

bool
HostBlitManager::readImage(
    device::Memory& srcMemory,
    void*       dstHost,
    const amd::Coord3D& origin,
    const amd::Coord3D& size,
    size_t      rowPitch,
    size_t      slicePitch,
    bool        entire) const
{
    size_t  startLayer  = origin[2];
    size_t  numLayers   = size[2];
    if (srcMemory.owner()->getType() == CL_MEM_OBJECT_IMAGE1D_ARRAY) {
        startLayer  = origin[1];
        numLayers   = size[1];
    }

    // rowPitch and slicePitch in bytes
    size_t  srcRowPitch;
    size_t  srcSlicePitch;

    // Get physical GPU memmory
    void*   src = srcMemory.cpuMap(vDev_, Memory::CpuReadOnly,
        startLayer, numLayers, &srcRowPitch, &srcSlicePitch);
    if (NULL == src) {
        LogError("Couldn't map GPU memory for host read");
        return false;
    }

    size_t elementSize = srcMemory.owner()->asImage()->getImageFormat().getElementSize();
    size_t  srcOffsBase = origin[0] * elementSize;
    size_t  copySize = size[0] * elementSize;
    size_t  srcOffs;
    size_t  dstOffs = 0;

    // Make sure we use the right pitch if it's not specified
    if (rowPitch == 0) {
        rowPitch = size[0] * elementSize;
    }

    // Make sure we use the right slice if it's not specified
    if (slicePitch == 0) {
        slicePitch = size[0] * size[1] * elementSize;
    }

    // Adjust destination offset with Y dimension
    srcOffsBase += srcRowPitch * origin[1];

    // Adjust the destination offset with Z dimension
    srcOffsBase += srcSlicePitch * origin[2];

    // Copy memory line by line
    for (size_t slice = 0; slice < size[2]; ++slice) {
        srcOffs = srcOffsBase + slice * srcSlicePitch;
        dstOffs = slice * slicePitch;

        // Copy memory line by line
        for (size_t row = 0; row < size[1]; ++row) {
            // Copy memory
            amd::Os::fastMemcpy(
                (reinterpret_cast<address>(dstHost) + dstOffs),
                (reinterpret_cast<const_address>(src) + srcOffs),
                copySize);

            srcOffs += srcRowPitch;
            dstOffs += rowPitch;
        }
    }

    // Unmap the device memory
    srcMemory.cpuUnmap(vDev_);

    return true;
}

bool
HostBlitManager::writeBuffer(
    const void* srcHost,
    device::Memory& dstMemory,
    const amd::Coord3D& origin,
    const amd::Coord3D& size,
    bool        entire) const
{
    uint    flags = 0;
    if (entire) {
        flags = Memory::CpuWriteOnly;
    }

    // Map the device memory to CPU visible
    void*   dst = dstMemory.cpuMap(vDev_, flags);
    if (NULL == dst) {
        LogError("Couldn't map GPU memory for host write");
        return false;
    }

    // Copy memory
    amd::Os::fastMemcpy(
        reinterpret_cast<address>(dst) + origin[0], srcHost, size[0]);

    // Unmap the device memory
    dstMemory.cpuUnmap(vDev_);

    return true;
}

bool
HostBlitManager::writeBufferRect(
    const void* srcHost,
    device::Memory& dstMemory,
    const amd::BufferRect&   hostRect,
    const amd::BufferRect&   bufRect,
    const amd::Coord3D& size,
    bool        entire) const
{
    // Map destination memory
    void *dst = dstMemory.cpuMap(vDev_, (entire) ? Memory::CpuWriteOnly : 0);
    if (dst == NULL) {
        LogError("Couldn't map destination memory");
        return false;
    }

    size_t  srcOffset;
    size_t  dstOffset;

    for (size_t z = 0; z < size[2]; ++z) {
        for (size_t y = 0; y < size[1]; ++y) {
            srcOffset   = hostRect.offset(0, y, z);
            dstOffset   = bufRect.offset(0, y, z);

            // Copy memory line by line
            amd::Os::fastMemcpy(
                (reinterpret_cast<address>(dst) + dstOffset),
                (reinterpret_cast<const_address>(srcHost) + srcOffset),
                size[0]);
        }
    }

    // Unmap destination memory
    dstMemory.cpuUnmap(vDev_);

    return true;
}

bool
HostBlitManager::writeImage(
    const void* srcHost,
    device::Memory& dstMemory,
    const amd::Coord3D& origin,
    const amd::Coord3D& size,
    size_t      rowPitch,
    size_t      slicePitch,
    bool        entire) const
{
    uint    flags = 0;
    if (entire) {
        flags = Memory::CpuWriteOnly;
    }

    size_t  startLayer  = origin[2];
    size_t  numLayers   = size[2];
    if (dstMemory.owner()->getType() == CL_MEM_OBJECT_IMAGE1D_ARRAY) {
        startLayer  = origin[1];
        numLayers   = size[1];
    }

    // rowPitch and slicePitch in bytes
    size_t  dstRowPitch;
    size_t  dstSlicePitch;
    // Map the device memory to CPU visible
    void*   dst = dstMemory.cpuMap(vDev_, flags,
        startLayer, numLayers, &dstRowPitch, &dstSlicePitch);
    if (NULL == dst) {
        LogError("Couldn't map GPU memory for host write");
        return false;
    }

    size_t elementSize = dstMemory.owner()->asImage()->getImageFormat().getElementSize();
    size_t  srcOffs = 0;
    size_t  copySize = size[0] * elementSize;
    size_t  dstOffsBase = origin[0] * elementSize;
    size_t  dstOffs;

    // Make sure we use the right pitch if it's not specified
    if (rowPitch == 0) {
        rowPitch = size[0] * elementSize;
    }

    // Make sure we use the right slice if it's not specified
    if (slicePitch == 0) {
        slicePitch = size[0] * size[1] * elementSize;
    }

    // Adjust the destination offset with Y dimension
    dstOffsBase += dstRowPitch * origin[1];

    // Adjust the destination offset with Z dimension
    dstOffsBase += dstSlicePitch * origin[2];

    // Copy memory slice by slice
    for (size_t slice = 0; slice < size[2]; ++slice) {
        dstOffs = dstOffsBase + slice * dstSlicePitch;
        srcOffs = slice * slicePitch;

        // Copy memory line by line
        for (size_t row = 0; row < size[1]; ++row) {
            // Copy memory
            amd::Os::fastMemcpy(
                (reinterpret_cast<address>(dst) + dstOffs),
                (reinterpret_cast<const_address>(srcHost) + srcOffs),
                copySize);

            dstOffs += dstRowPitch;
            srcOffs += rowPitch;
        }
    }

    // Unmap the device memory
    dstMemory.cpuUnmap(vDev_);

    return true;
}

bool
HostBlitManager::copyBuffer(
    device::Memory& srcMemory,
    device::Memory& dstMemory,
    const amd::Coord3D& srcOrigin,
    const amd::Coord3D& dstOrigin,
    const amd::Coord3D& size,
    bool        entire) const
{
    // Map source memory
    void *src = srcMemory.cpuMap(vDev_,
        // Overlap detection
        (&srcMemory == &dstMemory) ? 0 : Memory::CpuReadOnly);
    if (src == NULL) {
        LogError("Couldn't map source memory");
        return false;
    }

    // Map destination memory
    void *dst = dstMemory.cpuMap(vDev_, (entire) ? Memory::CpuWriteOnly : 0);
    if (dst == NULL) {
        LogError("Couldn't map destination memory");
        return false;
    }

    // Straight forward buffer copy
    amd::Os::fastMemcpy(
        (reinterpret_cast<address>(dst) + dstOrigin[0]),
        (reinterpret_cast<const_address>(src) + srcOrigin[0]),
        size[0]);

    // Unmap source and destination memory
    dstMemory.cpuUnmap(vDev_);
    srcMemory.cpuUnmap(vDev_);

    return true;
}

bool
HostBlitManager::copyBufferRect(
    device::Memory& srcMemory,
    device::Memory& dstMemory,
    const amd::BufferRect&   srcRect,
    const amd::BufferRect&   dstRect,
    const amd::Coord3D& size,
    bool        entire) const
{
    // Map source memory
    void *src = srcMemory.cpuMap(vDev_,
        // Overlap detection
        (&srcMemory == &dstMemory) ? 0 : Memory::CpuReadOnly);
    if (src == NULL) {
        LogError("Couldn't map source memory");
        return false;
    }

    // Map destination memory
    void *dst = dstMemory.cpuMap(vDev_, (entire) ? Memory::CpuWriteOnly : 0);
    if (dst == NULL) {
        LogError("Couldn't map destination memory");
        return false;
    }

    for (size_t z = 0; z < size[2]; ++z) {
        for (size_t y = 0; y < size[1]; ++y) {
            size_t srcOffset = srcRect.offset(0, y, z);
            size_t dstOffset = dstRect.offset(0, y, z);

            // Copy memory line by line
            amd::Os::fastMemcpy(
                (reinterpret_cast<address>(dst) + dstOffset),
                (reinterpret_cast<const_address>(src) + srcOffset),
                size[0]);
        }
    }

    // Unmap source and destination memory
    dstMemory.cpuUnmap(vDev_);
    srcMemory.cpuUnmap(vDev_);

    return true;
}

bool
HostBlitManager::copyImageToBuffer(
    device::Memory& srcMemory,
    device::Memory& dstMemory,
    const amd::Coord3D& srcOrigin,
    const amd::Coord3D& dstOrigin,
    const amd::Coord3D& size,
    bool        entire,
    size_t      rowPitch,
    size_t      slicePitch) const
{
    size_t  startLayer  = srcOrigin[2];
    size_t  numLayers   = size[2];
    if (srcMemory.owner()->getType() == CL_MEM_OBJECT_IMAGE1D_ARRAY) {
        startLayer  = srcOrigin[1];
        numLayers   = size[1];
    }
    // rowPitch and slicePitch in bytes
    size_t  srcRowPitch;
    size_t  srcSlicePitch;
    // Map source memory
    void *src = srcMemory.cpuMap(vDev_, Memory::CpuReadOnly,
        startLayer, numLayers, &srcRowPitch, &srcSlicePitch);
    if (src == NULL) {
        LogError("Couldn't map source memory");
        return false;
    }
    size_t elementSize = srcMemory.owner()->asImage()->getImageFormat().getElementSize();

    // Map destination memory
    void *dst = dstMemory.cpuMap(vDev_, (entire) ? Memory::CpuWriteOnly : 0);
    if (dst == NULL) {
        LogError("Couldn't map destination memory");
        return false;
    }

    size_t  srcOffs     = srcOrigin[0];
    size_t  dstOffs     = dstOrigin[0];
    size_t  srcOffsOrg;
    size_t  copySize    = size[0];

    // Calculate the offset in bytes
    srcOffs     *= elementSize;
    copySize    *= elementSize;

    // Adjust source offset with Y and Z dimensions
    srcOffs += srcRowPitch * srcOrigin[1];
    srcOffs += srcSlicePitch * srcOrigin[2];

    srcOffsOrg = srcOffs;

    // Copy memory slice by slice
    for (size_t slice = 0; slice < size[2]; ++slice) {
        srcOffs = srcOffsOrg + slice * srcSlicePitch;

        // Copy memory line by line
        for (size_t rows = 0; rows < size[1]; ++rows) {
            amd::Os::fastMemcpy(
                (reinterpret_cast<address>(dst) + dstOffs),
                (reinterpret_cast<const_address>(src) + srcOffs),
                copySize);

            srcOffs += srcRowPitch;
            dstOffs += copySize;
        }
    }

    // Unmap source and destination memory
    srcMemory.cpuUnmap(vDev_);
    dstMemory.cpuUnmap(vDev_);

    return true;
}

bool
HostBlitManager::copyBufferToImage(
    device::Memory&     srcMemory,
    device::Memory&     dstMemory,
    const amd::Coord3D& srcOrigin,
    const amd::Coord3D& dstOrigin,
    const amd::Coord3D& size,
    bool        entire,
    size_t      rowPitch,
    size_t      slicePitch) const
{
    // Map source memory
    void *src = srcMemory.cpuMap(vDev_, Memory::CpuReadOnly);
    if (src == NULL) {
        LogError("Couldn't map source memory");
        return false;
    }

    size_t  startLayer  = dstOrigin[2];
    size_t  numLayers   = size[2];
    if (dstMemory.owner()->getType() == CL_MEM_OBJECT_IMAGE1D_ARRAY) {
        startLayer  = dstOrigin[1];
        numLayers   = size[1];
    }
    // rowPitch and slicePitch in bytes
    size_t  dstRowPitch;
    size_t  dstSlicePitch;
    // Map destination memory
    void *dst = dstMemory.cpuMap(vDev_, (entire) ? Memory::CpuWriteOnly : 0,
        startLayer, numLayers, &dstRowPitch, &dstSlicePitch);
    if (dst == NULL) {
        LogError("Couldn't map destination memory");
        return false;
    }

    size_t elementSize = dstMemory.owner()->asImage()->getImageFormat().getElementSize();
    size_t  srcOffs     = srcOrigin[0];
    size_t  dstOffs     = dstOrigin[0];
    size_t  dstOffsOrg;
    size_t  copySize    = size[0];

    // Calculate the offset in bytes
    dstOffs     *= elementSize;
    copySize    *= elementSize;

    // Adjust destination offset with Y and Z dimension
    dstOffs += dstRowPitch * dstOrigin[1];
    dstOffs += dstSlicePitch * dstOrigin[2];

    dstOffsOrg = dstOffs;

    // Copy memory slice by slice
    for (size_t slice = 0; slice < size[2]; ++slice) {
        dstOffs = dstOffsOrg + slice * dstSlicePitch;

        // Copy memory line by line
        for (size_t rows = 0; rows < size[1]; ++rows) {
            amd::Os::fastMemcpy(
                (reinterpret_cast<address>(dst) + dstOffs),
                (reinterpret_cast<const_address>(src) + srcOffs),
                copySize);

            srcOffs += copySize;
            dstOffs += dstRowPitch;
        }
    }

    // Unmap source and destination memory
    srcMemory.cpuUnmap(vDev_);
    dstMemory.cpuUnmap(vDev_);

    return true;
}

bool
HostBlitManager::copyImage(
    device::Memory& srcMemory,
    device::Memory& dstMemory,
    const amd::Coord3D& srcOrigin,
    const amd::Coord3D& dstOrigin,
    const amd::Coord3D& size,
    bool        entire) const
{
    size_t  startLayer  = srcOrigin[2];
    size_t  numLayers   = size[2];
    if (srcMemory.owner()->getType() == CL_MEM_OBJECT_IMAGE1D_ARRAY) {
        startLayer  = srcOrigin[1];
        numLayers   = size[1];
    }
    // rowPitch and slicePitch in bytes
    size_t  srcRowPitch;
    size_t  srcSlicePitch;
    // Map source memory
    void *src = srcMemory.cpuMap(vDev_, Memory::CpuReadOnly,
        startLayer, numLayers, &srcRowPitch, &srcSlicePitch);
    if (src == NULL) {
        LogError("Couldn't map source memory");
        return false;
    }
    if (dstMemory.owner()->getType() == CL_MEM_OBJECT_IMAGE1D_ARRAY) {
        startLayer  = dstOrigin[1];
        numLayers   = size[1];
    }
    else {
        startLayer  = dstOrigin[2];
        numLayers   = size[2];
    }

    // rowPitch and slicePitch in bytes
    size_t  dstRowPitch;
    size_t  dstSlicePitch;
    // Map destination memory
    void *dst = dstMemory.cpuMap(vDev_, (entire) ? Memory::CpuWriteOnly : 0,
        startLayer, numLayers, &dstRowPitch, &dstSlicePitch);
    if (dst == NULL) {
        LogError("Couldn't map destination memory");
        return false;
    }

    size_t elementSize = dstMemory.owner()->asImage()->getImageFormat().getElementSize();
    assert(elementSize == srcMemory.owner()->asImage()->getImageFormat().getElementSize());

    size_t  srcOffs     = srcOrigin[0];
    size_t  dstOffs     = dstOrigin[0];
    size_t  srcOffsOrg;
    size_t  dstOffsOrg;
    size_t  copySize    = size[0];

    // Calculate the offsets in bytes
    srcOffs     *= elementSize;
    dstOffs     *= elementSize;
    copySize    *= elementSize;

    // Adjust destination and sorce offsets with Y dimension
    srcOffs += srcRowPitch * srcOrigin[1];
    dstOffs += dstRowPitch * dstOrigin[1];

    // Adjust destination and sorce offsets with Z dimension
    srcOffs += srcSlicePitch * srcOrigin[2];
    dstOffs += dstSlicePitch * dstOrigin[2];

    srcOffsOrg = srcOffs;
    dstOffsOrg = dstOffs;

    // Copy memory slice by slice
    for (size_t slice = 0; slice < size[2]; ++slice) {
        srcOffs = srcOffsOrg + slice * srcSlicePitch;
        dstOffs = dstOffsOrg + slice * dstSlicePitch;

        // Copy memory line by line
        for (size_t rows = 0; rows < size[1]; ++rows) {
            amd::Os::fastMemcpy(
                (reinterpret_cast<address>(dst) + dstOffs),
                (reinterpret_cast<const_address>(src) + srcOffs),
                copySize);

            srcOffs += srcRowPitch;
            dstOffs += dstRowPitch;
        }
    }

    // Unmap source and destination memory
    srcMemory.cpuUnmap(vDev_);
    dstMemory.cpuUnmap(vDev_);

    return true;
}

bool
HostBlitManager::fillBuffer(
    device::Memory& memory,
    const void* pattern,
    size_t      patternSize,
    const amd::Coord3D& origin,
    const amd::Coord3D& size,
    bool        entire
    ) const
{
    // Map memory
    void* fillMem = memory.cpuMap(vDev_, (entire) ? Memory::CpuWriteOnly : 0);
    if (fillMem == NULL) {
        LogError("Couldn't map destination memory");
        return false;
    }

    size_t  offset      = origin[0];
    size_t  fillSize    = size[0];

    if ((fillSize % patternSize) != 0) {
        LogError("Misaligned buffer size and pattern size!");
    }

    // Fill the buffer memory with a pattern
    for (size_t i = 0; i < (fillSize / patternSize); i++) {
        memcpy(
            (reinterpret_cast<address>(fillMem) + offset),
            (reinterpret_cast<const_address>(pattern)),
            patternSize
            );
        offset += patternSize;
    }

    // Unmap source and destination memory
    memory.cpuUnmap(vDev_);

    return true;
}

bool
HostBlitManager::fillImage(
    device::Memory&     memory,
    const void* pattern,
    const amd::Coord3D& origin,
    const amd::Coord3D& size,
    bool        entire
    ) const
{
    size_t  startLayer  = origin[2];
    size_t  numLayers   = size[2];
    if (memory.owner()->getType() == CL_MEM_OBJECT_IMAGE1D_ARRAY) {
        startLayer  = origin[1];
        numLayers   = size[1];
    }
    // rowPitch and slicePitch in bytes
    size_t  devRowPitch;
    size_t  devSlicePitch;
    // Map memory
    void* fillMem = memory.cpuMap(vDev_, (entire) ? Memory::CpuWriteOnly : 0,
        startLayer, numLayers, &devRowPitch, &devSlicePitch);
    if (fillMem == NULL) {
        LogError("Couldn't map destination memory");
        return false;
    }

    float fillValue[4];
    memset(fillValue, 0, sizeof(fillValue));
    memory.owner()->asImage()->getImageFormat().formatColor(pattern, fillValue);

    size_t  elementSize = memory.owner()->asImage()->getImageFormat().getElementSize();
    size_t  offset  = origin[0] * elementSize;
    size_t  offsetOrg;

    // Adjust offset with Y dimension
    offset += devRowPitch * origin[1];

    // Adjust offset with Z dimension
    offset += devSlicePitch * origin[2];

    offsetOrg = offset;

    // Fill the image memory with a pattern
    for (size_t slice = 0; slice < size[2]; ++slice) {
        offset = offsetOrg + slice * devSlicePitch;

        for (size_t rows = 0; rows < size[1]; ++rows) {
            size_t  pixOffset = offset;

            // Copy memory pixel by pixel
            for (size_t column = 0; column < size[0]; ++column) {
                memcpy(
                    (reinterpret_cast<address>(fillMem) + pixOffset),
                    (reinterpret_cast<const_address>(fillValue)),
                    elementSize
                    );
                pixOffset += elementSize;
            }

            offset += devRowPitch;
        }
    }

    // Unmap memory
    memory.cpuUnmap(vDev_);

    return true;
}

} // namespace gpu
