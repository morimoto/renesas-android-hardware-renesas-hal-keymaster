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

#ifndef ANDROID_OPTEE_AES_H
#define ANDROID_OPTEE_AES_H

#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>
#include <utee_defines.h>

#include "ta_ca_defs.h"
#include "operations.h"
#include "paddings.h"

void TA_append_tag(keymaster_blob_t *output, uint32_t *out_size,
			const uint8_t *tag, const uint32_t tag_len);

keymaster_error_t TA_aes_finish(keymaster_operation_t *operation,
				keymaster_blob_t *input,
				keymaster_blob_t *output, uint32_t *out_size,
				uint32_t tag_len);

#endif/*ANDROID_OPTEE_AES_H*/
