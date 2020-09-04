// Copyright (C) 2019-2020 Zilliz. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance
// with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations under the License.
#ifdef MILVUS_GPU_VERSION
#include "scheduler/selector/FaissIVFPQPass.h"
#include "cache/GpuCacheMgr.h"
#include "config/Config.h"
#include "knowhere/index/vector_index/helpers/IndexParameter.h"
#include "scheduler/SchedInst.h"
#include "scheduler/Utils.h"
#include "scheduler/task/SearchTask.h"
#include "scheduler/tasklabel/SpecResLabel.h"
#include "utils/Log.h"
#include "utils/ValidationUtil.h"

#include <fiu-local.h>

namespace milvus {
namespace scheduler {

void
FaissIVFPQPass::Init() {
#ifdef MILVUS_GPU_VERSION
    server::Config& config = server::Config::GetInstance();
    Status s = config.GetGpuResourceConfigGpuSearchThreshold(threshold_);
    if (!s.ok()) {
        threshold_ = std::numeric_limits<int32_t>::max();
    }
    s = config.GetGpuResourceConfigSearchResources(search_gpus_);
    if (!s.ok()) {
        throw std::exception();
    }

    SetIdentity("FaissIVFPQPass");
    AddGpuEnableListener();
    AddGpuSearchThresholdListener();
    AddGpuSearchResourcesListener();
#endif
}

bool
FaissIVFPQPass::Run(const TaskPtr& task) {
    if (task->Type() != TaskType::SearchTask) {
        return false;
    }

    auto search_task = std::static_pointer_cast<XSearchTask>(task);
    if (search_task->file_->engine_type_ != (int)engine::EngineType::FAISS_PQ) {
        return false;
    }

    auto search_job = std::static_pointer_cast<SearchJob>(search_task->job_.lock());
    ResourcePtr res_ptr;
    if (!gpu_enable_) {
        LOG_SERVER_DEBUG_ << LogOut("[%s][%d] FaissIVFPQPass: gpu disable, specify cpu to search!", "search", 0);
        res_ptr = ResMgrInst::GetInstance()->GetResource("cpu");
    } else if (search_job->nq() < (uint64_t)threshold_) {
        LOG_SERVER_DEBUG_ << LogOut("[%s][%d] FaissIVFPQPass: nq < gpu_search_threshold, specify cpu to search!",
                                    "search", 0);
        res_ptr = ResMgrInst::GetInstance()->GetResource("cpu");
    } else if (search_job->extra_params()[knowhere::IndexParams::nprobe].get<int64_t>() <
               milvus::server::QUERY_MAX_TOPK) {
        LOG_SERVER_DEBUG_ << LogOut("[%s][%d] FaissIVFFlatPass: nprobe > gpu_nprobe_threshold, specify cpu to search!",
                                    "search", 0);
        res_ptr = ResMgrInst::GetInstance()->GetResource("cpu");
    } else {
        LOG_SERVER_DEBUG_ << LogOut("[%s][%d] FaissIVFPQPass: nq >= gpu_search_threshold, specify gpu %d to search!",
                                    "search", 0, search_gpus_[idx_]);
        res_ptr = ResMgrInst::GetInstance()->GetResource(ResourceType::GPU, search_gpus_[idx_]);
        idx_ = (idx_ + 1) % search_gpus_.size();
    }
    auto label = std::make_shared<SpecResLabel>(res_ptr);
    task->label() = label;
    return true;
}

}  // namespace scheduler
}  // namespace milvus
#endif
