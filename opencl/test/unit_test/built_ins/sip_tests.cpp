/*
 * Copyright (C) 2018-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/built_ins/built_ins.h"
#include "shared/source/built_ins/sip.h"
#include "shared/test/unit_test/helpers/test_files.h"
#include "shared/test/unit_test/mocks/mock_device.h"

#include "opencl/test/unit_test/global_environment.h"
#include "opencl/test/unit_test/mocks/mock_program.h"
#include "test.h"

#include "gtest/gtest.h"

using namespace NEO;

namespace SipKernelTests {
std::string getDebugSipKernelNameWithBitnessAndProductSuffix(std::string &base, const char *product) {
    std::string fullName = base + std::string("_");

    if (sizeof(uintptr_t) == 8) {
        fullName.append("64_");
    } else {
        fullName.append("32_");
    }

    fullName.append(product);
    return fullName;
}

TEST(Sip, WhenSipKernelIsInvalidThenEmptyCompilerInternalOptionsAreReturned) {
    const char *opt = getSipKernelCompilerInternalOptions(SipKernelType::COUNT);
    ASSERT_NE(nullptr, opt);
    EXPECT_EQ(0U, strlen(opt));
}

TEST(Sip, WhenRequestingCsrSipKernelThenProperCompilerInternalOptionsAreReturned) {
    const char *opt = getSipKernelCompilerInternalOptions(SipKernelType::Csr);
    ASSERT_NE(nullptr, opt);
    EXPECT_STREQ("-cl-include-sip-csr", opt);
}

TEST(Sip, When32BitAddressesAreNotBeingForcedThenSipLlHasSameBitnessAsHostApplication) {
    auto mockDevice = std::unique_ptr<MockDevice>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(nullptr));
    EXPECT_NE(nullptr, mockDevice);
    mockDevice->deviceInfo.force32BitAddressess = false;
    const char *src = getSipLlSrc(*mockDevice);
    ASSERT_NE(nullptr, src);
    if (sizeof(void *) == 8) {
        EXPECT_NE(nullptr, strstr(src, "target datalayout = \"e-p:64:64:64\""));
        EXPECT_NE(nullptr, strstr(src, "target triple = \"spir64\""));
    } else {
        EXPECT_NE(nullptr, strstr(src, "target datalayout = \"e-p:32:32:32\""));
        EXPECT_NE(nullptr, strstr(src, "target triple = \"spir\""));
        EXPECT_EQ(nullptr, strstr(src, "target triple = \"spir64\""));
    }
}

TEST(Sip, When32BitAddressesAreBeingForcedThenSipLlHas32BitAddresses) {
    auto mockDevice = std::unique_ptr<MockDevice>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(nullptr));
    EXPECT_NE(nullptr, mockDevice);
    mockDevice->deviceInfo.force32BitAddressess = true;
    const char *src = getSipLlSrc(*mockDevice);
    ASSERT_NE(nullptr, src);
    EXPECT_NE(nullptr, strstr(src, "target datalayout = \"e-p:32:32:32\""));
    EXPECT_NE(nullptr, strstr(src, "target triple = \"spir\""));
    EXPECT_EQ(nullptr, strstr(src, "target triple = \"spir64\""));
}

TEST(Sip, GivenSipLlWhenGettingMetadataThenMetadataRequiredByCompilerIsReturned) {
    auto mockDevice = std::unique_ptr<MockDevice>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(nullptr));
    EXPECT_NE(nullptr, mockDevice);
    const char *src = getSipLlSrc(*mockDevice);
    ASSERT_NE(nullptr, src);

    EXPECT_NE(nullptr, strstr(src, "!opencl.compiler.options"));
    EXPECT_NE(nullptr, strstr(src, "!opencl.kernels"));
}

TEST(Sip, WhenGettingTypeThenCorrectTypeIsReturned) {
    SipKernel csr{SipKernelType::Csr, nullptr};
    EXPECT_EQ(SipKernelType::Csr, csr.getType());

    SipKernel dbgCsr{SipKernelType::DbgCsr, nullptr};
    EXPECT_EQ(SipKernelType::DbgCsr, dbgCsr.getType());

    SipKernel dbgCsrLocal{SipKernelType::DbgCsrLocal, nullptr};
    EXPECT_EQ(SipKernelType::DbgCsrLocal, dbgCsrLocal.getType());

    SipKernel undefined{SipKernelType::COUNT, nullptr};
    EXPECT_EQ(SipKernelType::COUNT, undefined.getType());
}

TEST(Sip, givenSipKernelClassWhenAskedForMaxDebugSurfaceSizeThenCorrectValueIsReturned) {
    EXPECT_EQ(0x1800000u, SipKernel::maxDbgSurfaceSize);
}

TEST(Sip, givenDebuggingInactiveWhenSipTypeIsQueriedThenCsrSipTypeIsReturned) {
    auto sipType = SipKernel::getSipKernelType(renderCoreFamily, false);
    EXPECT_EQ(SipKernelType::Csr, sipType);
}

TEST(DebugSip, givenDebuggingActiveWhenSipTypeIsQueriedThenDbgCsrSipTypeIsReturned) {
    auto sipType = SipKernel::getSipKernelType(renderCoreFamily, true);
    EXPECT_LE(SipKernelType::DbgCsr, sipType);
}

TEST(DebugSip, WhenRequestingDbgCsrSipKernelThenProperCompilerInternalOptionsAreReturned) {
    const char *opt = getSipKernelCompilerInternalOptions(SipKernelType::DbgCsr);
    ASSERT_NE(nullptr, opt);
    EXPECT_STREQ("-cl-include-sip-kernel-debug -cl-include-sip-csr -cl-set-bti:0", opt);
}

TEST(DebugSip, WhenRequestingDbgCsrWithLocalMemorySipKernelThenProperCompilerInternalOptionsAreReturned) {
    const char *opt = getSipKernelCompilerInternalOptions(SipKernelType::DbgCsrLocal);
    ASSERT_NE(nullptr, opt);
    EXPECT_STREQ("-cl-include-sip-kernel-local-debug -cl-include-sip-csr -cl-set-bti:0", opt);
}

TEST(DebugSip, givenBuiltInsWhenDbgCsrSipIsRequestedThanCorrectSipKernelIsReturned) {
    auto mockDevice = std::unique_ptr<MockDevice>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(nullptr));
    EXPECT_NE(nullptr, mockDevice);

    auto &builtins = *mockDevice->getBuiltIns();
    auto &sipKernel = builtins.getSipKernel(SipKernelType::DbgCsr, *mockDevice);

    EXPECT_NE(nullptr, &sipKernel);
    EXPECT_EQ(SipKernelType::DbgCsr, sipKernel.getType());
}

} // namespace SipKernelTests
