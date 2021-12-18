/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "device_profile_storage.h"

#include <cinttypes>
#include <thread>

#include "device_profile_errors.h"
#include "device_profile_log.h"
#include "device_profile_storage_manager.h"
#include "service_characteristic_profile.h"

#include "datetime_ex.h"

namespace OHOS {
namespace DeviceProfile {
using namespace OHOS::DistributedKv;
using namespace std::chrono_literals;

namespace {
const std::string TAG = "DeviceProfileStorage";

constexpr int32_t RETRY_TIMES_GET_KVSTORE = 10;
}

DeviceProfileStorage::DeviceProfileStorage(const std::string& appId, const std::string& storeId)
{
    appId_.appId = appId,
    storeId_.storeId = storeId;
}

void DeviceProfileStorage::SetOptions(const Options& options)
{
    options_ = options;
}

bool DeviceProfileStorage::RegisterKvStoreInitCallback(const KvStoreInitCallback& callback)
{
    if (kvStoreInitCallback_ != nullptr) {
        HILOGE("callback is not null");
        return false;
    }
    kvStoreInitCallback_ = callback;
    return true;
}

void DeviceProfileStorage::Init()
{
    int64_t begin = GetTickCount();
    std::unique_lock<std::shared_mutex> writeLock(storageLock_);
    bool result = TryGetKvStore();
    writeLock.unlock();
    int64_t end = GetTickCount();
    HILOGI("TryGetKvStore %{public}s, spend %{public}" PRId64 " ms",
        result ? "succeeded" : "failed", end - begin);
    // must call callback before set init status
    if (kvStoreInitCallback_ != nullptr) {
        kvStoreInitCallback_();
    }
    initStatus_ = StorageInitStatus::INIT_SUCCEED;
}

StorageInitStatus DeviceProfileStorage::GetInitStatus()
{
    return initStatus_;
}

bool DeviceProfileStorage::TryGetKvStore()
{
    int32_t retryTimes = 0;
    while (retryTimes < RETRY_TIMES_GET_KVSTORE) {
        if (GetKvStore() == Status::SUCCESS && kvStorePtr_ != nullptr) {
            return true;
        }
        HILOGD("retry get kvstore...");
        std::this_thread::sleep_for(500ms);
        retryTimes++;
    }
    if (kvStorePtr_ == nullptr) {
        initStatus_ = StorageInitStatus::INIT_FAILED;
        return false;
    }
    return true;
}

Status DeviceProfileStorage::GetKvStore()
{
    HILOGD("called");
    Status status = dataManager_.GetSingleKvStore(options_, appId_, storeId_, kvStorePtr_);
    if (status != Status::SUCCESS) {
        HILOGI("get failed, error = %{public}d", status);
    } else {
        HILOGI("get succeeded");
    }
    return status;
}

void DeviceProfileStorage::DeleteKvStore()
{
    Status status = dataManager_.DeleteKvStore(appId_, storeId_);
    if (status != Status::SUCCESS) {
        HILOGE("delete failed, error = %{public}d", status);
    }
}

int32_t DeviceProfileStorage::GetDeviceProfile(const std::string& key, std::string& value)
{
    std::shared_lock<std::shared_mutex> readLock(storageLock_);
    if (kvStorePtr_ == nullptr) {
        HILOGE("null kvstore");
        return ERR_DP_INVALID_PARAMS;
    }

    Key k(key);
    Value v;
    Status status = kvStorePtr_->Get(k, v);
    if (status != Status::SUCCESS) {
        HILOGE("get failed, %{public}d", status);
        return static_cast<int32_t>(status);
    }
    value = v.ToString();
    HILOGI("get succeeded");
    return static_cast<int32_t>(status);
}

int32_t DeviceProfileStorage::PutDeviceProfile(const std::string& key, const std::string& value)
{
    std::unique_lock<std::shared_mutex> writeLock(storageLock_);
    if (kvStorePtr_ == nullptr) {
        HILOGE("null kvstore");
        return ERR_DP_INVALID_PARAMS;
    }

    Key k(key);
    Value v(value);
    Status status  = kvStorePtr_->Put(k, v);
    if (status != Status::SUCCESS) {
        HILOGE("put failed, error = %{public}d", status);
    }
    return static_cast<int32_t>(status);
}

int32_t DeviceProfileStorage::PutDeviceProfileBatch(const std::vector<std::string>& keys,
    const std::vector<std::string>& values)
{
    std::unique_lock<std::shared_mutex> writeLock(storageLock_);
    if (kvStorePtr_ == nullptr) {
        HILOGE("null kvstore");
        return ERR_DP_INVALID_PARAMS;
    }

    const size_t keySize = keys.size();
    const size_t valSize = values.size();
    HILOGI("keySize = %{public}zu, valSize = %{public}zu", keySize, valSize);
    if (keySize != valSize) {
        HILOGE("diff key-value size");
        return ERR_DP_INVALID_PARAMS;
    }

    std::vector<Entry> entries;
    entries.reserve(keySize);
    for (uint32_t i = 0; i < keySize; i++) {
        Entry entry;
        entry.key = keys[i];
        entry.value = values[i];
        entries.emplace_back(entry);
    }

    Status status = kvStorePtr_->PutBatch(entries);
    if (status != Status::SUCCESS) {
        HILOGE("put batch failed, error = %{public}d", status);
    }
    return static_cast<int32_t>(status);
}

int32_t DeviceProfileStorage::DeleteDeviceProfile(const std::string& key)
{
    std::unique_lock<std::shared_mutex> writeLock(storageLock_);
    if (kvStorePtr_ == nullptr) {
        return ERR_DP_INVALID_PARAMS;
    }
    Key k(key);
    Status status = kvStorePtr_->Delete(k);
    if (status != Status::SUCCESS) {
        HILOGE("delete failed, error = %{public}d", status);
    }
    return static_cast<int32_t>(status);
}
} // namespace DeviceProfile
} // namespace OHOS
