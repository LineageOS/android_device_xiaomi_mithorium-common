/*
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include "memtrack_msm.h"

using namespace aidl::android::hardware::memtrack;

static class MemtrackRecord record_templates[] = {
    {
        .flags = MemtrackRecord::FLAG_SMAPS_ACCOUNTED |
                 MemtrackRecord::FLAG_PRIVATE |
                 MemtrackRecord::FLAG_NONSECURE,
    },
    {
        .flags = MemtrackRecord::FLAG_SMAPS_UNACCOUNTED |
                 MemtrackRecord::FLAG_PRIVATE |
                 MemtrackRecord::FLAG_NONSECURE,
    },
};

int kgsl_memtrack_get_memory(int pid, MemtrackType type,
                             std::vector<MemtrackRecord>* _aidl_return)
{
    char syspath[128];
    long accounted_size = 0;
    long unaccounted_size = 0;
    FILE *fp;
    int ret;

    if (type == MemtrackType::GL) {

        snprintf(syspath, sizeof(syspath),
                 "/sys/class/kgsl/kgsl/proc/%d/gpumem_mapped", pid);

        fp = fopen(syspath, "r");
        if (fp == NULL)
            return -errno;

        ret = fscanf(fp, "%lu", &accounted_size);
        if (ret != 1) {
            fclose(fp);
            return -EINVAL;
        }
        fclose(fp);

        snprintf(syspath, sizeof(syspath),
                 "/sys/class/kgsl/kgsl/proc/%d/gpumem_unmapped", pid);

        fp = fopen(syspath, "r");
        if (fp == NULL) {
            fclose(fp);
            return -errno;
        }

        ret = fscanf(fp, "%lu", &unaccounted_size);
        if (ret != 1) {
            fclose(fp);
            return -EINVAL;
        }
        fclose(fp);

    } else if (type == MemtrackType::GRAPHICS) {

        snprintf(syspath, sizeof(syspath),
                 "/sys/class/kgsl/kgsl/proc/%d/imported_mem", pid);

        fp = fopen(syspath, "r");
        if (fp == NULL)
            return -errno;

        ret = fscanf(fp, "%lu", &unaccounted_size);
        if (ret != 1) {
            fclose(fp);
            return -EINVAL;
        }
        fclose(fp);
    }

    record_templates[0].sizeInBytes = accounted_size;
    _aidl_return->emplace_back(record_templates[0]);
    record_templates[1].sizeInBytes = unaccounted_size;
    _aidl_return->emplace_back(record_templates[1]);

    return 0;
}
