// Copyright (C) 2018-2023 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include "include/auto_unit_test.hpp"

using Config = std::map<std::string, std::string>;
using namespace ov::mock_auto_plugin;

using PriorityParams = std::tuple<unsigned int, std::string>; //{modelpriority, deviceUniquName}

using ConfigParams = std::tuple<
        std::string,                       // netPrecision
        bool,                              // enable device priority
        std::vector<PriorityParams>        // {{modelpriority, expect device unique_name}}
        >;
class KeyNetworkPriorityTest : public tests::AutoTest, public ::testing::TestWithParam<ConfigParams> {
public:
    std::vector<DeviceInformation>                  metaDevices;

public:
    static std::string getTestCaseName(testing::TestParamInfo<ConfigParams> obj) {
        std::string netPrecision;
        bool enableDevicePriority;
        std::vector<PriorityParams> PriorityConfigs;
        std::tie(netPrecision, enableDevicePriority, PriorityConfigs) = obj.param;
        std::ostringstream result;
        if (enableDevicePriority) {
            result << "_enableDevicePriority_true";
        } else {
            result << "_enableDevicePriority_false";
        }
        for (auto& item : PriorityConfigs) {
            result <<  "_priority_" << std::get<0>(item);
            result <<  "_return_" << std::get<1>(item);
        }
        result << "netPrecision_" << netPrecision;
        return result.str();
    }

    void TearDown() override {
        metaDevices.clear();
    }

    void SetUp() override {
        std::tie(netPrecision, enableDevicePriority, PriorityConfigs) = GetParam();
        sizeOfConfigs = static_cast<int>(PriorityConfigs.size());
        std::vector<std::string> gpuCability =  {"FP32", "FP16", "BATCHED_BLOB", "BIN"};
        ON_CALL(*core, get_property(HasSubstr("GPU"),
                StrEq(ov::device::capabilities.name()), _)).WillByDefault(RETURN_MOCK_VALUE(gpuCability));
        ON_CALL(*plugin, get_valid_device)
            .WillByDefault([this](const std::vector<DeviceInformation>& metaDevices, const std::string& netPrecision) {
               return plugin->Plugin::get_valid_device(metaDevices, netPrecision);
            });
    }

protected:
    std::string netPrecision;
    bool enableDevicePriority;
    std::vector<PriorityParams> PriorityConfigs;
    int sizeOfConfigs;
};

TEST_P(KeyNetworkPriorityTest, SelectDevice) {
    std::vector<DeviceInformation> resDevInfo;
    if (enableDevicePriority) {
        metaDevices = {{CommonTestUtils::DEVICE_CPU, {}, 2, "", "CPU_01", 0},
            {"GPU.0", {}, 2, "01", "iGPU_01", 1},
            {"GPU.1", {}, 2, "01", "dGPU_01", 2},
            {"MYRIAD", {}, 2, "01", "MYRIAD_01", 3},
            {CommonTestUtils::DEVICE_KEEMBAY, {}, 2, "01", "VPUX_01", 4}};
    } else {
        metaDevices = {{CommonTestUtils::DEVICE_CPU, {}, 2, "", "CPU_01", 0},
            {"GPU.0", {}, 2, "01", "iGPU_01", 0},
            {"GPU.1", {}, 2, "01", "dGPU_01", 0},
            {"MYRIAD", {}, 2, "01", "MYRIAD_01", 0},
            {CommonTestUtils::DEVICE_KEEMBAY, {}, 2, "01", "VPUX_01", 0}};
    }

    EXPECT_CALL(*plugin, select_device(_, _, _)).Times(sizeOfConfigs);
    EXPECT_CALL(*core, get_property(_, _, _)).Times(AtLeast(sizeOfConfigs * 4));

    for (auto& item : PriorityConfigs) {
        resDevInfo.push_back(plugin->select_device(metaDevices, netPrecision, std::get<0>(item)));
    }
    for (int i = 0; i < sizeOfConfigs; i++) {
        EXPECT_EQ(resDevInfo[i].unique_name, std::get<1>(PriorityConfigs[i]));
        plugin->unregister_priority(std::get<0>(PriorityConfigs[i]), std::get<1>(PriorityConfigs[i]));
    }
}

