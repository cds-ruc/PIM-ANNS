#pragma once

#include "../common/common.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>
#include <string>

class mConfig
{
private:
    int m_MAX_CLUSTER;
    bool m_ENABLE_DYNAMIC;
    std::string m_RESULT_DIR;
    std::string m_INDEX_PATH;
    std::string m_QUERY_PATH;
    std::string m_HISTORY_QUERY_PATH;
    std::string m_GROUNDTRUTH_PATH;
    std::string m_PROJECT_SOURCE_DIR;

    // below is new path member variables
    std::string m_REPLICA_PATH;
    std::string m_WORKLOAD_PATH;
    std::string m_FREQ_PATH;
    std::string m_SIZE_PATH;
    std::string m_DPU_ACTIVE_NUM_PATH;
    std::string m_BATCH_DPU_ACTIVE_NUM_PATH;
    std::string m_CPU_TIME_PATH;
    std::string m_DPU_TIME_PATH;
    std::string m_BATCH_DPU_TIME_PATH;
    std::string m_CPU_RESULT_PATH;
    std::string m_DPU_RESULT_PATH;
    std::string m_BATCH_DPU_RESULT_PATH;
    std::string m_CPU_DETAIL_PATH;
    std::string m_DPU_DETAIL_PATH;
    std::string m_BATCH_DPU_DETAIL_PATH;


    // parse config from json
    void parseConfig(const nlohmann::json &config)
    {
        m_MAX_CLUSTER = config["MAX_CLUSTER"].get<int>();
        m_RESULT_DIR = config["RESULT_DIR"].get<std::string>();
        m_INDEX_PATH = config["INDEX_PATH"].get<std::string>();
        m_QUERY_PATH = config["QUERY_PATH"].get<std::string>();
        m_HISTORY_QUERY_PATH = config["HISTORY_QUERY_PATH"].get<std::string>();
        m_GROUNDTRUTH_PATH = config["GROUNDTRUTH_PATH"].get<std::string>();

      
        // path of the new member variables
        m_PROJECT_SOURCE_DIR = std::string(PROJECT_SOURCE_DIR);
        m_REPLICA_PATH = m_PROJECT_SOURCE_DIR + "/" + m_RESULT_DIR + "/cpynum";
        m_WORKLOAD_PATH = m_PROJECT_SOURCE_DIR + "/" + m_RESULT_DIR + "/workload";
        m_FREQ_PATH = m_PROJECT_SOURCE_DIR + "/" + m_RESULT_DIR + "/freq";
        m_SIZE_PATH = m_PROJECT_SOURCE_DIR + "/" + m_RESULT_DIR + "/size";
        m_DPU_ACTIVE_NUM_PATH = m_PROJECT_SOURCE_DIR + "/" + m_RESULT_DIR + "/DPU_DIR/dpu-active-num";
        m_BATCH_DPU_ACTIVE_NUM_PATH = m_PROJECT_SOURCE_DIR + "/" + m_RESULT_DIR + "/BATCH_DPU_DIR/batch-dpu-active-num";
        m_CPU_TIME_PATH = m_PROJECT_SOURCE_DIR + "/" + m_RESULT_DIR + "/CPU_DIR/cpu-time";
        m_DPU_TIME_PATH = m_PROJECT_SOURCE_DIR + "/" + m_RESULT_DIR + "/DPU_DIR/dpu-time";
        m_BATCH_DPU_TIME_PATH = m_PROJECT_SOURCE_DIR + "/" + m_RESULT_DIR + "/BATCH_DPU_DIR/batch-dpu-time";
        m_CPU_RESULT_PATH = m_PROJECT_SOURCE_DIR + "/" + m_RESULT_DIR + "/CPU_DIR/cpu-result";
        m_DPU_RESULT_PATH = m_PROJECT_SOURCE_DIR + "/" + m_RESULT_DIR + "/DPU_DIR/dpu-result";
        m_BATCH_DPU_RESULT_PATH = m_PROJECT_SOURCE_DIR + "/" + m_RESULT_DIR + "/BATCH_DPU_DIR/batch-dpu-result";
        m_CPU_DETAIL_PATH = m_PROJECT_SOURCE_DIR + "/" + m_RESULT_DIR + "/CPU_DIR/cpu-detail";
        m_DPU_DETAIL_PATH = m_PROJECT_SOURCE_DIR + "/" + m_RESULT_DIR + "/DPU_DIR/dpu-detail";
        m_BATCH_DPU_DETAIL_PATH = m_PROJECT_SOURCE_DIR + "/" + m_RESULT_DIR + "/BATCH_DPU_DIR/batch-dpu-detail";

        if(DYNAMIC_BALANCE == 1)
        {
            m_ENABLE_DYNAMIC = true;
        }
        else
        {
            m_ENABLE_DYNAMIC = false;
        }

    }

public:
    mConfig(const std::string &filePath)
    {
        std::ifstream file(filePath);
        if (!file.is_open())
        {
            throw std::runtime_error("Failed to open config file");
        }
        nlohmann::json config;
        file >> config;
        parseConfig(config);
    }

    
    static mConfig from_json(const std::string &filePath)
    {
        std::ifstream file(filePath);
        if (!file.is_open())
        {
            throw std::runtime_error("Failed to open config file");
        }
        nlohmann::json config;
        file >> config;
        mConfig cfg("", false); 
        cfg.parseConfig(config);
        return cfg;
    }

   
    mConfig(const std::string &dummy, bool unused) {}

    mConfig() {}

   
    int getMaxCluster() const { return m_MAX_CLUSTER; }

    bool getEnableDynamic() const { return m_ENABLE_DYNAMIC; }
    const std::string &getReplaceDir() const { return m_RESULT_DIR; }
    const std::string &getIndexPath() const { return m_INDEX_PATH; }
    const std::string &getQueryPath() const { return m_QUERY_PATH; }

    const std::string &getHistoryQueryPath() const { return m_HISTORY_QUERY_PATH; }
    const std::string &getGroundTruthPath() const { return m_GROUNDTRUTH_PATH; }
    const std::string &getProjectSourceDir() const { return m_PROJECT_SOURCE_DIR; }


    const std::string &getReplicaPath() const { return m_REPLICA_PATH; }
    const std::string &getWorkloadPath() const { return m_WORKLOAD_PATH; }
    const std::string &getFreqPath() const { return m_FREQ_PATH; }
    const std::string &getSizePath() const { return m_SIZE_PATH; }
    const std::string &getDpuActiveNumPath() const { return m_DPU_ACTIVE_NUM_PATH; }
    const std::string &getBatchDpuActiveNumPath() const { return m_BATCH_DPU_ACTIVE_NUM_PATH; }
    const std::string &getCpuTimePath() const { return m_CPU_TIME_PATH; }
    const std::string &getDpuTimePath() const { return m_DPU_TIME_PATH; }
    const std::string &getBatchDpuTimePath() const { return m_BATCH_DPU_TIME_PATH; }
    const std::string &getCpuResultPath() const { return m_CPU_RESULT_PATH; }
    const std::string &getDpuResultPath() const { return m_DPU_RESULT_PATH; }
    const std::string &getBatchDpuResultPath() const { return m_BATCH_DPU_RESULT_PATH; }
    const std::string &getCpuDetailPath() const { return m_CPU_DETAIL_PATH; }
    const std::string &getDpuDetailPath() const { return m_DPU_DETAIL_PATH; }
    const std::string &getBatchDpuDetailPath() const { return m_BATCH_DPU_DETAIL_PATH; }
};