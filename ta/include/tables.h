/*
 *
 * Copyright (C) 2017 GlobalLogic
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

#ifndef ANDROID_OPTEE_TABLES_H
#define ANDROID_OPTEE_TABLES_H

#define KM_MAX_USE_COUNTERS 20U
#define KM_MAX_USE_TIMERS 30U
#define UNDEFINED UINT32_MAX

#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>
#include <utee_defines.h>

#include "ta_ca_defs.h"
#include "master_crypto.h"

/* We save only key TAG to reduce the space */
typedef struct {
	uint8_t *key_tag;
	uint32_t count;
} keymaster_use_counter_t;

typedef struct {
	uint8_t *key_tag;
	TEE_Time last_access;
	uint32_t min_sec;
} keymaster_use_timer_t;

keymaster_error_t TA_count_key_uses(const keymaster_key_blob_t *key,
				const uint32_t max_uses);

void TA_clean_timers(void);

keymaster_error_t TA_trigger_timer(const keymaster_key_blob_t *key,
				const uint32_t min_sec);

keymaster_error_t TA_check_key_use_timer(const keymaster_key_blob_t *key,
				const uint32_t min_sec);

#endif/* ANDROID_OPTEE_TABLES_H */