TEST_P(KeyNetworkPriorityTest, MultiThreadsSelectDevice) {
    std::vector<DeviceInformation> resDevInfo;
    std::vector<std::future<void>> futureVect;
    if (enableDevicePriority) {
        metaDevices = {{CommonTestUtils::DEVICE_CPU, {}, 2, "", "CPU_01", 0},
            {"GPU.0", {}, 2, "01", "iGPU_01", 1},
            {"GPU.1", {}, 2, "01", "dGPU_01", 2},
            {"MYRIAD", {}, 2, "01", "MYRIAD_01", 3},
            {CommonTestUtils::DEVICE_KEEMBAY, {}, 2, "01", "VPUX_01", 4}};
    } else {
        metaDevices = {{CommonTestUtils::DEVICE_CPU, {}, 2, "", "CPU_01", 0},
            {"GPU.0", {}, 2, "01", "iGPU_01", 0},
            {"GPU.1", {}, 2, "01", "dGPU_01", 0},
            {"MYRIAD", {}, 2, "01", "MYRIAD_01", 0},
            {CommonTestUtils::DEVICE_KEEMBAY, {}, 2, "01", "VPUX_01", 0}};
    }

    EXPECT_CALL(*plugin, select_device(_, _, _)).Times(sizeOfConfigs * 2);
    EXPECT_CALL(*core, get_property(_, _, _)).Times(AtLeast(sizeOfConfigs * 4 * 2));
    // selectdevice in multi threads, and UnregisterPriority them all, should not affect the
    // Priority Map
    for (auto& item : PriorityConfigs) {
       unsigned int priority = std::get<0>(item);
       auto future = std::async(std::launch::async, [this, priority] {
               auto deviceInfo = plugin->select_device(metaDevices, netPrecision, priority);
               plugin->unregister_priority(priority, deviceInfo.unique_name);
               });
       futureVect.push_back(std::move(future));
    }

    for (auto& item : futureVect) {
           item.get();
    }

    for (auto& item : PriorityConfigs) {
        resDevInfo.push_back(plugin->select_device(metaDevices, netPrecision, std::get<0>(item)));
    }
    for (int i = 0; i < sizeOfConfigs; i++) {
        EXPECT_EQ(resDevInfo[i].unique_name, std::get<1>(PriorityConfigs[i]));
        plugin->unregister_priority(std::get<0>(PriorityConfigs[i]), std::get<1>(PriorityConfigs[i]));
    }
}

// ConfigParams details
// example
// ConfigParams {"FP32", false, {PriorityParams {0, "dGPU_01"},
//                        PriorityParams {1, "iGPU_01"},
//                        PriorityParams {2, "MYRIAD_01"},
//                        PriorityParams {2, "MYRIAD_01"}}},
//              {netPrecision, enableDevicePriority,  PriorityParamsVector{{modelpriority, expect device unique_name}}}

