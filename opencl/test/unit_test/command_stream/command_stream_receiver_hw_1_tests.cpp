/*
 * Copyright (C) 2018-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/command_stream/scratch_space_controller_base.h"
#include "shared/test/unit_test/helpers/debug_manager_state_restore.h"
#include "shared/test/unit_test/utilities/base_object_utils.h"

#include "opencl/source/event/user_event.h"
#include "opencl/test/unit_test/command_stream/command_stream_receiver_hw_fixture.h"
#include "opencl/test/unit_test/fixtures/ult_command_stream_receiver_fixture.h"
#include "opencl/test/unit_test/mocks/mock_command_queue.h"
#include "opencl/test/unit_test/mocks/mock_csr.h"
#include "opencl/test/unit_test/mocks/mock_kernel.h"
#include "opencl/test/unit_test/mocks/mock_timestamp_container.h"
#include "test.h"

using namespace NEO;

HWCMDTEST_F(IGFX_GEN8_CORE, UltCommandStreamReceiverTest, givenPreambleSentAndThreadArbitrationPolicyNotChangedWhenEstimatingPreambleCmdSizeThenReturnItsValue) {
    auto &commandStreamReceiver = pDevice->getUltCommandStreamReceiver<FamilyType>();
    commandStreamReceiver.isPreambleSent = true;
    commandStreamReceiver.requiredThreadArbitrationPolicy = commandStreamReceiver.lastSentThreadArbitrationPolicy;
    auto expectedCmdSize = sizeof(typename FamilyType::PIPE_CONTROL) + sizeof(typename FamilyType::MEDIA_VFE_STATE);
    EXPECT_EQ(expectedCmdSize, commandStreamReceiver.getRequiredCmdSizeForPreamble(*pDevice));
}

HWCMDTEST_F(IGFX_GEN8_CORE, UltCommandStreamReceiverTest, givenNotSentStateSipWhenFirstTaskIsFlushedThenStateSipCmdIsAddedAndIsStateSipSentSetToTrue) {
    using STATE_SIP = typename FamilyType::STATE_SIP;

    auto mockDevice = std::make_unique<MockClDevice>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(nullptr));

    if (mockDevice->getHardwareInfo().capabilityTable.defaultPreemptionMode == PreemptionMode::MidThread) {
        mockDevice->setPreemptionMode(PreemptionMode::MidThread);

        auto &csr = mockDevice->getUltCommandStreamReceiver<FamilyType>();
        csr.isPreambleSent = true;

        CommandQueueHw<FamilyType> commandQueue(nullptr, mockDevice.get(), 0, false);
        auto &commandStream = commandQueue.getCS(4096u);

        DispatchFlags dispatchFlags = DispatchFlagsHelper::createDefaultDispatchFlags();
        dispatchFlags.preemptionMode = PreemptionMode::MidThread;

        MockGraphicsAllocation allocation(nullptr, 0);
        IndirectHeap heap(&allocation);

        csr.flushTask(commandStream,
                      0,
                      heap,
                      heap,
                      heap,
                      0,
                      dispatchFlags,
                      mockDevice->getDevice());

        EXPECT_TRUE(csr.isStateSipSent);

        HardwareParse hwParser;
        hwParser.parseCommands<FamilyType>(csr.getCS(0));

        auto stateSipItor = find<STATE_SIP *>(hwParser.cmdList.begin(), hwParser.cmdList.end());
        EXPECT_NE(hwParser.cmdList.end(), stateSipItor);
    }
}

HWTEST_F(UltCommandStreamReceiverTest, givenCsrWhenProgramStateSipIsCalledThenIsStateSipCalledIsSetToTrue) {
    auto &commandStreamReceiver = pDevice->getUltCommandStreamReceiver<FamilyType>();

    auto requiredSize = PreemptionHelper::getRequiredStateSipCmdSize<FamilyType>(*pDevice);
    StackVec<char, 4096> buffer(requiredSize);
    LinearStream cmdStream(buffer.begin(), buffer.size());

    commandStreamReceiver.programStateSip(cmdStream, *pDevice);
    EXPECT_TRUE(commandStreamReceiver.isStateSipSent);
}

HWTEST_F(UltCommandStreamReceiverTest, givenSentStateSipFlagSetWhenGetRequiredStateSipCmdSizeIsCalledThenStateSipCmdSizeIsNotIncluded) {
    auto &commandStreamReceiver = pDevice->getUltCommandStreamReceiver<FamilyType>();
    DispatchFlags dispatchFlags = DispatchFlagsHelper::createDefaultDispatchFlags();

    commandStreamReceiver.isStateSipSent = false;
    auto sizeWithStateSipIsNotSent = commandStreamReceiver.getRequiredCmdStreamSize(dispatchFlags, *pDevice);

    commandStreamReceiver.isStateSipSent = true;
    auto sizeWhenSipIsSent = commandStreamReceiver.getRequiredCmdStreamSize(dispatchFlags, *pDevice);

    auto sizeForStateSip = PreemptionHelper::getRequiredStateSipCmdSize<FamilyType>(*pDevice);
    EXPECT_EQ(sizeForStateSip, sizeWithStateSipIsNotSent - sizeWhenSipIsSent);
}

HWTEST_F(UltCommandStreamReceiverTest, givenSentStateSipFlagSetAndSourceLevelDebuggerIsActiveWhenGetRequiredStateSipCmdSizeIsCalledThenStateSipCmdSizeIsIncluded) {
    auto &commandStreamReceiver = pDevice->getUltCommandStreamReceiver<FamilyType>();
    DispatchFlags dispatchFlags = DispatchFlagsHelper::createDefaultDispatchFlags();

    commandStreamReceiver.isStateSipSent = true;
    auto sizeWithoutSourceKernelDebugging = commandStreamReceiver.getRequiredCmdStreamSize(dispatchFlags, *pDevice);

    pDevice->setDebuggerActive(true);
    commandStreamReceiver.isStateSipSent = true;
    auto sizeWithSourceKernelDebugging = commandStreamReceiver.getRequiredCmdStreamSize(dispatchFlags, *pDevice);

    auto sizeForStateSip = PreemptionHelper::getRequiredStateSipCmdSize<FamilyType>(*pDevice);
    EXPECT_EQ(sizeForStateSip, sizeWithSourceKernelDebugging - sizeWithoutSourceKernelDebugging - PreambleHelper<FamilyType>::getKernelDebuggingCommandsSize(true));
    pDevice->setDebuggerActive(false);
}

HWTEST_F(UltCommandStreamReceiverTest, givenPreambleSentAndThreadArbitrationPolicyChangedWhenEstimatingPreambleCmdSizeThenResultDependsOnPolicyProgrammingCmdSize) {
    auto &commandStreamReceiver = pDevice->getUltCommandStreamReceiver<FamilyType>();
    commandStreamReceiver.isPreambleSent = true;

    commandStreamReceiver.requiredThreadArbitrationPolicy = commandStreamReceiver.lastSentThreadArbitrationPolicy;
    auto policyNotChanged = commandStreamReceiver.getRequiredCmdSizeForPreamble(*pDevice);

    commandStreamReceiver.requiredThreadArbitrationPolicy = commandStreamReceiver.lastSentThreadArbitrationPolicy + 1;
    auto policyChanged = commandStreamReceiver.getRequiredCmdSizeForPreamble(*pDevice);

    auto actualDifference = policyChanged - policyNotChanged;
    auto expectedDifference = PreambleHelper<FamilyType>::getThreadArbitrationCommandsSize();
    EXPECT_EQ(expectedDifference, actualDifference);
}

HWTEST_F(UltCommandStreamReceiverTest, givenPreambleSentWhenEstimatingPreambleCmdSizeThenResultDependsOnPolicyProgrammingAndAdditionalCmdsSize) {
    auto &commandStreamReceiver = pDevice->getUltCommandStreamReceiver<FamilyType>();
    commandStreamReceiver.requiredThreadArbitrationPolicy = commandStreamReceiver.lastSentThreadArbitrationPolicy;

    commandStreamReceiver.isPreambleSent = false;
    auto preambleNotSent = commandStreamReceiver.getRequiredCmdSizeForPreamble(*pDevice);

    commandStreamReceiver.isPreambleSent = true;
    auto preambleSent = commandStreamReceiver.getRequiredCmdSizeForPreamble(*pDevice);

    auto actualDifference = preambleNotSent - preambleSent;
    auto expectedDifference = PreambleHelper<FamilyType>::getThreadArbitrationCommandsSize() + PreambleHelper<FamilyType>::getAdditionalCommandsSize(*pDevice);

    EXPECT_EQ(expectedDifference, actualDifference);
}

HWTEST_F(UltCommandStreamReceiverTest, givenPerDssBackBufferProgrammingEnabledWhenEstimatingPreambleCmdSizeThenResultIncludesPerDssBackBufferProgramingCommandsSize) {
    DebugManagerStateRestore restore;
    DebugManager.flags.ForcePerDssBackedBufferProgramming.set(true);

    auto &commandStreamReceiver = pDevice->getUltCommandStreamReceiver<FamilyType>();
    commandStreamReceiver.requiredThreadArbitrationPolicy = commandStreamReceiver.lastSentThreadArbitrationPolicy;

    commandStreamReceiver.isPreambleSent = false;
    auto preambleNotSent = commandStreamReceiver.getRequiredCmdSizeForPreamble(*pDevice);

    commandStreamReceiver.isPreambleSent = true;
    auto preambleSent = commandStreamReceiver.getRequiredCmdSizeForPreamble(*pDevice);

    auto actualDifference = preambleNotSent - preambleSent;
    auto expectedDifference = PreambleHelper<FamilyType>::getThreadArbitrationCommandsSize() + PreambleHelper<FamilyType>::getAdditionalCommandsSize(*pDevice) + PreambleHelper<FamilyType>::getPerDssBackedBufferCommandsSize(pDevice->getHardwareInfo());

    EXPECT_EQ(expectedDifference, actualDifference);
}

HWCMDTEST_F(IGFX_GEN8_CORE, UltCommandStreamReceiverTest, givenMediaVfeStateDirtyEstimatingPreambleCmdSizeThenResultDependsVfeStateProgrammingCmdSize) {
    typedef typename FamilyType::MEDIA_VFE_STATE MEDIA_VFE_STATE;
    typedef typename FamilyType::PIPE_CONTROL PIPE_CONTROL;

    auto &commandStreamReceiver = pDevice->getUltCommandStreamReceiver<FamilyType>();

    commandStreamReceiver.setMediaVFEStateDirty(false);
    auto notDirty = commandStreamReceiver.getRequiredCmdSizeForPreamble(*pDevice);

    commandStreamReceiver.setMediaVFEStateDirty(true);
    auto dirty = commandStreamReceiver.getRequiredCmdSizeForPreamble(*pDevice);

    auto actualDifference = dirty - notDirty;
    auto expectedDifference = sizeof(PIPE_CONTROL) + sizeof(MEDIA_VFE_STATE);
    EXPECT_EQ(expectedDifference, actualDifference);
}

HWTEST_F(UltCommandStreamReceiverTest, givenCommandStreamReceiverInInitialStateWhenHeapsAreAskedForDirtyStatusThenTrueIsReturned) {
    auto &commandStreamReceiver = pDevice->getUltCommandStreamReceiver<FamilyType>();

    EXPECT_EQ(0u, commandStreamReceiver.peekTaskCount());
    EXPECT_EQ(0u, commandStreamReceiver.peekTaskLevel());

    EXPECT_TRUE(commandStreamReceiver.dshState.updateAndCheck(&dsh));
    EXPECT_TRUE(commandStreamReceiver.iohState.updateAndCheck(&ioh));
    EXPECT_TRUE(commandStreamReceiver.sshState.updateAndCheck(&ssh));
}

HWTEST_F(UltCommandStreamReceiverTest, givenPreambleSentAndForceSemaphoreDelayBetweenWaitsFlagWhenEstimatingPreambleCmdSizeThenResultIsExpected) {
    auto &commandStreamReceiver = pDevice->getUltCommandStreamReceiver<FamilyType>();
    commandStreamReceiver.requiredThreadArbitrationPolicy = commandStreamReceiver.lastSentThreadArbitrationPolicy;
    DebugManagerStateRestore debugManagerStateRestore;

    DebugManager.flags.ForceSemaphoreDelayBetweenWaits.set(-1);
    commandStreamReceiver.isPreambleSent = false;

    auto preambleNotSentAndSemaphoreDelayNotReprogrammed = commandStreamReceiver.getRequiredCmdSizeForPreamble(*pDevice);

    DebugManager.flags.ForceSemaphoreDelayBetweenWaits.set(0);
    commandStreamReceiver.isPreambleSent = false;

    auto preambleNotSentAndSemaphoreDelayReprogrammed = commandStreamReceiver.getRequiredCmdSizeForPreamble(*pDevice);

    commandStreamReceiver.isPreambleSent = true;
    auto preambleSent = commandStreamReceiver.getRequiredCmdSizeForPreamble(*pDevice);

    auto actualDifferenceWhenSemaphoreDelayNotReprogrammed = preambleNotSentAndSemaphoreDelayNotReprogrammed - preambleSent;
    auto expectedDifference = PreambleHelper<FamilyType>::getThreadArbitrationCommandsSize() + PreambleHelper<FamilyType>::getAdditionalCommandsSize(*pDevice);

    EXPECT_EQ(expectedDifference, actualDifferenceWhenSemaphoreDelayNotReprogrammed);

    auto actualDifferenceWhenSemaphoreDelayReprogrammed = preambleNotSentAndSemaphoreDelayReprogrammed - preambleSent;
    expectedDifference = PreambleHelper<FamilyType>::getThreadArbitrationCommandsSize() + PreambleHelper<FamilyType>::getAdditionalCommandsSize(*pDevice) + PreambleHelper<FamilyType>::getSemaphoreDelayCommandSize();

    EXPECT_EQ(expectedDifference, actualDifferenceWhenSemaphoreDelayReprogrammed);
}

HWTEST_F(UltCommandStreamReceiverTest, givenNoBlitterOverrideWhenBlitterNotSupportedThenExpectFalseReturned) {
    auto &commandStreamReceiver = pDevice->getUltCommandStreamReceiver<FamilyType>();

    DirectSubmissionProperties properties;
    properties.engineSupported = false;
    properties.submitOnInit = false;
    bool startOnInit = true;
    EXPECT_FALSE(commandStreamReceiver.checkDirectSubmissionSupportsEngine(properties, aub_stream::ENGINE_BCS, startOnInit));
    EXPECT_FALSE(startOnInit);
}

HWTEST_F(UltCommandStreamReceiverTest, givenNoBlitterOverrideWhenBlitterSupportedThenExpectTrueReturned) {
    auto &commandStreamReceiver = pDevice->getUltCommandStreamReceiver<FamilyType>();

    DirectSubmissionProperties properties;
    properties.engineSupported = true;
    properties.submitOnInit = true;
    bool startOnInit = false;
    EXPECT_TRUE(commandStreamReceiver.checkDirectSubmissionSupportsEngine(properties, aub_stream::ENGINE_BCS, startOnInit));
    EXPECT_TRUE(startOnInit);
}

HWTEST_F(UltCommandStreamReceiverTest, givenBlitterOverrideEnableWhenBlitterNotSupportedThenExpectTrueReturned) {
    DebugManagerStateRestore debugManagerStateRestore;
    DebugManager.flags.DirectSubmissionOverrideBlitterSupport.set(1);
    auto &commandStreamReceiver = pDevice->getUltCommandStreamReceiver<FamilyType>();

    DirectSubmissionProperties properties;
    properties.engineSupported = false;
    properties.submitOnInit = false;
    bool startOnInit = false;
    EXPECT_TRUE(commandStreamReceiver.checkDirectSubmissionSupportsEngine(properties, aub_stream::ENGINE_BCS, startOnInit));
    EXPECT_TRUE(startOnInit);
}

HWTEST_F(UltCommandStreamReceiverTest, givenBlitterOverrideEnableAndNoStartWhenBlitterNotSupportedThenExpectTrueReturnedStartOnInitSetToTrue) {
    DebugManagerStateRestore debugManagerStateRestore;
    DebugManager.flags.DirectSubmissionOverrideBlitterSupport.set(2);
    auto &commandStreamReceiver = pDevice->getUltCommandStreamReceiver<FamilyType>();

    DirectSubmissionProperties properties;
    properties.engineSupported = false;
    properties.submitOnInit = true;
    bool startOnInit = true;
    EXPECT_TRUE(commandStreamReceiver.checkDirectSubmissionSupportsEngine(properties, aub_stream::ENGINE_BCS, startOnInit));
    EXPECT_FALSE(startOnInit);
}

HWTEST_F(UltCommandStreamReceiverTest, givenBlitterOverrideDisableWhenBlitterSupportedThenExpectFalseReturned) {
    DebugManagerStateRestore debugManagerStateRestore;
    DebugManager.flags.DirectSubmissionOverrideBlitterSupport.set(0);
    auto &commandStreamReceiver = pDevice->getUltCommandStreamReceiver<FamilyType>();

    DirectSubmissionProperties properties;
    properties.engineSupported = true;
    properties.submitOnInit = false;
    bool startOnInit = true;
    EXPECT_FALSE(commandStreamReceiver.checkDirectSubmissionSupportsEngine(properties, aub_stream::ENGINE_BCS, startOnInit));
    EXPECT_FALSE(startOnInit);
}

HWTEST_F(UltCommandStreamReceiverTest, givenNoRenderOverrideWhenRenderNotSupportedThenExpectFalseReturned) {
    auto &commandStreamReceiver = pDevice->getUltCommandStreamReceiver<FamilyType>();

    DirectSubmissionProperties properties;
    properties.engineSupported = false;
    properties.submitOnInit = false;
    bool startOnInit = true;
    EXPECT_FALSE(commandStreamReceiver.checkDirectSubmissionSupportsEngine(properties, aub_stream::ENGINE_RCS, startOnInit));
    EXPECT_FALSE(startOnInit);
}

HWTEST_F(UltCommandStreamReceiverTest, givenNoRenderOverrideWhenRenderSupportedThenExpectTrueReturned) {
    auto &commandStreamReceiver = pDevice->getUltCommandStreamReceiver<FamilyType>();

    DirectSubmissionProperties properties;
    properties.engineSupported = true;
    properties.submitOnInit = true;
    bool startOnInit = false;
    EXPECT_TRUE(commandStreamReceiver.checkDirectSubmissionSupportsEngine(properties, aub_stream::ENGINE_RCS, startOnInit));
    EXPECT_TRUE(startOnInit);
}

HWTEST_F(UltCommandStreamReceiverTest, givenRenderOverrideEnableWhenRenderNotSupportedThenExpectTrueReturned) {
    DebugManagerStateRestore debugManagerStateRestore;
    DebugManager.flags.DirectSubmissionOverrideRenderSupport.set(1);
    auto &commandStreamReceiver = pDevice->getUltCommandStreamReceiver<FamilyType>();

    DirectSubmissionProperties properties;
    properties.engineSupported = false;
    properties.submitOnInit = false;
    bool startOnInit = false;
    EXPECT_TRUE(commandStreamReceiver.checkDirectSubmissionSupportsEngine(properties, aub_stream::ENGINE_RCS, startOnInit));
    EXPECT_TRUE(startOnInit);
}

HWTEST_F(UltCommandStreamReceiverTest, givenRenderOverrideEnableAndNoStartWhenRenderNotSupportedThenExpectTrueReturnedAndStartOnInitSetFalse) {
    DebugManagerStateRestore debugManagerStateRestore;
    DebugManager.flags.DirectSubmissionOverrideRenderSupport.set(2);
    auto &commandStreamReceiver = pDevice->getUltCommandStreamReceiver<FamilyType>();

    DirectSubmissionProperties properties;
    properties.engineSupported = false;
    properties.submitOnInit = true;
    bool startOnInit = true;
    EXPECT_TRUE(commandStreamReceiver.checkDirectSubmissionSupportsEngine(properties, aub_stream::ENGINE_RCS, startOnInit));
    EXPECT_FALSE(startOnInit);
}

HWTEST_F(UltCommandStreamReceiverTest, givenRenderOverrideDisableWhenRenderSupportedThenExpectFalseReturned) {
    DebugManagerStateRestore debugManagerStateRestore;
    DebugManager.flags.DirectSubmissionOverrideRenderSupport.set(0);
    auto &commandStreamReceiver = pDevice->getUltCommandStreamReceiver<FamilyType>();

    DirectSubmissionProperties properties;
    properties.engineSupported = true;
    properties.submitOnInit = false;
    bool startOnInit = true;
    EXPECT_FALSE(commandStreamReceiver.checkDirectSubmissionSupportsEngine(properties, aub_stream::ENGINE_RCS, startOnInit));
    EXPECT_FALSE(startOnInit);
}

HWTEST_F(UltCommandStreamReceiverTest, givenNoComputeOverrideWhenComputeNotSupportedThenExpectFalseReturned) {
    auto &commandStreamReceiver = pDevice->getUltCommandStreamReceiver<FamilyType>();

    DirectSubmissionProperties properties;
    properties.engineSupported = false;
    properties.submitOnInit = false;
    bool startOnInit = true;
    EXPECT_FALSE(commandStreamReceiver.checkDirectSubmissionSupportsEngine(properties, aub_stream::ENGINE_CCS, startOnInit));
    EXPECT_FALSE(startOnInit);
}

HWTEST_F(UltCommandStreamReceiverTest, givenNoComputeOverrideWhenComputeSupportedThenExpectTrueReturned) {
    auto &commandStreamReceiver = pDevice->getUltCommandStreamReceiver<FamilyType>();

    DirectSubmissionProperties properties;
    properties.engineSupported = true;
    properties.submitOnInit = true;
    bool startOnInit = false;
    EXPECT_TRUE(commandStreamReceiver.checkDirectSubmissionSupportsEngine(properties, aub_stream::ENGINE_CCS, startOnInit));
    EXPECT_TRUE(startOnInit);
}

HWTEST_F(UltCommandStreamReceiverTest, givenComputeOverrideEnableWhenComputeNotSupportedThenExpectTrueReturned) {
    DebugManagerStateRestore debugManagerStateRestore;
    DebugManager.flags.DirectSubmissionOverrideComputeSupport.set(1);
    auto &commandStreamReceiver = pDevice->getUltCommandStreamReceiver<FamilyType>();

    DirectSubmissionProperties properties;
    properties.engineSupported = false;
    properties.submitOnInit = false;
    bool startOnInit = false;
    EXPECT_TRUE(commandStreamReceiver.checkDirectSubmissionSupportsEngine(properties, aub_stream::ENGINE_CCS, startOnInit));
    EXPECT_TRUE(startOnInit);
}

HWTEST_F(UltCommandStreamReceiverTest, givenComputeOverrideEnableAndNoStartWhenComputeNotSupportedThenExpectTrueReturnedAndStartOnInitSetToFalse) {
    DebugManagerStateRestore debugManagerStateRestore;
    DebugManager.flags.DirectSubmissionOverrideComputeSupport.set(2);
    auto &commandStreamReceiver = pDevice->getUltCommandStreamReceiver<FamilyType>();

    DirectSubmissionProperties properties;
    properties.engineSupported = false;
    properties.submitOnInit = true;
    bool startOnInit = true;
    EXPECT_TRUE(commandStreamReceiver.checkDirectSubmissionSupportsEngine(properties, aub_stream::ENGINE_CCS, startOnInit));
    EXPECT_FALSE(startOnInit);
}

HWTEST_F(UltCommandStreamReceiverTest, givenComputeOverrideDisableWhenComputeSupportedThenExpectFalseReturned) {
    DebugManagerStateRestore debugManagerStateRestore;
    DebugManager.flags.DirectSubmissionOverrideComputeSupport.set(0);
    auto &commandStreamReceiver = pDevice->getUltCommandStreamReceiver<FamilyType>();

    DirectSubmissionProperties properties;
    properties.engineSupported = true;
    properties.submitOnInit = false;
    bool startOnInit = true;
    EXPECT_FALSE(commandStreamReceiver.checkDirectSubmissionSupportsEngine(properties, aub_stream::ENGINE_CCS, startOnInit));
    EXPECT_FALSE(startOnInit);
}

typedef UltCommandStreamReceiverTest CommandStreamReceiverFlushTests;

HWTEST_F(CommandStreamReceiverFlushTests, WhenAddingBatchBufferEndThenBatchBufferEndIsAppendedCorrectly) {
    auto usedPrevious = commandStream.getUsed();

    CommandStreamReceiverHw<FamilyType>::addBatchBufferEnd(commandStream, nullptr);

    EXPECT_EQ(commandStream.getUsed(), usedPrevious + sizeof(typename FamilyType::MI_BATCH_BUFFER_END));

    auto batchBufferEnd = genCmdCast<typename FamilyType::MI_BATCH_BUFFER_END *>(
        ptrOffset(commandStream.getCpuBase(), usedPrevious));
    EXPECT_NE(nullptr, batchBufferEnd);
}

HWTEST_F(CommandStreamReceiverFlushTests, WhenAligningCommandStreamReceiverToCacheLineSizeThenItIsAlignedCorrectly) {
    commandStream.getSpace(sizeof(uint32_t));
    CommandStreamReceiverHw<FamilyType>::alignToCacheLine(commandStream);

    EXPECT_EQ(0u, commandStream.getUsed() % MemoryConstants::cacheLineSize);
}

typedef Test<ClDeviceFixture> CommandStreamReceiverHwTest;

HWTEST_F(CommandStreamReceiverHwTest, givenCsrHwWhenTypeIsCheckedThenCsrHwIsReturned) {
    auto csr = std::unique_ptr<CommandStreamReceiver>(CommandStreamReceiverHw<FamilyType>::create(*pDevice->executionEnvironment, pDevice->getRootDeviceIndex()));

    EXPECT_EQ(CommandStreamReceiverType::CSR_HW, csr->getType());
}

HWCMDTEST_F(IGFX_GEN8_CORE, CommandStreamReceiverHwTest, WhenCommandStreamReceiverHwIsCreatedThenDefaultSshSizeIs64KB) {
    auto &commandStreamReceiver = pDevice->getGpgpuCommandStreamReceiver();
    EXPECT_EQ(64 * KB, commandStreamReceiver.defaultSshSize);
}

HWTEST_F(CommandStreamReceiverHwTest, WhenScratchSpaceIsNotRequiredThenScratchAllocationIsNotCreated) {
    auto commandStreamReceiver = std::make_unique<MockCsrHw<FamilyType>>(*pDevice->executionEnvironment, pDevice->getRootDeviceIndex());
    auto scratchController = commandStreamReceiver->getScratchSpaceController();

    bool stateBaseAddressDirty = false;
    bool cfeStateDirty = false;
    scratchController->setRequiredScratchSpace(reinterpret_cast<void *>(0x2000), 0u, 0u, 0u, *pDevice->getDefaultEngine().osContext, stateBaseAddressDirty, cfeStateDirty);
    EXPECT_FALSE(cfeStateDirty);
    EXPECT_FALSE(stateBaseAddressDirty);
    EXPECT_EQ(nullptr, scratchController->getScratchSpaceAllocation());
    EXPECT_EQ(nullptr, scratchController->getPrivateScratchSpaceAllocation());
}

HWTEST_F(CommandStreamReceiverHwTest, WhenScratchSpaceIsRequiredThenCorrectAddressIsReturned) {
    auto commandStreamReceiver = std::make_unique<MockCsrHw<FamilyType>>(*pDevice->executionEnvironment, pDevice->getRootDeviceIndex());
    commandStreamReceiver->setupContext(*pDevice->getDefaultEngine().osContext);
    auto scratchController = commandStreamReceiver->getScratchSpaceController();

    bool cfeStateDirty = false;
    bool stateBaseAddressDirty = false;

    std::unique_ptr<void, std::function<decltype(alignedFree)>> surfaceHeap(alignedMalloc(0x1000, 0x1000), alignedFree);
    scratchController->setRequiredScratchSpace(surfaceHeap.get(), 0x1000u, 0u, 0u, *pDevice->getDefaultEngine().osContext, stateBaseAddressDirty, cfeStateDirty);

    uint64_t expectedScratchAddress = 0xAAABBBCCCDDD000ull;
    auto scratchAllocation = scratchController->getScratchSpaceAllocation();
    scratchAllocation->setCpuPtrAndGpuAddress(scratchAllocation->getUnderlyingBuffer(), expectedScratchAddress);
    EXPECT_TRUE(UnitTestHelper<FamilyType>::evaluateGshAddressForScratchSpace((scratchAllocation->getGpuAddress() - MemoryConstants::pageSize), scratchController->calculateNewGSH()));
}

HWTEST_F(CommandStreamReceiverHwTest, WhenScratchSpaceIsNotRequiredThenGshAddressZeroIsReturned) {
    auto commandStreamReceiver = std::make_unique<MockCsrHw<FamilyType>>(*pDevice->executionEnvironment, pDevice->getRootDeviceIndex());
    auto scratchController = commandStreamReceiver->getScratchSpaceController();

    EXPECT_EQ(nullptr, scratchController->getScratchSpaceAllocation());
    EXPECT_EQ(0u, scratchController->calculateNewGSH());
}

HWTEST_F(BcsTests, givenBltSizeWhenEstimatingCommandSizeThenAddAllRequiredCommands) {
    constexpr auto max2DBlitSize = BlitterConstants::maxBlitWidth * BlitterConstants::maxBlitHeight;
    constexpr auto cmdsSizePerBlit = sizeof(typename FamilyType::XY_COPY_BLT) + sizeof(typename FamilyType::MI_ARB_CHECK);
    size_t notAlignedBltSize = (3 * max2DBlitSize) + 1;
    size_t alignedBltSize = (3 * max2DBlitSize);
    uint32_t alignedNumberOfBlts = 3;
    uint32_t notAlignedNumberOfBlts = 4;

    auto expectedAlignedSize = cmdsSizePerBlit * alignedNumberOfBlts;
    auto expectedNotAlignedSize = cmdsSizePerBlit * notAlignedNumberOfBlts;
    auto alignedCopySize = Vec3<size_t>{alignedBltSize, 1, 1};
    auto notAlignedCopySize = Vec3<size_t>{notAlignedBltSize, 1, 1};

    auto alignedEstimatedSize = BlitCommandsHelper<FamilyType>::estimateBlitCommandsSize(
        alignedCopySize, csrDependencies, false, false, pClDevice->getRootDeviceEnvironment());
    auto notAlignedEstimatedSize = BlitCommandsHelper<FamilyType>::estimateBlitCommandsSize(
        notAlignedCopySize, csrDependencies, false, false, pClDevice->getRootDeviceEnvironment());

    EXPECT_EQ(expectedAlignedSize, alignedEstimatedSize);
    EXPECT_EQ(expectedNotAlignedSize, notAlignedEstimatedSize);
    EXPECT_FALSE(BlitCommandsHelper<FamilyType>::isCopyRegionPreferred(alignedCopySize, pClDevice->getRootDeviceEnvironment()));
    EXPECT_FALSE(BlitCommandsHelper<FamilyType>::isCopyRegionPreferred(notAlignedCopySize, pClDevice->getRootDeviceEnvironment()));
}

HWTEST_F(BcsTests, givenDebugCapabilityWhenEstimatingCommandSizeThenAddAllRequiredCommands) {
    constexpr auto max2DBlitSize = BlitterConstants::maxBlitWidth * BlitterConstants::maxBlitHeight;
    constexpr auto cmdsSizePerBlit = sizeof(typename FamilyType::XY_COPY_BLT) + sizeof(typename FamilyType::MI_ARB_CHECK);
    const size_t debugCommandsSize = (EncodeMiFlushDW<FamilyType>::getMiFlushDwCmdSizeForDataWrite() + EncodeSempahore<FamilyType>::getSizeMiSemaphoreWait()) * 2;

    constexpr uint32_t numberOfBlts = 3;
    constexpr size_t bltSize = (numberOfBlts * max2DBlitSize);

    auto expectedSize = (cmdsSizePerBlit * numberOfBlts) + debugCommandsSize + MemorySynchronizationCommands<FamilyType>::getSizeForAdditonalSynchronization(pDevice->getHardwareInfo()) +
                        EncodeMiFlushDW<FamilyType>::getMiFlushDwCmdSizeForDataWrite() + sizeof(typename FamilyType::MI_BATCH_BUFFER_END);
    expectedSize = alignUp(expectedSize, MemoryConstants::cacheLineSize);

    BlitProperties blitProperties;
    blitProperties.copySize = {bltSize, 1, 1};
    BlitPropertiesContainer blitPropertiesContainer;
    blitPropertiesContainer.push_back(blitProperties);

    auto estimatedSize = BlitCommandsHelper<FamilyType>::estimateBlitCommandsSize(
        blitPropertiesContainer, false, true, pClDevice->getRootDeviceEnvironment());

    EXPECT_EQ(expectedSize, estimatedSize);
    EXPECT_FALSE(BlitCommandsHelper<FamilyType>::isCopyRegionPreferred(blitProperties.copySize, pClDevice->getRootDeviceEnvironment()));
}

HWTEST_F(BcsTests, givenBltSizeWhenEstimatingCommandSizeForReadBufferRectThenAddAllRequiredCommands) {
    constexpr auto max2DBlitSize = BlitterConstants::maxBlitWidth * BlitterConstants::maxBlitHeight;
    constexpr auto cmdsSizePerBlit = sizeof(typename FamilyType::XY_COPY_BLT) + sizeof(typename FamilyType::MI_ARB_CHECK);
    Vec3<size_t> notAlignedBltSize = {(3 * max2DBlitSize) + 1, 4, 2};
    Vec3<size_t> alignedBltSize = {(3 * max2DBlitSize), 4, 2};
    size_t alignedNumberOfBlts = 3 * alignedBltSize.y * alignedBltSize.z;
    size_t notAlignedNumberOfBlts = 4 * notAlignedBltSize.y * notAlignedBltSize.z;

    auto expectedAlignedSize = cmdsSizePerBlit * alignedNumberOfBlts;
    auto expectedNotAlignedSize = cmdsSizePerBlit * notAlignedNumberOfBlts;

    auto alignedEstimatedSize = BlitCommandsHelper<FamilyType>::estimateBlitCommandsSize(
        alignedBltSize, csrDependencies, false, false, pClDevice->getRootDeviceEnvironment());
    auto notAlignedEstimatedSize = BlitCommandsHelper<FamilyType>::estimateBlitCommandsSize(
        notAlignedBltSize, csrDependencies, false, false, pClDevice->getRootDeviceEnvironment());

    EXPECT_EQ(expectedAlignedSize, alignedEstimatedSize);
    EXPECT_EQ(expectedNotAlignedSize, notAlignedEstimatedSize);
    EXPECT_FALSE(BlitCommandsHelper<FamilyType>::isCopyRegionPreferred(notAlignedBltSize, pClDevice->getRootDeviceEnvironment()));
    EXPECT_FALSE(BlitCommandsHelper<FamilyType>::isCopyRegionPreferred(alignedBltSize, pClDevice->getRootDeviceEnvironment()));
}

HWTEST_F(BcsTests, givenBltWithBigCopySizeWhenEstimatingCommandSizeForReadBufferRectThenAddAllRequiredCommands) {
    auto &rootDeviceEnvironment = pClDevice->getRootDeviceEnvironment();
    auto maxWidthToCopy = static_cast<size_t>(BlitCommandsHelper<FamilyType>::getMaxBlitWidth(rootDeviceEnvironment));
    auto maxHeightToCopy = static_cast<size_t>(BlitCommandsHelper<FamilyType>::getMaxBlitHeight(rootDeviceEnvironment));

    constexpr auto cmdsSizePerBlit = sizeof(typename FamilyType::XY_COPY_BLT) + sizeof(typename FamilyType::MI_ARB_CHECK);
    Vec3<size_t> alignedBltSize = {(3 * maxWidthToCopy), (4 * maxHeightToCopy), 2};
    Vec3<size_t> notAlignedBltSize = {(3 * maxWidthToCopy + 1), (4 * maxHeightToCopy), 2};

    EXPECT_TRUE(BlitCommandsHelper<FamilyType>::isCopyRegionPreferred(alignedBltSize, rootDeviceEnvironment));

    size_t alignedNumberOfBlts = (3 * 4 * alignedBltSize.z);
    size_t notAlignedNumberOfBlts = (4 * 4 * notAlignedBltSize.z);

    auto expectedAlignedSize = cmdsSizePerBlit * alignedNumberOfBlts;
    auto expectedNotAlignedSize = cmdsSizePerBlit * notAlignedNumberOfBlts;

    auto alignedEstimatedSize = BlitCommandsHelper<FamilyType>::estimateBlitCommandsSize(
        alignedBltSize, csrDependencies, false, false, rootDeviceEnvironment);
    auto notAlignedEstimatedSize = BlitCommandsHelper<FamilyType>::estimateBlitCommandsSize(
        notAlignedBltSize, csrDependencies, false, false, rootDeviceEnvironment);

    EXPECT_EQ(expectedAlignedSize, alignedEstimatedSize);
    EXPECT_EQ(expectedNotAlignedSize, notAlignedEstimatedSize);
    EXPECT_TRUE(BlitCommandsHelper<FamilyType>::isCopyRegionPreferred(notAlignedBltSize, rootDeviceEnvironment));
    EXPECT_TRUE(BlitCommandsHelper<FamilyType>::isCopyRegionPreferred(alignedBltSize, rootDeviceEnvironment));
}

HWTEST_F(BcsTests, WhenGetNumberOfBlitsIsCalledThenCorrectValuesAreReturned) {
    auto &rootDeviceEnvironment = pClDevice->getRootDeviceEnvironment();
    auto maxWidthToCopy = static_cast<size_t>(BlitCommandsHelper<FamilyType>::getMaxBlitWidth(rootDeviceEnvironment));
    auto maxHeightToCopy = static_cast<size_t>(BlitCommandsHelper<FamilyType>::getMaxBlitHeight(rootDeviceEnvironment));

    {
        Vec3<size_t> copySize = {maxWidthToCopy * maxHeightToCopy, 1, 3};
        size_t expectednBlitsCopyRegion = maxHeightToCopy * 3;
        size_t expectednBlitsCopyPerRow = 3;
        auto nBlitsCopyRegion = BlitCommandsHelper<FamilyType>::getNumberOfBlitsForCopyRegion(copySize, rootDeviceEnvironment);
        auto nBlitsCopyPerRow = BlitCommandsHelper<FamilyType>::getNumberOfBlitsForCopyPerRow(copySize, rootDeviceEnvironment);

        EXPECT_EQ(expectednBlitsCopyPerRow, nBlitsCopyPerRow);
        EXPECT_EQ(expectednBlitsCopyRegion, nBlitsCopyRegion);
        EXPECT_FALSE(BlitCommandsHelper<FamilyType>::isCopyRegionPreferred(copySize, rootDeviceEnvironment));
    }
    {
        Vec3<size_t> copySize = {2 * maxWidthToCopy, 16, 3};
        size_t expectednBlitsCopyRegion = 2 * 3;
        size_t expectednBlitsCopyPerRow = 16 * 3;
        auto nBlitsCopyRegion = BlitCommandsHelper<FamilyType>::getNumberOfBlitsForCopyRegion(copySize, rootDeviceEnvironment);
        auto nBlitsCopyPerRow = BlitCommandsHelper<FamilyType>::getNumberOfBlitsForCopyPerRow(copySize, rootDeviceEnvironment);

        EXPECT_EQ(expectednBlitsCopyPerRow, nBlitsCopyPerRow);
        EXPECT_EQ(expectednBlitsCopyRegion, nBlitsCopyRegion);
        EXPECT_TRUE(BlitCommandsHelper<FamilyType>::isCopyRegionPreferred(copySize, rootDeviceEnvironment));
    }
    {
        Vec3<size_t> copySize = {2 * maxWidthToCopy, 3 * maxHeightToCopy, 4};
        size_t expectednBlitsCopyRegion = 2 * 3 * 4;
        size_t expectednBlitsCopyPerRow = 3 * maxHeightToCopy * 4;
        auto nBlitsCopyRegion = BlitCommandsHelper<FamilyType>::getNumberOfBlitsForCopyRegion(copySize, rootDeviceEnvironment);
        auto nBlitsCopyPerRow = BlitCommandsHelper<FamilyType>::getNumberOfBlitsForCopyPerRow(copySize, rootDeviceEnvironment);

        EXPECT_EQ(expectednBlitsCopyPerRow, nBlitsCopyPerRow);
        EXPECT_EQ(expectednBlitsCopyRegion, nBlitsCopyRegion);
        EXPECT_TRUE(BlitCommandsHelper<FamilyType>::isCopyRegionPreferred(copySize, rootDeviceEnvironment));
    }
}

HWTEST_F(BcsTests, WhenGetNumberOfBlitsForCopyPerRowIsCalledThenCorrectValuesAreReturned) {
    auto &rootDeviceEnvironment = pClDevice->getRootDeviceEnvironment();
    auto maxWidthToCopy = static_cast<size_t>(BlitCommandsHelper<FamilyType>::getMaxBlitWidth(rootDeviceEnvironment));
    auto maxHeightToCopy = static_cast<size_t>(BlitCommandsHelper<FamilyType>::getMaxBlitHeight(rootDeviceEnvironment));
    {
        Vec3<size_t> copySize = {(maxWidthToCopy * maxHeightToCopy - 1), 1, 1};
        size_t expectednBlitsCopyPerRow = 2;
        auto nBlitsCopyPerRow = BlitCommandsHelper<FamilyType>::getNumberOfBlitsForCopyPerRow(copySize, rootDeviceEnvironment);
        EXPECT_EQ(expectednBlitsCopyPerRow, nBlitsCopyPerRow);
    }
    {
        Vec3<size_t> copySize = {(maxWidthToCopy * maxHeightToCopy), 1, 1};
        size_t expectednBlitsCopyPerRow = 1;
        auto nBlitsCopyPerRow = BlitCommandsHelper<FamilyType>::getNumberOfBlitsForCopyPerRow(copySize, rootDeviceEnvironment);
        EXPECT_EQ(expectednBlitsCopyPerRow, nBlitsCopyPerRow);
    }
    {
        Vec3<size_t> copySize = {(maxWidthToCopy * maxHeightToCopy + 1), 1, 1};
        size_t expectednBlitsCopyPerRow = 2;
        auto nBlitsCopyPerRow = BlitCommandsHelper<FamilyType>::getNumberOfBlitsForCopyPerRow(copySize, rootDeviceEnvironment);
        EXPECT_EQ(expectednBlitsCopyPerRow, nBlitsCopyPerRow);
    }
    {
        Vec3<size_t> copySize = {(maxWidthToCopy * maxHeightToCopy + maxWidthToCopy), 1, 1};
        size_t expectednBlitsCopyPerRow = 2;
        auto nBlitsCopyPerRow = BlitCommandsHelper<FamilyType>::getNumberOfBlitsForCopyPerRow(copySize, rootDeviceEnvironment);
        EXPECT_EQ(expectednBlitsCopyPerRow, nBlitsCopyPerRow);
    }
    {
        Vec3<size_t> copySize = {(maxWidthToCopy * maxHeightToCopy + maxWidthToCopy + 1), 1, 1};
        size_t expectednBlitsCopyPerRow = 3;
        auto nBlitsCopyPerRow = BlitCommandsHelper<FamilyType>::getNumberOfBlitsForCopyPerRow(copySize, rootDeviceEnvironment);
        EXPECT_EQ(expectednBlitsCopyPerRow, nBlitsCopyPerRow);
    }
    {
        Vec3<size_t> copySize = {(maxWidthToCopy * maxHeightToCopy + 2 * maxWidthToCopy), 1, 1};
        size_t expectednBlitsCopyPerRow = 2;
        auto nBlitsCopyPerRow = BlitCommandsHelper<FamilyType>::getNumberOfBlitsForCopyPerRow(copySize, rootDeviceEnvironment);
        EXPECT_EQ(expectednBlitsCopyPerRow, nBlitsCopyPerRow);
        EXPECT_FALSE(BlitCommandsHelper<FamilyType>::isCopyRegionPreferred(copySize, rootDeviceEnvironment));
    }
}

HWTEST_F(BcsTests, whenAskingForCmdSizeForMiFlushDwWithMemoryWriteThenReturnCorrectValue) {
    size_t waSize = EncodeMiFlushDW<FamilyType>::getMiFlushDwWaSize();
    size_t totalSize = EncodeMiFlushDW<FamilyType>::getMiFlushDwCmdSizeForDataWrite();
    constexpr size_t miFlushDwSize = sizeof(typename FamilyType::MI_FLUSH_DW);

    size_t additionalSize = UnitTestHelper<FamilyType>::additionalMiFlushDwRequired ? miFlushDwSize : 0;

    EXPECT_EQ(additionalSize, waSize);
    EXPECT_EQ(miFlushDwSize + additionalSize, totalSize);
}

HWTEST_F(BcsTests, givenBlitPropertiesContainerWhenExstimatingCommandsSizeThenCalculateForAllAttachedProperites) {
    const auto max2DBlitSize = BlitterConstants::maxBlitWidth * BlitterConstants::maxBlitHeight;
    const uint32_t numberOfBlts = 3;
    const size_t bltSize = (3 * max2DBlitSize);
    const uint32_t numberOfBlitOperations = 4;

    auto baseSize = EncodeMiFlushDW<FamilyType>::getMiFlushDwCmdSizeForDataWrite() + sizeof(typename FamilyType::MI_BATCH_BUFFER_END);
    constexpr size_t cmdsSizePerBlit = sizeof(typename FamilyType::XY_COPY_BLT) + sizeof(typename FamilyType::MI_ARB_CHECK);
    auto expectedBlitInstructionsSize = cmdsSizePerBlit * numberOfBlts;

    auto expectedAlignedSize = baseSize + MemorySynchronizationCommands<FamilyType>::getSizeForAdditonalSynchronization(pDevice->getHardwareInfo());

    BlitPropertiesContainer blitPropertiesContainer;
    for (uint32_t i = 0; i < numberOfBlitOperations; i++) {
        BlitProperties blitProperties;
        blitProperties.copySize = {bltSize, 1, 1};
        blitPropertiesContainer.push_back(blitProperties);

        expectedAlignedSize += expectedBlitInstructionsSize;
    }

    expectedAlignedSize = alignUp(expectedAlignedSize, MemoryConstants::cacheLineSize);

    auto alignedEstimatedSize = BlitCommandsHelper<FamilyType>::estimateBlitCommandsSize(
        blitPropertiesContainer, false, false, pClDevice->getRootDeviceEnvironment());

    EXPECT_EQ(expectedAlignedSize, alignedEstimatedSize);
}

HWTEST_F(BcsTests, givenBlitPropertiesContainerWhenExstimatingCommandsSizeForWriteReadBufferRectThenCalculateForAllAttachedProperites) {
    constexpr auto max2DBlitSize = BlitterConstants::maxBlitWidth * BlitterConstants::maxBlitHeight;
    const Vec3<size_t> bltSize = {(3 * max2DBlitSize), 4, 2};
    const size_t numberOfBlts = 3 * bltSize.y * bltSize.z;
    const size_t numberOfBlitOperations = 4 * bltSize.y * bltSize.z;
    const size_t cmdsSizePerBlit = sizeof(typename FamilyType::XY_COPY_BLT) + sizeof(typename FamilyType::MI_ARB_CHECK);

    auto baseSize = EncodeMiFlushDW<FamilyType>::getMiFlushDwCmdSizeForDataWrite() + sizeof(typename FamilyType::MI_BATCH_BUFFER_END);
    auto expectedBlitInstructionsSize = cmdsSizePerBlit * numberOfBlts;

    auto expectedAlignedSize = baseSize + MemorySynchronizationCommands<FamilyType>::getSizeForAdditonalSynchronization(pDevice->getHardwareInfo());

    BlitPropertiesContainer blitPropertiesContainer;
    for (uint32_t i = 0; i < numberOfBlitOperations; i++) {
        BlitProperties blitProperties;
        blitProperties.copySize = bltSize;
        blitPropertiesContainer.push_back(blitProperties);

        expectedAlignedSize += expectedBlitInstructionsSize;
    }

    expectedAlignedSize = alignUp(expectedAlignedSize, MemoryConstants::cacheLineSize);

    auto alignedEstimatedSize = BlitCommandsHelper<FamilyType>::estimateBlitCommandsSize(
        blitPropertiesContainer, false, false, pClDevice->getRootDeviceEnvironment());

    EXPECT_EQ(expectedAlignedSize, alignedEstimatedSize);
}

HWTEST_F(BcsTests, givenTimestampPacketWriteRequestWhenEstimatingSizeForCommandsThenAddMiFlushDw) {
    constexpr size_t expectedBaseSize = sizeof(typename FamilyType::XY_COPY_BLT) + sizeof(typename FamilyType::MI_ARB_CHECK);

    auto expectedSizeWithTimestampPacketWrite = expectedBaseSize + EncodeMiFlushDW<FamilyType>::getMiFlushDwCmdSizeForDataWrite();
    auto expectedSizeWithoutTimestampPacketWrite = expectedBaseSize;

    auto estimatedSizeWithTimestampPacketWrite = BlitCommandsHelper<FamilyType>::estimateBlitCommandsSize(
        {1, 1, 1}, csrDependencies, true, false, pClDevice->getRootDeviceEnvironment());
    auto estimatedSizeWithoutTimestampPacketWrite = BlitCommandsHelper<FamilyType>::estimateBlitCommandsSize(
        {1, 1, 1}, csrDependencies, false, false, pClDevice->getRootDeviceEnvironment());

    EXPECT_EQ(expectedSizeWithTimestampPacketWrite, estimatedSizeWithTimestampPacketWrite);
    EXPECT_EQ(expectedSizeWithoutTimestampPacketWrite, estimatedSizeWithoutTimestampPacketWrite);
}

HWTEST_F(BcsTests, givenBltSizeAndCsrDependenciesWhenEstimatingCommandSizeThenAddAllRequiredCommands) {
    uint32_t numberOfBlts = 1;
    size_t numberNodesPerContainer = 5;
    auto &csr = pDevice->getUltCommandStreamReceiver<FamilyType>();

    MockTimestampPacketContainer timestamp0(*csr.getTimestampPacketAllocator(), numberNodesPerContainer);
    MockTimestampPacketContainer timestamp1(*csr.getTimestampPacketAllocator(), numberNodesPerContainer);
    csrDependencies.push_back(&timestamp0);
    csrDependencies.push_back(&timestamp1);

    constexpr size_t cmdsSizePerBlit = sizeof(typename FamilyType::XY_COPY_BLT) + sizeof(typename FamilyType::MI_ARB_CHECK);
    size_t expectedSize = (cmdsSizePerBlit * numberOfBlts) +
                          TimestampPacketHelper::getRequiredCmdStreamSize<FamilyType>(csrDependencies);

    auto estimatedSize = BlitCommandsHelper<FamilyType>::estimateBlitCommandsSize(
        {1, 1, 1}, csrDependencies, false, false, pClDevice->getRootDeviceEnvironment());

    EXPECT_EQ(expectedSize, estimatedSize);
}

HWTEST_F(BcsTests, givenBltSizeWithLeftoverWhenDispatchedThenProgramAllRequiredCommands) {
    using MI_FLUSH_DW = typename FamilyType::MI_FLUSH_DW;
    using MI_SEMAPHORE_WAIT = typename FamilyType::MI_SEMAPHORE_WAIT;
    constexpr auto max2DBlitSize = BlitterConstants::maxBlitWidth * BlitterConstants::maxBlitHeight;

    auto &csr = pDevice->getUltCommandStreamReceiver<FamilyType>();
    static_cast<OsAgnosticMemoryManager *>(csr.getMemoryManager())->turnOnFakingBigAllocations();

    uint32_t bltLeftover = 17;
    size_t bltSize = (2 * max2DBlitSize) + bltLeftover;
    uint32_t numberOfBlts = 3;

    cl_int retVal = CL_SUCCESS;
    auto buffer = clUniquePtr<Buffer>(Buffer::create(context.get(), CL_MEM_READ_WRITE, static_cast<size_t>(bltSize), nullptr, retVal));
    void *hostPtr = reinterpret_cast<void *>(0x12340000);

    uint32_t newTaskCount = 19;
    csr.taskCount = newTaskCount - 1;
    EXPECT_EQ(0u, csr.recursiveLockCounter.load());
    auto blitProperties = BlitProperties::constructPropertiesForReadWriteBuffer(BlitterConstants::BlitDirection::HostPtrToBuffer,
                                                                                csr, buffer->getGraphicsAllocation(pDevice->getRootDeviceIndex()), nullptr, hostPtr,
                                                                                buffer->getGraphicsAllocation(pDevice->getRootDeviceIndex())->getGpuAddress(), 0,
                                                                                0, 0, {bltSize, 1, 1}, 0, 0, 0, 0);

    blitBuffer(&csr, blitProperties, true);
    EXPECT_EQ(newTaskCount, csr.taskCount);
    EXPECT_EQ(newTaskCount, csr.latestFlushedTaskCount);
    EXPECT_EQ(newTaskCount, csr.latestSentTaskCount);
    EXPECT_EQ(newTaskCount, csr.latestSentTaskCountValueDuringFlush);
    EXPECT_EQ(1u, csr.recursiveLockCounter.load());

    HardwareParse hwParser;
    hwParser.parseCommands<FamilyType>(csr.commandStream);
    auto &cmdList = hwParser.cmdList;

    auto cmdIterator = find<typename FamilyType::XY_COPY_BLT *>(hwParser.cmdList.begin(), hwParser.cmdList.end());
    ASSERT_NE(hwParser.cmdList.end(), cmdIterator);

    for (uint32_t i = 0; i < numberOfBlts; i++) {
        auto bltCmd = genCmdCast<typename FamilyType::XY_COPY_BLT *>(*(cmdIterator++));
        EXPECT_NE(nullptr, bltCmd);

        uint32_t expectedWidth = static_cast<uint32_t>(BlitterConstants::maxBlitWidth);
        uint32_t expectedHeight = static_cast<uint32_t>(BlitterConstants::maxBlitHeight);
        if (i == (numberOfBlts - 1)) {
            expectedWidth = bltLeftover;
            expectedHeight = 1;
        }
        EXPECT_EQ(expectedWidth, bltCmd->getTransferWidth());
        EXPECT_EQ(expectedHeight, bltCmd->getTransferHeight());
        EXPECT_EQ(expectedWidth, bltCmd->getDestinationPitch());
        EXPECT_EQ(expectedWidth, bltCmd->getSourcePitch());

        auto miArbCheckCmd = genCmdCast<typename FamilyType::MI_ARB_CHECK *>(*(cmdIterator++));
        EXPECT_NE(nullptr, miArbCheckCmd);
        EXPECT_TRUE(memcmp(&FamilyType::cmdInitArbCheck, miArbCheckCmd, sizeof(typename FamilyType::MI_ARB_CHECK)) == 0);
    }

    if (UnitTestHelper<FamilyType>::isAdditionalSynchronizationRequired(pDevice->getHardwareInfo())) {
        if (UnitTestHelper<FamilyType>::isAdditionalMiSemaphoreWaitRequired(pDevice->getHardwareInfo())) {
            auto miSemaphoreWaitCmd = genCmdCast<MI_SEMAPHORE_WAIT *>(*(cmdIterator++));
            EXPECT_NE(nullptr, miSemaphoreWaitCmd);
            EXPECT_TRUE(UnitTestHelper<FamilyType>::isAdditionalMiSemaphoreWait(*miSemaphoreWaitCmd));
        } else {
            cmdIterator++;
        }
    }

    auto miFlushCmd = genCmdCast<MI_FLUSH_DW *>(*(cmdIterator++));

    if (UnitTestHelper<FamilyType>::additionalMiFlushDwRequired) {
        uint64_t gpuAddress = 0x0;
        uint64_t immData = 0;

        EXPECT_NE(nullptr, miFlushCmd);
        EXPECT_EQ(MI_FLUSH_DW::POST_SYNC_OPERATION_NO_WRITE, miFlushCmd->getPostSyncOperation());
        EXPECT_EQ(gpuAddress, miFlushCmd->getDestinationAddress());
        EXPECT_EQ(immData, miFlushCmd->getImmediateData());

        miFlushCmd = genCmdCast<MI_FLUSH_DW *>(*(cmdIterator++));
    }

    EXPECT_NE(cmdIterator, cmdList.end());
    EXPECT_EQ(MI_FLUSH_DW::POST_SYNC_OPERATION_WRITE_IMMEDIATE_DATA_QWORD, miFlushCmd->getPostSyncOperation());
    EXPECT_EQ(csr.getTagAllocation()->getGpuAddress(), miFlushCmd->getDestinationAddress());
    EXPECT_EQ(newTaskCount, miFlushCmd->getImmediateData());

    if (UnitTestHelper<FamilyType>::isAdditionalSynchronizationRequired(pDevice->getHardwareInfo())) {
        if (UnitTestHelper<FamilyType>::isAdditionalMiSemaphoreWaitRequired(pDevice->getHardwareInfo())) {
            auto miSemaphoreWaitCmd = genCmdCast<MI_SEMAPHORE_WAIT *>(*(cmdIterator++));
            EXPECT_NE(nullptr, miSemaphoreWaitCmd);
            EXPECT_TRUE(UnitTestHelper<FamilyType>::isAdditionalMiSemaphoreWait(*miSemaphoreWaitCmd));
        } else {
            cmdIterator++;
        }
    }

    EXPECT_NE(nullptr, genCmdCast<typename FamilyType::MI_BATCH_BUFFER_END *>(*(cmdIterator++)));

    // padding
    while (cmdIterator != cmdList.end()) {
        EXPECT_NE(nullptr, genCmdCast<typename FamilyType::MI_NOOP *>(*(cmdIterator++)));
    }
}

struct BcsTestParam {
    Vec3<size_t> copySize;

    Vec3<size_t> hostPtrOffset;
    Vec3<size_t> copyOffset;

    size_t dstRowPitch;
    size_t dstSlicePitch;
    size_t srcRowPitch;
    size_t srcSlicePitch;
} BlitterProperties[] = {
    {{(2 * BlitterConstants::maxBlitWidth * BlitterConstants::maxBlitHeight) + 17, 1, 1},
     {0, 1, 1},
     {BlitterConstants::maxBlitWidth, 1, 1},
     (2 * BlitterConstants::maxBlitWidth * BlitterConstants::maxBlitHeight) + 17,
     (2 * BlitterConstants::maxBlitWidth * BlitterConstants::maxBlitHeight) + 17,
     (2 * BlitterConstants::maxBlitWidth * BlitterConstants::maxBlitHeight) + 17,
     (2 * BlitterConstants::maxBlitWidth * BlitterConstants::maxBlitHeight) + 17},
    {{(2 * BlitterConstants::maxBlitWidth * BlitterConstants::maxBlitHeight) + 17, 2, 1},
     {BlitterConstants::maxBlitWidth, 2, 2},
     {BlitterConstants::maxBlitWidth, 1, 1},
     0,
     ((2 * BlitterConstants::maxBlitWidth * BlitterConstants::maxBlitHeight) + 17) * 2,
     0,
     ((2 * BlitterConstants::maxBlitWidth * BlitterConstants::maxBlitHeight) + 17) * 2},
    {{(2 * BlitterConstants::maxBlitWidth * BlitterConstants::maxBlitHeight) + 17, 1, 3},
     {BlitterConstants::maxBlitWidth, 2, 2},
     {BlitterConstants::maxBlitWidth, 1, 1},
     0,
     ((2 * BlitterConstants::maxBlitWidth * BlitterConstants::maxBlitHeight) + 17) * 2,
     0,
     ((2 * BlitterConstants::maxBlitWidth * BlitterConstants::maxBlitHeight) + 17) * 2},
    {{(2 * BlitterConstants::maxBlitWidth * BlitterConstants::maxBlitHeight) + 17, 4, 2},
     {0, 0, 0},
     {0, 0, 0},
     (2 * BlitterConstants::maxBlitWidth * BlitterConstants::maxBlitHeight) + 17,
     ((2 * BlitterConstants::maxBlitWidth * BlitterConstants::maxBlitHeight) + 17) * 4,
     (2 * BlitterConstants::maxBlitWidth * BlitterConstants::maxBlitHeight) + 17,
     ((2 * BlitterConstants::maxBlitWidth * BlitterConstants::maxBlitHeight) + 17) * 4},
    {{(2 * BlitterConstants::maxBlitWidth * BlitterConstants::maxBlitHeight) + 17, 3, 2},
     {BlitterConstants::maxBlitWidth, 2, 2},
     {BlitterConstants::maxBlitWidth, 1, 1},
     ((2 * BlitterConstants::maxBlitWidth * BlitterConstants::maxBlitHeight) + 17) + 2,
     (((2 * BlitterConstants::maxBlitWidth * BlitterConstants::maxBlitHeight) + 17) * 3) + 2,
     ((2 * BlitterConstants::maxBlitWidth * BlitterConstants::maxBlitHeight) + 17) + 2,
     (((2 * BlitterConstants::maxBlitWidth * BlitterConstants::maxBlitHeight) + 17) * 3) + 2}};

template <typename ParamType>
struct BcsDetaliedTests : public BcsTests,
                          public ::testing::WithParamInterface<ParamType> {
    void SetUp() override {
        BcsTests::SetUp();
    }

    void TearDown() override {
        BcsTests::TearDown();
    }
};

using BcsDetaliedTestsWithParams = BcsDetaliedTests<std::tuple<BcsTestParam, BlitterConstants::BlitDirection>>;

HWTEST_P(BcsDetaliedTestsWithParams, givenBltSizeWithLeftoverWhenDispatchedThenProgramAddresseForWriteReadBufferRect) {
    auto &csr = pDevice->getUltCommandStreamReceiver<FamilyType>();
    static_cast<OsAgnosticMemoryManager *>(csr.getMemoryManager())->turnOnFakingBigAllocations();

    uint32_t bltLeftover = 17;
    Vec3<size_t> bltSize = std::get<0>(GetParam()).copySize;

    size_t numberOfBltsForSingleBltSizeProgramm = 3;
    size_t totalNumberOfBits = numberOfBltsForSingleBltSizeProgramm * bltSize.y * bltSize.z;

    cl_int retVal = CL_SUCCESS;
    auto buffer = clUniquePtr<Buffer>(Buffer::create(context.get(), CL_MEM_READ_WRITE, static_cast<size_t>(8 * BlitterConstants::maxBlitWidth * BlitterConstants::maxBlitHeight), nullptr, retVal));
    void *hostPtr = reinterpret_cast<void *>(0x12340000);

    Vec3<size_t> hostPtrOffset = std::get<0>(GetParam()).hostPtrOffset;
    Vec3<size_t> copyOffset = std::get<0>(GetParam()).copyOffset;

    size_t dstRowPitch = std::get<0>(GetParam()).dstRowPitch;
    size_t dstSlicePitch = std::get<0>(GetParam()).dstSlicePitch;
    size_t srcRowPitch = std::get<0>(GetParam()).srcRowPitch;
    size_t srcSlicePitch = std::get<0>(GetParam()).srcSlicePitch;
    auto allocation = buffer->getGraphicsAllocation(pDevice->getRootDeviceIndex());

    auto blitProperties = BlitProperties::constructPropertiesForReadWriteBuffer(std::get<1>(GetParam()),     //blitDirection
                                                                                csr, allocation,             //commandStreamReceiver
                                                                                nullptr,                     //memObjAllocation
                                                                                hostPtr,                     //preallocatedHostAllocation
                                                                                allocation->getGpuAddress(), //memObjGpuVa
                                                                                0,                           //hostAllocGpuVa
                                                                                hostPtrOffset,               //hostPtrOffset
                                                                                copyOffset,                  //copyOffset
                                                                                bltSize,                     //copySize
                                                                                dstRowPitch,                 //hostRowPitch
                                                                                dstSlicePitch,               //hostSlicePitch
                                                                                srcRowPitch,                 //gpuRowPitch
                                                                                srcSlicePitch                //gpuSlicePitch
    );
    blitBuffer(&csr, blitProperties, true);

    HardwareParse hwParser;
    hwParser.parseCommands<FamilyType>(csr.commandStream);

    auto cmdIterator = find<typename FamilyType::XY_COPY_BLT *>(hwParser.cmdList.begin(), hwParser.cmdList.end());
    ASSERT_NE(hwParser.cmdList.end(), cmdIterator);

    uint64_t offset = 0;
    for (uint32_t i = 0; i < totalNumberOfBits; i++) {
        auto bltCmd = genCmdCast<typename FamilyType::XY_COPY_BLT *>(*(cmdIterator++));
        EXPECT_NE(nullptr, bltCmd);

        uint32_t expectedWidth = static_cast<uint32_t>(BlitterConstants::maxBlitWidth);
        uint32_t expectedHeight = static_cast<uint32_t>(BlitterConstants::maxBlitHeight);
        if (i % numberOfBltsForSingleBltSizeProgramm == numberOfBltsForSingleBltSizeProgramm - 1) {
            expectedWidth = bltLeftover;
            expectedHeight = 1;
        }

        if (i % numberOfBltsForSingleBltSizeProgramm == 0) {
            offset = 0;
        }

        auto rowIndex = (i / numberOfBltsForSingleBltSizeProgramm) % blitProperties.copySize.y;
        auto sliceIndex = i / (numberOfBltsForSingleBltSizeProgramm * blitProperties.copySize.y);

        auto expectedDstAddr = blitProperties.dstGpuAddress + blitProperties.dstOffset.x + offset +
                               blitProperties.dstOffset.y * blitProperties.dstRowPitch +
                               blitProperties.dstOffset.z * blitProperties.dstSlicePitch +
                               rowIndex * blitProperties.dstRowPitch +
                               sliceIndex * blitProperties.dstSlicePitch;
        auto expectedSrcAddr = blitProperties.srcGpuAddress + blitProperties.srcOffset.x + offset +
                               blitProperties.srcOffset.y * blitProperties.srcRowPitch +
                               blitProperties.srcOffset.z * blitProperties.srcSlicePitch +
                               rowIndex * blitProperties.srcRowPitch +
                               sliceIndex * blitProperties.srcSlicePitch;

        auto dstAddr = NEO::BlitCommandsHelper<FamilyType>::calculateBlitCommandDestinationBaseAddress(blitProperties, offset, rowIndex, sliceIndex);
        auto srcAddr = NEO::BlitCommandsHelper<FamilyType>::calculateBlitCommandSourceBaseAddress(blitProperties, offset, rowIndex, sliceIndex);

        EXPECT_EQ(dstAddr, expectedDstAddr);
        EXPECT_EQ(srcAddr, expectedSrcAddr);

        offset += (expectedWidth * expectedHeight);

        auto miArbCheckCmd = genCmdCast<typename FamilyType::MI_ARB_CHECK *>(*(cmdIterator++));
        EXPECT_NE(nullptr, miArbCheckCmd);
        EXPECT_TRUE(memcmp(&FamilyType::cmdInitArbCheck, miArbCheckCmd, sizeof(typename FamilyType::MI_ARB_CHECK)) == 0);
    }
}

HWTEST_P(BcsDetaliedTestsWithParams, givenBltSizeWithLeftoverWhenDispatchedThenProgramAllRequiredCommandsForWriteReadBufferRect) {
    auto &csr = pDevice->getUltCommandStreamReceiver<FamilyType>();
    static_cast<OsAgnosticMemoryManager *>(csr.getMemoryManager())->turnOnFakingBigAllocations();

    uint32_t bltLeftover = 17;
    Vec3<size_t> bltSize = std::get<0>(GetParam()).copySize;

    size_t numberOfBltsForSingleBltSizeProgramm = 3;
    size_t totalNumberOfBits = numberOfBltsForSingleBltSizeProgramm * bltSize.y * bltSize.z;

    cl_int retVal = CL_SUCCESS;
    auto buffer = clUniquePtr<Buffer>(Buffer::create(context.get(), CL_MEM_READ_WRITE, static_cast<size_t>(8 * BlitterConstants::maxBlitWidth * BlitterConstants::maxBlitHeight), nullptr, retVal));
    void *hostPtr = reinterpret_cast<void *>(0x12340000);

    Vec3<size_t> hostPtrOffset = std::get<0>(GetParam()).hostPtrOffset;
    Vec3<size_t> copyOffset = std::get<0>(GetParam()).copyOffset;

    size_t dstRowPitch = std::get<0>(GetParam()).dstRowPitch;
    size_t dstSlicePitch = std::get<0>(GetParam()).dstSlicePitch;
    size_t srcRowPitch = std::get<0>(GetParam()).srcRowPitch;
    size_t srcSlicePitch = std::get<0>(GetParam()).srcSlicePitch;
    auto allocation = buffer->getGraphicsAllocation(pDevice->getRootDeviceIndex());

    auto blitProperties = BlitProperties::constructPropertiesForReadWriteBuffer(std::get<1>(GetParam()),     //blitDirection
                                                                                csr, allocation,             //commandStreamReceiver
                                                                                nullptr,                     //memObjAllocation
                                                                                hostPtr,                     //preallocatedHostAllocation
                                                                                allocation->getGpuAddress(), //memObjGpuVa
                                                                                0,                           //hostAllocGpuVa
                                                                                hostPtrOffset,               //hostPtrOffset
                                                                                copyOffset,                  //copyOffset
                                                                                bltSize,                     //copySize
                                                                                dstRowPitch,                 //hostRowPitch
                                                                                dstSlicePitch,               //hostSlicePitch
                                                                                srcRowPitch,                 //gpuRowPitch
                                                                                srcSlicePitch                //gpuSlicePitch
    );
    blitBuffer(&csr, blitProperties, true);

    HardwareParse hwParser;
    hwParser.parseCommands<FamilyType>(csr.commandStream);

    auto cmdIterator = find<typename FamilyType::XY_COPY_BLT *>(hwParser.cmdList.begin(), hwParser.cmdList.end());
    ASSERT_NE(hwParser.cmdList.end(), cmdIterator);

    uint64_t offset = 0;
    for (uint32_t i = 0; i < totalNumberOfBits; i++) {
        auto bltCmd = genCmdCast<typename FamilyType::XY_COPY_BLT *>(*(cmdIterator++));
        EXPECT_NE(nullptr, bltCmd);

        uint32_t expectedWidth = static_cast<uint32_t>(BlitterConstants::maxBlitWidth);
        uint32_t expectedHeight = static_cast<uint32_t>(BlitterConstants::maxBlitHeight);
        if (i % numberOfBltsForSingleBltSizeProgramm == numberOfBltsForSingleBltSizeProgramm - 1) {
            expectedWidth = bltLeftover;
            expectedHeight = 1;
        }

        if (i % numberOfBltsForSingleBltSizeProgramm == 0) {
            offset = 0;
        }

        EXPECT_EQ(expectedWidth, bltCmd->getTransferWidth());
        EXPECT_EQ(expectedHeight, bltCmd->getTransferHeight());
        EXPECT_EQ(expectedWidth, bltCmd->getDestinationPitch());
        EXPECT_EQ(expectedWidth, bltCmd->getSourcePitch());

        auto rowIndex = (i / numberOfBltsForSingleBltSizeProgramm) % blitProperties.copySize.y;
        auto sliceIndex = i / (numberOfBltsForSingleBltSizeProgramm * blitProperties.copySize.y);

        auto dstAddr = NEO::BlitCommandsHelper<FamilyType>::calculateBlitCommandDestinationBaseAddress(blitProperties, offset, rowIndex, sliceIndex);
        auto srcAddr = NEO::BlitCommandsHelper<FamilyType>::calculateBlitCommandSourceBaseAddress(blitProperties, offset, rowIndex, sliceIndex);

        EXPECT_EQ(dstAddr, bltCmd->getDestinationBaseAddress());
        EXPECT_EQ(srcAddr, bltCmd->getSourceBaseAddress());

        offset += (expectedWidth * expectedHeight);

        auto miArbCheckCmd = genCmdCast<typename FamilyType::MI_ARB_CHECK *>(*(cmdIterator++));
        EXPECT_NE(nullptr, miArbCheckCmd);
        EXPECT_TRUE(memcmp(&FamilyType::cmdInitArbCheck, miArbCheckCmd, sizeof(typename FamilyType::MI_ARB_CHECK)) == 0);
    }
}

HWTEST_P(BcsDetaliedTestsWithParams, givenBltSizeWithLeftoverWhenDispatchedThenProgramAllRequiredCommandsForCopyBufferRect) {
    auto &csr = pDevice->getUltCommandStreamReceiver<FamilyType>();
    static_cast<OsAgnosticMemoryManager *>(csr.getMemoryManager())->turnOnFakingBigAllocations();

    uint32_t bltLeftover = 17;
    Vec3<size_t> bltSize = std::get<0>(GetParam()).copySize;

    size_t numberOfBltsForSingleBltSizeProgramm = 3;
    size_t totalNumberOfBits = numberOfBltsForSingleBltSizeProgramm * bltSize.y * bltSize.z;

    cl_int retVal = CL_SUCCESS;
    auto buffer1 = clUniquePtr<Buffer>(Buffer::create(context.get(), CL_MEM_READ_WRITE, static_cast<size_t>(8 * BlitterConstants::maxBlitWidth * BlitterConstants::maxBlitHeight), nullptr, retVal));

    Vec3<size_t> buffer1Offset = std::get<0>(GetParam()).hostPtrOffset;
    Vec3<size_t> buffer2Offset = std::get<0>(GetParam()).copyOffset;

    size_t buffer1RowPitch = std::get<0>(GetParam()).dstRowPitch;
    size_t buffer1SlicePitch = std::get<0>(GetParam()).dstSlicePitch;
    size_t buffer2RowPitch = std::get<0>(GetParam()).srcRowPitch;
    size_t buffer2SlicePitch = std::get<0>(GetParam()).srcSlicePitch;
    auto allocation = buffer1->getGraphicsAllocation(pDevice->getRootDeviceIndex());

    auto blitProperties = BlitProperties::constructPropertiesForCopyBuffer(allocation,        //dstAllocation
                                                                           allocation,        //srcAllocation
                                                                           buffer1Offset,     //dstOffset
                                                                           buffer2Offset,     //srcOffset
                                                                           bltSize,           //copySize
                                                                           buffer1RowPitch,   //srcRowPitch
                                                                           buffer1SlicePitch, //srcSlicePitch
                                                                           buffer2RowPitch,   //dstRowPitch
                                                                           buffer2SlicePitch  //dstSlicePitch
    );
    blitBuffer(&csr, blitProperties, true);

    HardwareParse hwParser;
    hwParser.parseCommands<FamilyType>(csr.commandStream);

    auto cmdIterator = find<typename FamilyType::XY_COPY_BLT *>(hwParser.cmdList.begin(), hwParser.cmdList.end());
    ASSERT_NE(hwParser.cmdList.end(), cmdIterator);

    uint64_t offset = 0;
    for (uint32_t i = 0; i < totalNumberOfBits; i++) {
        auto bltCmd = genCmdCast<typename FamilyType::XY_COPY_BLT *>(*(cmdIterator++));
        EXPECT_NE(nullptr, bltCmd);

        uint32_t expectedWidth = static_cast<uint32_t>(BlitterConstants::maxBlitWidth);
        uint32_t expectedHeight = static_cast<uint32_t>(BlitterConstants::maxBlitHeight);
        if (i % numberOfBltsForSingleBltSizeProgramm == numberOfBltsForSingleBltSizeProgramm - 1) {
            expectedWidth = bltLeftover;
            expectedHeight = 1;
        }

        if (i % numberOfBltsForSingleBltSizeProgramm == 0) {
            offset = 0;
        }

        EXPECT_EQ(expectedWidth, bltCmd->getTransferWidth());
        EXPECT_EQ(expectedHeight, bltCmd->getTransferHeight());
        EXPECT_EQ(expectedWidth, bltCmd->getDestinationPitch());
        EXPECT_EQ(expectedWidth, bltCmd->getSourcePitch());

        auto rowIndex = (i / numberOfBltsForSingleBltSizeProgramm) % blitProperties.copySize.y;
        auto sliceIndex = i / (numberOfBltsForSingleBltSizeProgramm * blitProperties.copySize.y);

        auto dstAddr = NEO::BlitCommandsHelper<FamilyType>::calculateBlitCommandDestinationBaseAddress(blitProperties, offset, rowIndex, sliceIndex);
        auto srcAddr = NEO::BlitCommandsHelper<FamilyType>::calculateBlitCommandSourceBaseAddress(blitProperties, offset, rowIndex, sliceIndex);

        EXPECT_EQ(dstAddr, bltCmd->getDestinationBaseAddress());
        EXPECT_EQ(srcAddr, bltCmd->getSourceBaseAddress());

        offset += (expectedWidth * expectedHeight);

        auto miArbCheckCmd = genCmdCast<typename FamilyType::MI_ARB_CHECK *>(*(cmdIterator++));
        EXPECT_NE(nullptr, miArbCheckCmd);
        EXPECT_TRUE(memcmp(&FamilyType::cmdInitArbCheck, miArbCheckCmd, sizeof(typename FamilyType::MI_ARB_CHECK)) == 0);
    }
}

INSTANTIATE_TEST_CASE_P(BcsDetaliedTest,
                        BcsDetaliedTestsWithParams,
                        ::testing::Combine(
                            ::testing::ValuesIn(BlitterProperties),
                            ::testing::Values(BlitterConstants::BlitDirection::HostPtrToBuffer, BlitterConstants::BlitDirection::BufferToHostPtr)));