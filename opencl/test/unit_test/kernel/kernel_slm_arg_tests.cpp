/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/helpers/basic_math.h"
#include "shared/source/helpers/ptr_math.h"

#include "opencl/source/kernel/kernel.h"
#include "opencl/test/unit_test/fixtures/cl_device_fixture.h"
#include "opencl/test/unit_test/mocks/mock_context.h"
#include "opencl/test/unit_test/mocks/mock_kernel.h"
#include "opencl/test/unit_test/mocks/mock_program.h"
#include "test.h"

#include "gtest/gtest.h"

using namespace NEO;

class KernelSlmArgTest : public Test<ClDeviceFixture> {
  protected:
    void SetUp() override {
        ClDeviceFixture::SetUp();
        pKernelInfo = std::make_unique<KernelInfo>();
        KernelArgPatchInfo kernelArgPatchInfo;

        pKernelInfo->kernelArgInfo.resize(3);
        pKernelInfo->kernelArgInfo[2].kernelArgPatchInfoVector.push_back(kernelArgPatchInfo);
        pKernelInfo->kernelArgInfo[1].kernelArgPatchInfoVector.push_back(kernelArgPatchInfo);
        pKernelInfo->kernelArgInfo[0].kernelArgPatchInfoVector.push_back(kernelArgPatchInfo);

        pKernelInfo->kernelArgInfo[0].kernelArgPatchInfoVector[0].crossthreadOffset = 0x10;
        pKernelInfo->kernelArgInfo[0].slmAlignment = 0x1;
        pKernelInfo->kernelArgInfo[1].kernelArgPatchInfoVector[0].crossthreadOffset = 0x20;
        pKernelInfo->kernelArgInfo[1].kernelArgPatchInfoVector[0].size = sizeof(void *);
        pKernelInfo->kernelArgInfo[2].kernelArgPatchInfoVector[0].crossthreadOffset = 0x30;
        pKernelInfo->kernelArgInfo[2].slmAlignment = 0x400;
        pKernelInfo->workloadInfo.slmStaticSize = 3 * KB;

        program = std::make_unique<MockProgram>(toClDeviceVector(*pClDevice));
        pKernel = new MockKernel(program.get(), *pKernelInfo);
        ASSERT_EQ(CL_SUCCESS, pKernel->initialize());

        pKernel->setKernelArgHandler(0, &Kernel::setArgLocal);
        pKernel->setKernelArgHandler(1, &Kernel::setArgImmediate);
        pKernel->setKernelArgHandler(2, &Kernel::setArgLocal);

        uint32_t crossThreadData[0x40] = {};
        crossThreadData[0x20 / sizeof(uint32_t)] = 0x12344321;
        pKernel->setCrossThreadData(crossThreadData, sizeof(crossThreadData));
    }

    void TearDown() override {
        delete pKernel;

        ClDeviceFixture::TearDown();
    }

    cl_int retVal = CL_SUCCESS;
    std::unique_ptr<MockProgram> program;
    MockKernel *pKernel = nullptr;
    std::unique_ptr<KernelInfo> pKernelInfo;

    static const size_t slmSize0 = 0x200;
    static const size_t slmSize2 = 0x30;
};

TEST_F(KernelSlmArgTest, WhenSettingSizeThenAlignmentOfHigherSlmArgsIsUpdated) {
    pKernel->setArg(0, slmSize0, nullptr);
    pKernel->setArg(2, slmSize2, nullptr);

    auto crossThreadData = reinterpret_cast<uint32_t *>(pKernel->getCrossThreadData(rootDeviceIndex));
    auto slmOffset = ptrOffset(crossThreadData, 0x10);
    EXPECT_EQ(0u, *slmOffset);

    slmOffset = ptrOffset(crossThreadData, 0x20);
    EXPECT_EQ(0x12344321u, *slmOffset);

    slmOffset = ptrOffset(crossThreadData, 0x30);
    EXPECT_EQ(0x400u, *slmOffset);

    EXPECT_EQ(5 * KB, pKernel->slmTotalSize);
}

TEST_F(KernelSlmArgTest, GivenReverseOrderWhenSettingSizeThenAlignmentOfHigherSlmArgsIsUpdated) {
    pKernel->setArg(2, slmSize2, nullptr);
    pKernel->setArg(0, slmSize0, nullptr);

    auto crossThreadData = reinterpret_cast<uint32_t *>(pKernel->getCrossThreadData(rootDeviceIndex));
    auto slmOffset = ptrOffset(crossThreadData, 0x10);
    EXPECT_EQ(0u, *slmOffset);

    slmOffset = ptrOffset(crossThreadData, 0x20);
    EXPECT_EQ(0x12344321u, *slmOffset);

    slmOffset = ptrOffset(crossThreadData, 0x30);
    EXPECT_EQ(0x400u, *slmOffset);

    EXPECT_EQ(5 * KB, pKernel->slmTotalSize);
}