const std::vector<ConfigParams> testConfigs = {
    ConfigParams {"FP32", false, {PriorityParams {0, "dGPU_01"},
        PriorityParams {1, "iGPU_01"},
        PriorityParams {2, "CPU_01"},
        PriorityParams {2, "CPU_01"}}},
    ConfigParams {"FP32", false, {PriorityParams {2, "dGPU_01"},
        PriorityParams {3, "iGPU_01"},
        PriorityParams {4, "CPU_01"},
        PriorityParams {5, "MYRIAD_01"}}},
    ConfigParams {"FP32", false, {PriorityParams {2, "dGPU_01"},
        PriorityParams {0, "dGPU_01"},
        PriorityParams {2, "iGPU_01"},
        PriorityParams {2, "iGPU_01"}}},
    ConfigParams {"FP32", false, {PriorityParams {2, "dGPU_01"},
        PriorityParams {0, "dGPU_01"},
        PriorityParams {2, "iGPU_01"},
        PriorityParams {3, "CPU_01"}}},
    ConfigParams {"FP32", false, {PriorityParams {0, "dGPU_01"},
        PriorityParams {1, "iGPU_01"},
        PriorityParams {2, "CPU_01"},
        PriorityParams {3, "MYRIAD_01"},
        PriorityParams {0, "dGPU_01"},
        PriorityParams {1, "iGPU_01"},
        PriorityParams {2, "CPU_01"},
        PriorityParams {3, "MYRIAD_01"}}},
    ConfigParams {"INT8", false, {PriorityParams {0, "VPUX_01"},
        PriorityParams {1, "CPU_01"},
        PriorityParams {2, "CPU_01"},
        PriorityParams {2, "CPU_01"}}},
    ConfigParams {"INT8", false, {PriorityParams {2, "VPUX_01"},
        PriorityParams {3, "CPU_01"},
        PriorityParams {4, "CPU_01"},
        PriorityParams {5, "CPU_01"}}},
    ConfigParams {"INT8", false, {PriorityParams {2, "VPUX_01"},
        PriorityParams {0, "VPUX_01"},
        PriorityParams {2, "CPU_01"},
        PriorityParams {2, "CPU_01"}}},
    ConfigParams {"INT8", false, {PriorityParams {2, "VPUX_01"},
        PriorityParams {0, "VPUX_01"},
        PriorityParams {2, "CPU_01"},
        PriorityParams {3, "CPU_01"}}},
    ConfigParams {"INT8", false, {PriorityParams {0, "VPUX_01"},
        PriorityParams {1, "CPU_01"},
        PriorityParams {2, "CPU_01"},
        PriorityParams {3, "CPU_01"},
        PriorityParams {0, "VPUX_01"},
        PriorityParams {1, "CPU_01"},
        PriorityParams {2, "CPU_01"},
        PriorityParams {3, "CPU_01"}}},
    ConfigParams {"BIN", false, {PriorityParams {0, "dGPU_01"},
        PriorityParams {1, "iGPU_01"},
        PriorityParams {2, "CPU_01"},
        PriorityParams {2, "CPU_01"}}},
    ConfigParams {"BIN", false, {PriorityParams {2, "dGPU_01"},
        PriorityParams {3, "iGPU_01"},
        PriorityParams {4, "CPU_01"},
        PriorityParams {5, "CPU_01"}}},
    ConfigParams {"BIN", false, {PriorityParams {2, "dGPU_01"},
        PriorityParams {0, "dGPU_01"},
        PriorityParams {2, "iGPU_01"},
        PriorityParams {2, "iGPU_01"}}},
    ConfigParams {"BIN", false, {PriorityParams {2, "dGPU_01"},
        PriorityParams {0, "dGPU_01"},
        PriorityParams {2, "iGPU_01"},
        PriorityParams {3, "CPU_01"}}},
    ConfigParams {"BIN", false, {PriorityParams {0, "dGPU_01"},
        PriorityParams {1, "iGPU_01"},
        PriorityParams {2, "CPU_01"},
        PriorityParams {3, "CPU_01"},
        PriorityParams {0, "dGPU_01"},
        PriorityParams {1, "iGPU_01"},
        PriorityParams {2, "CPU_01"},
        PriorityParams {3, "CPU_01"}}},
    // metaDevices = {{CommonTestUtils::DEVICE_CPU, {}, 2, "", "CPU_01", 0},
    // {CommonTestUtils::DEVICE_GPU, {}, 2, "01", "iGPU_01", 1},
    // {CommonTestUtils::DEVICE_GPU, {}, 2, "01", "dGPU_01", 2},
    // {"MYRIAD", {}, 2, "01", "MYRIAD_01", 3},
    // {CommonTestUtils::DEVICE_KEEMBAY, {}, 2, "01", "VPUX_01", 4}};
    // cpu > igpu > dgpu > MYRIAD > VPUX
    ConfigParams {"FP32", true, {PriorityParams {0, "CPU_01"},
        PriorityParams {1, "iGPU_01"},
        PriorityParams {2, "dGPU_01"},
        PriorityParams {2, "dGPU_01"}}},
    ConfigParams {"FP32", true, {PriorityParams {2, "CPU_01"},
        PriorityParams {3, "iGPU_01"},
        PriorityParams {4, "dGPU_01"},
        PriorityParams {5, "MYRIAD_01"}}},
    ConfigParams {"FP32", true, {PriorityParams {2, "CPU_01"},
        PriorityParams {0, "CPU_01"},
        PriorityParams {2, "iGPU_01"},
        PriorityParams {2, "iGPU_01"}}},
    ConfigParams {"FP32", true, {PriorityParams {2, "CPU_01"},
        PriorityParams {0, "CPU_01"},
        PriorityParams {2, "iGPU_01"},
        PriorityParams {3, "dGPU_01"}}},
    ConfigParams {"FP32", true, {PriorityParams {0, "CPU_01"},
        PriorityParams {1, "iGPU_01"},
        PriorityParams {2, "dGPU_01"},
        PriorityParams {3, "MYRIAD_01"},
        PriorityParams {0, "CPU_01"},
        PriorityParams {1, "iGPU_01"},
        PriorityParams {2, "dGPU_01"},
        PriorityParams {3, "MYRIAD_01"}}},
    ConfigParams {"INT8", true, {PriorityParams {0, "CPU_01"},
        PriorityParams {1, "VPUX_01"},
        PriorityParams {2, "VPUX_01"},
        PriorityParams {2, "VPUX_01"}}},
    ConfigParams {"INT8", true, {PriorityParams {2, "CPU_01"},
        PriorityParams {3, "VPUX_01"},
        PriorityParams {4, "VPUX_01"},
        PriorityParams {5, "VPUX_01"}}},
    ConfigParams {"INT8", true, {PriorityParams {2, "CPU_01"},
        PriorityParams {0, "CPU_01"},
        PriorityParams {2, "VPUX_01"},
        PriorityParams {2, "VPUX_01"}}},
    ConfigParams {"INT8", true, {PriorityParams {2, "CPU_01"},
        PriorityParams {0, "CPU_01"},
        PriorityParams {2, "VPUX_01"},
        PriorityParams {3, "VPUX_01"}}},
    ConfigParams {"INT8", true, {PriorityParams {0, "CPU_01"},
        PriorityParams {1, "VPUX_01"},
        PriorityParams {2, "VPUX_01"},
        PriorityParams {3, "VPUX_01"},
        PriorityParams {0, "CPU_01"},
        PriorityParams {1, "VPUX_01"},
        PriorityParams {2, "VPUX_01"},
        PriorityParams {3, "VPUX_01"}}},
    ConfigParams {"BIN", true, {PriorityParams {0, "CPU_01"},
        PriorityParams {1, "iGPU_01"},
        PriorityParams {2, "dGPU_01"},
        PriorityParams {2, "dGPU_01"}}},
    ConfigParams {"BIN", true, {PriorityParams {2, "CPU_01"},
        PriorityParams {3, "iGPU_01"},
        PriorityParams {4, "dGPU_01"},
        PriorityParams {5, "dGPU_01"}}},
    ConfigParams {"BIN", true, {PriorityParams {2, "CPU_01"},
        PriorityParams {0, "CPU_01"},
        PriorityParams {2, "iGPU_01"},
        PriorityParams {2, "iGPU_01"}}},
    ConfigParams {"BIN", true, {PriorityParams {2, "CPU_01"},
        PriorityParams {0, "CPU_01"},
        PriorityParams {2, "iGPU_01"},
        PriorityParams {3, "dGPU_01"}}},
    ConfigParams {"BIN", true, {PriorityParams {0, "CPU_01"},
        PriorityParams {1, "iGPU_01"},
        PriorityParams {2, "dGPU_01"},
        PriorityParams {3, "dGPU_01"},
        PriorityParams {0, "CPU_01"},
        PriorityParams {1, "iGPU_01"},
        PriorityParams {2, "dGPU_01"},
        PriorityParams {3, "dGPU_01"}}}
};


INSTANTIATE_TEST_SUITE_P(smoke_Auto_BehaviorTests, KeyNetworkPriorityTest,
                ::testing::ValuesIn(testConfigs),
            KeyNetworkPriorityTest::getTestCaseName);
