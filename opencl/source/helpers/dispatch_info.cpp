/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "opencl/source/helpers/dispatch_info.h"

#include "opencl/source/kernel/kernel.h"

namespace NEO {
bool DispatchInfo::usesSlm() const {
    return (kernel == nullptr) ? false : kernel->slmTotalSize > 0;
}

bool DispatchInfo::usesStatelessPrintfSurface() const {
    return (kernel == nullptr) ? false : (kernel->getKernelInfo().patchInfo.pAllocateStatelessPrintfSurface != nullptr);
}

uint32_t DispatchInfo::getRequiredScratchSize() const {
    return (kernel == nullptr) ? 0 : kernel->getScratchSize();
}

uint32_t DispatchInfo::getRequiredPrivateScratchSize() const {
    return (kernel == nullptr) ? 0 : kernel->getPrivateScratchSize();
}

Kernel *MultiDispatchInfo::peekMainKernel() const {
    if (dispatchInfos.size() == 0) {
        return nullptr;
    }
    return mainKernel ? mainKernel : dispatchInfos.begin()->getKernel();
}

Kernel *MultiDispatchInfo::peekParentKernel() const {
    return (mainKernel && mainKernel->isParentKernel) ? mainKernel : nullptr;
}

void MultiDispatchInfo::backupUnifiedMemorySyncRequirement() {
    for (const auto &dispatchInfo : dispatchInfos) {
        dispatchInfo.getKernel()->setUnifiedMemorySyncRequirement(true);
    }
}
} // namespace NEO
