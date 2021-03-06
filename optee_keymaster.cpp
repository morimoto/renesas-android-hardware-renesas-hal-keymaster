/*
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

#include <utils/Log.h>
#include <cutils/properties.h>
#include <cstring>
#include <memory>
#include <new>

#include "optee_keymaster.h"
#include "optee_keymaster_ipc.h"

#undef LOG_TAG
#define LOG_TAG "OpteeKeymaster"

namespace android {
namespace hardware {
namespace keymaster {
namespace V3_0 {
namespace renesas {

static inline keymaster_tag_type_t typeFromTag(const keymaster_tag_t tag) {
    return keymaster_tag_get_type(tag);
}

/*
 * legacy_enum_conversion converts enums from hidl to keymaster and back. Currently, this is just a
 * cast to make the compiler happy. One of two things should happen though:
 * TODO The keymaster enums should become aliases for the hidl generated enums so that we have a
 *      single point of truth. Then this cast function can go away.
 */
inline static keymaster_tag_t legacy_enum_conversion(const Tag value) {
    return keymaster_tag_t(value);
}

inline static Tag legacy_enum_conversion(const keymaster_tag_t value) {
    return Tag(value);
}

inline static keymaster_purpose_t legacy_enum_conversion(const KeyPurpose value) {
    return keymaster_purpose_t(value);
}

inline static keymaster_key_format_t legacy_enum_conversion(const KeyFormat value) {
    return keymaster_key_format_t(value);
}

inline static ErrorCode legacy_enum_conversion(const keymaster_error_t value) {
    return ErrorCode(value);
}

/*
 * KmParamSet implementation
 */
KmParamSet::KmParamSet():
keymaster_key_param_set_t{nullptr, 0} { }

KmParamSet::KmParamSet(const hidl_vec<KeyParameter> &keyParams) {
    params = new keymaster_key_param_t[keyParams.size()];
    length = keyParams.size();
    for (size_t i = 0; i < keyParams.size(); ++i) {
        auto tag = legacy_enum_conversion(keyParams[i].tag);
        switch (typeFromTag(tag)) {
        case KM_ENUM:
        case KM_ENUM_REP:
            params[i] = keymaster_param_enum(tag, keyParams[i].f.integer);
            break;
        case KM_UINT:
        case KM_UINT_REP:
            params[i] = keymaster_param_int(tag, keyParams[i].f.integer);
            break;
        case KM_ULONG:
        case KM_ULONG_REP:
            params[i] = keymaster_param_long(tag, keyParams[i].f.longInteger);
            break;
        case KM_DATE:
            params[i] = keymaster_param_date(tag, keyParams[i].f.dateTime);
            break;
        case KM_BOOL:
            if (keyParams[i].f.boolValue)
                params[i] = keymaster_param_bool(tag);
            else
                params[i].tag = KM_TAG_INVALID;
            break;
        case KM_BIGNUM:
        case KM_BYTES:
            params[i] =
                keymaster_param_blob(tag, &keyParams[i].blob[0], keyParams[i].blob.size());
            break;
        case KM_INVALID:
        default:
            params[i].tag = KM_TAG_INVALID;
            /* just skip */
            break;
        }
    }
}

KmParamSet::KmParamSet(KmParamSet &&other):
    keymaster_key_param_set_t{other.params, other.length} {
    other.length = 0;
    other.params = nullptr;
}

KmParamSet::~KmParamSet() { delete[] params; }

inline static KmParamSet hidlParams2KmParamSet(const hidl_vec<KeyParameter> &params) {
    return KmParamSet(params);
}

inline static keymaster_blob_t hidlVec2KmBlob(const hidl_vec<uint8_t> &blob) {
    if (blob.size())
        return {&blob[0], blob.size()};
    return {nullptr, 0};
}

inline static keymaster_key_blob_t hidlVec2KmKeyBlob(const hidl_vec<uint8_t> &blob) {
    if (blob.size())
        return {&blob[0], blob.size()};
    return {nullptr, 0};
}

inline static hidl_vec<uint8_t> kmBlob2hidlVec(const keymaster_key_blob_t &blob) {
    hidl_vec<uint8_t> result;
    result.setToExternal(const_cast<unsigned char *>(blob.key_material), blob.key_material_size);
    return result;
}

inline static hidl_vec<uint8_t> kmBlob2hidlVec(const keymaster_blob_t &blob) {
    hidl_vec<uint8_t> result;
    result.setToExternal(const_cast<unsigned char *>(blob.data), blob.data_length);
    return result;
}

inline static hidl_vec<hidl_vec<uint8_t>> kmCertChain2Hidl(
                const keymaster_cert_chain_t *cert_chain) {
    hidl_vec<hidl_vec<uint8_t>> result;
    if (!cert_chain || cert_chain->entry_count == 0 || !cert_chain->entries)
        return result;

    result.resize(cert_chain->entry_count);
    for (size_t i = 0; i < cert_chain->entry_count; ++i)
    {
        auto &entry = cert_chain->entries[i];
        result[i] = kmBlob2hidlVec(entry);
    }

    return result;
}

static inline hidl_vec<KeyParameter> kmParamSet2Hidl(const keymaster_key_param_set_t& set) {
    hidl_vec<KeyParameter> result;
    if (set.length == 0 || set.params == nullptr) return result;

    result.resize(set.length);
    keymaster_key_param_t* params = set.params;
    for (size_t i = 0; i < set.length; ++i) {
        auto tag = params[i].tag;
      result[i].tag = legacy_enum_conversion(tag);
      switch (typeFromTag(tag)) {
      case KM_ENUM:
      case KM_ENUM_REP:
          result[i].f.integer = params[i].enumerated;
          break;
      case KM_UINT:
      case KM_UINT_REP:
          result[i].f.integer = params[i].integer;
          break;
      case KM_ULONG:
      case KM_ULONG_REP:
          result[i].f.longInteger = params[i].long_integer;
          break;
      case KM_DATE:
          result[i].f.dateTime = params[i].date_time;
          break;
      case KM_BOOL:
          result[i].f.boolValue = params[i].boolean;
          break;
      case KM_BIGNUM:
      case KM_BYTES:
          result[i].blob.setToExternal(const_cast<unsigned char*>(params[i].blob.data),
                                       params[i].blob.data_length);
          break;
      case KM_INVALID:
      default:
          params[i].tag = KM_TAG_INVALID;
          /* just skip */
          break;
      }
  }
    return result;
}

/*OpteeKeymasterDevice implementation*/

OpteeKeymasterDevice::OpteeKeymasterDevice() {
    connect();
}

OpteeKeymasterDevice::~OpteeKeymasterDevice() {
    disconnect();
}

Return<void>  OpteeKeymasterDevice::getHardwareFeatures(getHardwareFeatures_cb _hidl_cb) {
    //send results off to the client
    _hidl_cb(is_secure_, supports_ec_, supports_symmetric_cryptography_,
             supports_attestation_, supports_all_digests_,
             name_, author_);
    return Void();
}

Return<ErrorCode> OpteeKeymasterDevice::addRngEntropy(const hidl_vec<uint8_t> &data) {
    ErrorCode rc = ErrorCode::OK;
    int in_size = data.size() + sizeof(size_t);
    std::unique_ptr<uint8_t[]> in(new uint8_t[in_size]);
    /*Restrictions for max input data length 2KB*/
    const uint32_t maxInputData = 1024 * 2;
    if (!checkConnection(rc))
        goto error;
    if (!data.size())
        goto out;
    if (data.size() > maxInputData) {
        rc = ErrorCode::INVALID_INPUT_LENGTH;
        goto error;
    }
    memset(in.get(), 0, in_size);
    serializeData(in.get(), data.size(), &data[0], sizeof(uint8_t));

    rc = legacy_enum_conversion(
        optee_keystore_call(KM_ADD_RNG_ENTROPY, in.get(), in_size, nullptr, 0));

    if (rc != ErrorCode::OK)
        ALOGE("Add RNG entropy failed with code %d [%x]", rc, rc);
out:
error:
    return rc;
}

int OpteeKeymasterDevice::osVersion(uint32_t *in) {
    char value[PROPERTY_VALUE_MAX] = {0,};
    char *str = value;

    if (property_get("ro.build.version.release", value, "") <= 0) {
        ALOGE("Error get property ro.build.version.release");
        *in = 0xFFFFFFFF;
        goto exit;
    }
    *in = (uint32_t) std::atoi(str) * 10000;

    if ((str = std::strchr(str, '.')) != NULL) {
        *in += (uint32_t) std::atoi(str + 1) * 100;
    } else {
        goto exit;
    }

    if ((str = std::strchr(str + 1, '.')) != NULL) {
        *in += (uint32_t) std::atoi(str + 1);
    }

exit:
    return sizeof(*in);
}

int OpteeKeymasterDevice::osPatchlevel(uint32_t *in) {
    char value[PROPERTY_VALUE_MAX] = {0,};
    char *str = value;

    if (property_get("ro.build.version.security_patch", value, "") <= 0) {
        ALOGE("Error get property ro.build.version.security_patch");
        *in = 0xFFFFFFFF;
        goto exit;
    }

    *in = (uint32_t) std::atoi(str) * 100;

    if ((str = std::strchr(str, '-')) != NULL) {
        *in += (uint32_t) std::atoi(str + 1);
    } else {
        *in = 0xFFFFFFFF;
    }

exit:
    return sizeof(*in);
}

Return<void> OpteeKeymasterDevice::generateKey(const hidl_vec<KeyParameter> &keyParams,
                                          generateKey_cb _hidl_cb) {
    ErrorCode rc = ErrorCode::OK;
    KeyCharacteristics resultCharacteristics;
    hidl_vec<uint8_t> resultKeyBlob;
    KmParamSet kmParams = hidlParams2KmParamSet(keyParams);
    keymaster_key_blob_t kmKeyBlob{nullptr, 0};
    keymaster_key_characteristics_t kmKeyCharacteristics{{nullptr, 0}, {nullptr, 0}};
    uint32_t outSize = recv_buf_size_;
    uint32_t inSize = getParamSetSize(kmParams) + 2 * sizeof(uint32_t); //+ os_version & patchlevel
    std::unique_ptr<uint8_t[]> out(new uint8_t[outSize]);
    std::unique_ptr<uint8_t[]> in(new uint8_t[inSize]);
    uint8_t *ptr = nullptr;
    if (!checkConnection(rc))
        goto error;
    memset(out.get(), 0, outSize);
    memset(in.get(), 0, inSize);

    ptr = in.get();
    ptr += serializeParamSet(ptr, kmParams);

    ptr += osVersion((uint32_t *)ptr);
    ptr += osPatchlevel((uint32_t *)ptr);

    rc = legacy_enum_conversion(
        optee_keystore_call(KM_GENERATE_KEY, in.get(),
            inSize, out.get(), outSize));
    if (rc != ErrorCode::OK) {
        ALOGE("Generate key failed with error code %d [%x]", rc, rc);
        goto error;
    }

    ptr = out.get();
    ptr += deserializeKeyBlob(kmKeyBlob, ptr, rc);
    if (rc != ErrorCode::OK) {
        ALOGE("Failed to deserialize key blob");
        goto error;
    }
    ptr += deserializeKeyCharacteristics(kmKeyCharacteristics, ptr, rc);
    if (rc != ErrorCode::OK) {
        ALOGE("Failed to deserialize characteristics");
        goto error;
    }

    resultKeyBlob = kmBlob2hidlVec(kmKeyBlob);
    resultCharacteristics.softwareEnforced = kmParamSet2Hidl(kmKeyCharacteristics.sw_enforced);
    resultCharacteristics.teeEnforced = kmParamSet2Hidl(kmKeyCharacteristics.hw_enforced);

error:
    //send results off to the client
    _hidl_cb(rc, resultKeyBlob, resultCharacteristics);
    if (kmKeyBlob.key_material)
        free(const_cast<uint8_t *>(kmKeyBlob.key_material));
    keymaster_free_characteristics(&kmKeyCharacteristics);
    return Void();
}

Return<void>  OpteeKeymasterDevice::getKeyCharacteristics(const hidl_vec<uint8_t> &keyBlob,
                                   const hidl_vec<uint8_t> &clientId,
                                   const hidl_vec<uint8_t> &appData,
                                   getKeyCharacteristics_cb _hidl_cb) {
    ErrorCode rc = ErrorCode::OK;
    KeyCharacteristics resultCharacteristics;
    keymaster_key_characteristics_t kmKeyCharacteristics{{nullptr, 0}, {nullptr, 0}};
    keymaster_key_blob_t kmKeyBlob = hidlVec2KmKeyBlob(keyBlob);
    keymaster_blob_t kmClientId = hidlVec2KmBlob(clientId);
    keymaster_blob_t kmAppData = hidlVec2KmBlob(appData);
    int outSize = recv_buf_size_;
    int inSize = getKeyBlobSize(kmKeyBlob);
    inSize += sizeof(presence);
    if (clientId.size())
        inSize += getBlobSize(kmClientId);
    inSize += sizeof(presence);
    if (appData.size())
        inSize += getBlobSize(kmAppData);
    std::unique_ptr<uint8_t[]> out(new uint8_t[outSize]);
    std::unique_ptr<uint8_t[]> in(new uint8_t[inSize]);
    uint8_t *ptr = nullptr;
    if (!checkConnection(rc))
        goto error;
    if (!keyBlob.size() || kmKeyBlob.key_material == nullptr) {
        rc = ErrorCode::UNEXPECTED_NULL_POINTER;
        goto error;
    }
    memset(out.get(), 0, outSize);
    memset(in.get(), 0, inSize);
    ptr = in.get();
    ptr += serializeData(ptr, kmKeyBlob.key_material_size, kmKeyBlob.key_material,
                     SIZE_OF_ITEM(kmKeyBlob.key_material));
    ptr += serializeBlobWithPresenceInfo(ptr, kmClientId, clientId.size());
    ptr += serializeBlobWithPresenceInfo(ptr, kmAppData, appData.size());

    rc = legacy_enum_conversion(
        optee_keystore_call(KM_GET_KEY_CHARACTERISTICS, in.get(),
            inSize, out.get(), outSize));

    if (rc != ErrorCode::OK) {
        ALOGE("Get key characteristics failed with code %d, [%x]", rc, rc);
        goto error;
    }

    deserializeKeyCharacteristics(kmKeyCharacteristics, out.get(), rc);
    if (rc != ErrorCode::OK) {
        ALOGE("Failed to deserialize key characteristics");
        goto error;
    }

    resultCharacteristics.softwareEnforced = kmParamSet2Hidl(kmKeyCharacteristics.sw_enforced);
    resultCharacteristics.teeEnforced = kmParamSet2Hidl(kmKeyCharacteristics.hw_enforced);

error:
    // send results off to the client
    _hidl_cb(rc, resultCharacteristics);
    keymaster_free_characteristics(&kmKeyCharacteristics);
    return Void();
}

Return<void>  OpteeKeymasterDevice::importKey(const hidl_vec<KeyParameter> &params, KeyFormat keyFormat,
                       const hidl_vec<uint8_t> &keyData, importKey_cb _hidl_cb) {
    ErrorCode rc = ErrorCode::OK;
    KeyCharacteristics resultCharacteristics;
    hidl_vec<uint8_t> resultKeyBlob;
    keymaster_key_blob_t kmKeyBlob{nullptr, 0};
    keymaster_key_characteristics_t kmKeyCharacteristics{{nullptr, 0}, {nullptr, 0}};
    KmParamSet kmParams = hidlParams2KmParamSet(params);
    keymaster_blob_t kmKeyData = hidlVec2KmBlob(keyData);
    keymaster_key_format_t kmKeyFormat = legacy_enum_conversion(keyFormat);
    int outSize = recv_buf_size_;
    int inSize = getParamSetSize(kmParams) + SIZE_OF_ITEM(kmParams.params) +
                    getBlobSize(kmKeyData);
    std::unique_ptr<uint8_t[]> out(new uint8_t[outSize]);
    std::unique_ptr<uint8_t[]> in(new uint8_t[inSize]);
    uint8_t *ptr = nullptr;
    if (!checkConnection(rc))
        goto error;
    memset(in.get(), 0, inSize);
    memset(out.get(), 0, outSize);
    ptr = in.get();
    ptr += serializeParamSet(ptr, kmParams);
    ptr += serializeKeyFormat(ptr, kmKeyFormat);
    ptr += serializeData(ptr, kmKeyData.data_length, kmKeyData.data,
                                               SIZE_OF_ITEM(kmKeyData.data));

    rc = legacy_enum_conversion(
        optee_keystore_call(KM_IMPORT_KEY, in.get(), inSize, out.get(), outSize));

    if (rc != ErrorCode::OK) {
        ALOGE("Import key failed with code %d [%x]", rc, rc);
        goto error;
    }

    ptr = out.get();
    ptr += deserializeKeyBlob(kmKeyBlob, ptr, rc);
    if (rc != ErrorCode::OK) {
        ALOGE("Failed to allocate memory for blob deserialization");
        goto error;
    }
    ptr += deserializeKeyCharacteristics(kmKeyCharacteristics, ptr, rc);
    if (rc != ErrorCode::OK) {
        ALOGE("Failed to allocate memory for characteristics deserialization");
        goto error;
    }

    resultKeyBlob = kmBlob2hidlVec(kmKeyBlob);
    resultCharacteristics.softwareEnforced = kmParamSet2Hidl(kmKeyCharacteristics.sw_enforced);
    resultCharacteristics.teeEnforced = kmParamSet2Hidl(kmKeyCharacteristics.hw_enforced);

error:
    //send results off to the client
    _hidl_cb(rc, resultKeyBlob, resultCharacteristics);

    if (kmKeyBlob.key_material)
        free(const_cast<uint8_t *>(kmKeyBlob.key_material));
    keymaster_free_characteristics(&kmKeyCharacteristics);

    return Void();
}

Return<void>  OpteeKeymasterDevice::exportKey(KeyFormat exportFormat, const hidl_vec<uint8_t> &keyBlob,
                       const hidl_vec<uint8_t> &clientId, const hidl_vec<uint8_t> &appData,
                       exportKey_cb _hidl_cb) {
    ErrorCode rc = ErrorCode::OK;
    hidl_vec<uint8_t> resultKeyBlob;
    keymaster_blob_t kmBlob{nullptr, 0};
    keymaster_key_blob_t kmKeyBlob = hidlVec2KmKeyBlob(keyBlob);
    keymaster_blob_t kmClientId = hidlVec2KmBlob(clientId);
    keymaster_blob_t kmAppData = hidlVec2KmBlob(appData);
    keymaster_key_format_t kmKeyFormat = legacy_enum_conversion(exportFormat);
    int outSize = recv_buf_size_;
    int inSize = getKeyBlobSize(kmKeyBlob);
    if (!clientId.size())
        inSize += getBlobSize(kmClientId);
    inSize += sizeof(presence);
    if (!appData.size())
        inSize += getBlobSize(kmAppData);
    std::unique_ptr<uint8_t[]> out(new uint8_t[outSize]);
    std::unique_ptr<uint8_t[]> in(new uint8_t[inSize]);
    uint8_t *ptr = nullptr;
    if (!checkConnection(rc))
        goto error;
    if (!keyBlob.size() || kmKeyBlob.key_material == nullptr) {
        rc = ErrorCode::UNEXPECTED_NULL_POINTER;
    }
    memset(out.get(), 0, outSize);
    memset(in.get(), 0, inSize);
    ptr = in.get();
    ptr += serializeKeyFormat(ptr, kmKeyFormat);
    ptr += serializeData(ptr, kmKeyBlob.key_material_size,
                     kmKeyBlob.key_material,
                     SIZE_OF_ITEM(kmKeyBlob.key_material));
    ptr += serializeBlobWithPresenceInfo(ptr, kmClientId, clientId.size());
    ptr += serializeBlobWithPresenceInfo(ptr, kmAppData, appData.size());

    rc = legacy_enum_conversion(
        optee_keystore_call(KM_EXPORT_KEY, in.get(), inSize, out.get(), outSize));

    if (rc != ErrorCode::OK) {
        ALOGE("Export key failed with code %d [%x]", rc, rc);
        goto error;
    }

    deserializeBlob(kmBlob, out.get(), rc);
    if (rc != ErrorCode::OK) {
        ALOGE("Failed to deserialize blob from TA");
        goto error;
    }

    resultKeyBlob = kmBlob2hidlVec(kmBlob);

error:
    //send results off to the client
    _hidl_cb(rc, resultKeyBlob);

    // free buffers that we are responsible for
    if (kmBlob.data)
        free(const_cast<uint8_t *>(kmBlob.data));

    return Void();
}

int OpteeKeymasterDevice::verifiedBootState(uint8_t *in) {
    char value[PROPERTY_VALUE_MAX] = {0,};

    if (property_get("ro.boot.verifiedbootstate", value, "") > 0) {
        if (value[0] == 'g') {
            *in = (uint8_t) 0x0;
        } else if (value[0] == 'y') {
            *in = (uint8_t) 0x1;
        } else if (value[0] == 'o') {
            *in = (uint8_t) 0x2;
        } else {
            *in = (uint8_t) 0xff;
        }
    } else {
        ALOGE("Error get property ro.boot.verifiedbootstate");
        *in = (uint8_t) 0xff;
    }

    return sizeof(*in);
}

Return<void>  OpteeKeymasterDevice::attestKey(const hidl_vec<uint8_t> &keyToAttest,
                       const hidl_vec<KeyParameter> &attestParams,
                       attestKey_cb _hidl_cb) {
    ErrorCode rc = ErrorCode::OK;
    hidl_vec<hidl_vec<uint8_t>> resultCertChain;
    keymaster_cert_chain_t kmCertChain{nullptr, 0};
    keymaster_key_blob_t kmKeyToAttest = hidlVec2KmKeyBlob(keyToAttest);
    KmParamSet kmAttestParams = hidlParams2KmParamSet(attestParams);
    int outSize = recv_buf_size_;
    int inSize = getParamSetSize(kmAttestParams) + getKeyBlobSize(kmKeyToAttest)
               + sizeof(uint8_t);  // verifiedbootstate
    std::unique_ptr<uint8_t[]> out(new uint8_t[outSize]);
    std::unique_ptr<uint8_t[]> in(new uint8_t[inSize]);
    uint8_t *perm = nullptr;
    uint8_t *ptr = nullptr;
    if (!checkConnection(rc))
        goto error;
    memset(in.get(), 0, inSize);
    memset(out.get(), 0, outSize);
    for (size_t i = 0; i < attestParams.size(); ++i) {
        switch (attestParams[i].tag) {
        case Tag::ATTESTATION_ID_BRAND:
        case Tag::ATTESTATION_ID_DEVICE:
        case Tag::ATTESTATION_ID_PRODUCT:
        case Tag::ATTESTATION_ID_SERIAL:
        case Tag::ATTESTATION_ID_IMEI:
        case Tag::ATTESTATION_ID_MEID:
        case Tag::ATTESTATION_ID_MANUFACTURER:
        case Tag::ATTESTATION_ID_MODEL:
            // Device id attestation may only be supported if the device is able to permanently
            // destroy its knowledge of the ids. This device is unable to do this, so it must
            // never perform any device id attestation.
            rc = ErrorCode::CANNOT_ATTEST_IDS;
            goto error;
        case Tag::ATTESTATION_APPLICATION_ID:
            break;
        default:
            break;
        }
    }

    ptr = in.get();
    ptr += serializeData(ptr, kmKeyToAttest.key_material_size,
                    kmKeyToAttest.key_material,
                    SIZE_OF_ITEM(kmKeyToAttest.key_material));
    ptr += serializeParamSet(ptr, kmAttestParams);

    ptr += verifiedBootState(ptr);

    rc = legacy_enum_conversion(
        optee_keystore_call(KM_ATTEST_KEY, in.get(), inSize, out.get(), outSize));

    if (rc != ErrorCode::OK) {
        ALOGE("Attest key failed with code %d [%x]", rc, rc);
        goto error;
    }

    ptr = out.get();
    ptr += deserializeSize(kmCertChain.entry_count, ptr);
    kmCertChain.entries = new (std::nothrow) keymaster_blob_t[kmCertChain.entry_count];
    if (!kmCertChain.entries) {
        ALOGE("Failed to allocate memory for cert chain");
        rc = ErrorCode::MEMORY_ALLOCATION_FAILED;
        goto error;
    }
    for(size_t i = 0; i < kmCertChain.entry_count; i++) {
        ptr += deserializeSize(kmCertChain.entries[i].data_length, ptr);;
        perm = new (std::nothrow) uint8_t[kmCertChain.entries[i].data_length];
        if (!perm) {
            ALOGE("Failed to allocate memory on certificate chain deserialization");
            rc = ErrorCode::MEMORY_ALLOCATION_FAILED;
            goto error;
        }
        memcpy(perm, ptr, kmCertChain.entries[i].data_length);
        ptr += kmCertChain.entries[i].data_length;
        kmCertChain.entries[i].data = perm;
    }

    resultCertChain = kmCertChain2Hidl(&kmCertChain);

error:
    //send results off to the client
    _hidl_cb(rc, resultCertChain);

    if (rc == ErrorCode::OK)
        keymaster_free_cert_chain(&kmCertChain);

    return Void();
}

Return<void>  OpteeKeymasterDevice::upgradeKey(const hidl_vec<uint8_t> &keyBlobToUpgrade,
                        const hidl_vec<KeyParameter> &upgradeParams,
                        upgradeKey_cb _hidl_cb) {
    ErrorCode rc = ErrorCode::OK;
    hidl_vec<uint8_t> resultKeyBlob;
    keymaster_key_blob_t kmKeyBlob{nullptr, 0};
    keymaster_key_blob_t kmKeyBlobToUpgrade = hidlVec2KmKeyBlob(keyBlobToUpgrade);
    KmParamSet kmUpgradeParams = hidlParams2KmParamSet(upgradeParams);
    int outSize = recv_buf_size_;
    int inSize = getKeyBlobSize(kmKeyBlobToUpgrade) +
                getParamSetSize(kmUpgradeParams);
    std::unique_ptr<uint8_t[]> out(new uint8_t[outSize]);
    std::unique_ptr<uint8_t[]> in(new uint8_t[inSize]);
    uint8_t *ptr = nullptr;
    if (!checkConnection(rc))
        goto error;
    memset(out.get(), 0, outSize);
    memset(in.get(), 0, inSize);
    ptr = in.get();
    ptr += serializeData(ptr, kmKeyBlobToUpgrade.key_material_size,
                   kmKeyBlobToUpgrade.key_material,
                   SIZE_OF_ITEM(kmKeyBlobToUpgrade.key_material));
    ptr += serializeParamSet(ptr, kmUpgradeParams);

    rc = legacy_enum_conversion(
        optee_keystore_call(KM_UPGRADE_KEY, in.get(), inSize, out.get(), outSize));
    if (rc != ErrorCode::OK) {
        ALOGE("Upgrade key failed with code %d [%x]", rc, rc);
        goto error;
    }

    deserializeKeyBlob(kmKeyBlob, out.get(), rc);
    if (rc != ErrorCode::OK) {
        ALOGE("Failed to deserialize key blob");
        goto error;
    }

    resultKeyBlob = kmBlob2hidlVec(kmKeyBlob);

error:
    //send results off to the client
    _hidl_cb(rc, resultKeyBlob);

    if (kmKeyBlob.key_material)
        free(const_cast<uint8_t *>(kmKeyBlob.key_material));

    return Void();
}

Return<ErrorCode>  OpteeKeymasterDevice::deleteKey(const hidl_vec<uint8_t> &keyBlob) {
    ErrorCode rc = ErrorCode::OK;
    keymaster_key_blob_t kmKeyBlob = hidlVec2KmKeyBlob(keyBlob);
    int inSize = getKeyBlobSize(kmKeyBlob);
    std::unique_ptr<uint8_t[]> in(new uint8_t[inSize]);
    if (!checkConnection(rc))
        goto error;
    memset(in.get(), 0, inSize);
    serializeData(in.get(), kmKeyBlob.key_material_size, kmKeyBlob.key_material,
                        SIZE_OF_ITEM(kmKeyBlob.key_material));

    rc = legacy_enum_conversion(
        optee_keystore_call(KM_DELETE_KEY, in.get(), inSize, nullptr, 0));

    /*
     * Keymaster 3.0 requires deleteKey to return ErrorCode::OK if the key
     * blob is unusable after the call. This is equally true if the key blob was
     * unusable before.
     */
    if (rc == ErrorCode::INVALID_KEY_BLOB)
        rc = ErrorCode::OK;

    if (rc != ErrorCode::OK)
        ALOGE("Attest key failed with code %d [%x]", rc, rc);
error:
    return rc;
}

Return<ErrorCode> OpteeKeymasterDevice::deleteAllKeys() {
    ErrorCode rc = ErrorCode::OK;
    if (!checkConnection(rc))
        goto error;
    rc = legacy_enum_conversion(
        optee_keystore_call(KM_DELETE_ALL_KEYS, nullptr, 0, nullptr, 0));
    if (rc != ErrorCode::OK)
        ALOGE("Delete all keys failed with code %d [%x]", rc, rc);
error:
    return rc;
}

Return<ErrorCode> OpteeKeymasterDevice::destroyAttestationIds() {
    ErrorCode rc = ErrorCode::OK;
    if (!checkConnection(rc))
        return rc;
    return ErrorCode::UNIMPLEMENTED;
}

Return<void> OpteeKeymasterDevice::begin(KeyPurpose purpose, const hidl_vec<uint8_t> &key,
                   const hidl_vec<KeyParameter> &inParams, begin_cb _hidl_cb) {
    ErrorCode rc = ErrorCode::OK;
    hidl_vec<KeyParameter> resultParams;
    uint64_t resultOpHandle = 0;
    KmParamSet kmOutParams;
    KmParamSet kmInParams = hidlParams2KmParamSet(inParams);
    keymaster_key_blob_t kmKey = hidlVec2KmKeyBlob(key);
    keymaster_purpose_t kmPurpose = legacy_enum_conversion(purpose);
    int outSize = recv_buf_size_;
    int inSize = sizeof(purpose) + getKeyBlobSize(kmKey) +
        sizeof(presence) + getParamSetSize(kmInParams);
    std::unique_ptr<uint8_t[]> out(new uint8_t[outSize]);
    std::unique_ptr<uint8_t[]> in(new uint8_t[inSize]);
    uint8_t *ptr = nullptr;
    if (!checkConnection(rc))
        goto error;
    if (kmKey.key_material == nullptr) {
        rc = ErrorCode::UNEXPECTED_NULL_POINTER;
        goto error;
    }
    memset(out.get(), 0, outSize);
    memset(in.get(), 0, inSize);
    ptr = in.get();
    memcpy(ptr, &kmPurpose, sizeof(kmPurpose));
    ptr += sizeof(kmPurpose);
    ptr += serializeData(ptr, kmKey.key_material_size, kmKey.key_material,
        SIZE_OF_ITEM(kmKey.key_material));
    ptr += serializeParamSetWithPresence(ptr, kmInParams);

    rc = legacy_enum_conversion(
        optee_keystore_call(KM_BEGIN, in.get(), inSize, out.get(), outSize));

    if (rc != ErrorCode::OK) {
        ALOGE("Begin failed with code %d [%x]", rc, rc);
        goto error;
    }

    ptr = out.get();
    ptr += deserializeParamSet(kmOutParams, ptr, rc);
    if (rc != ErrorCode::OK) {
        ALOGE("Failed to deserialize param set from TA");
        goto error;
    }
    memcpy(&resultOpHandle, ptr, sizeof(resultOpHandle));

    resultParams = kmParamSet2Hidl(kmOutParams);

error:
    //send results off to the client
    _hidl_cb(rc, resultParams, resultOpHandle);

    keymaster_free_param_set(&kmOutParams);

    return Void();
}

Return<void> OpteeKeymasterDevice::update(uint64_t operationHandle, const hidl_vec<KeyParameter> &inParams,
                    const hidl_vec<uint8_t> &input, update_cb _hidl_cb) {
    ErrorCode rc = ErrorCode::OK;
    uint32_t resultConsumed = 0;
    hidl_vec<KeyParameter> resultParams;
    hidl_vec<uint8_t> resultBlob;
    size_t consumed = 0;
    KmParamSet kmOutParams;
    KmParamSet kmInParams = hidlParams2KmParamSet(inParams);
    keymaster_blob_t kmOutBlob{nullptr, 0};
    keymaster_blob_t kmInputBlob = hidlVec2KmBlob(input);
    int outSize = recv_buf_size_;
    int inSize = sizeof(operationHandle) + getBlobSize(kmInputBlob) +
            sizeof(presence) + getParamSetSize(kmInParams);
    std::unique_ptr<uint8_t[]> out(new uint8_t[outSize]);
    std::unique_ptr<uint8_t[]> in(new uint8_t[inSize]);
    uint8_t *ptr = nullptr;
    if (!checkConnection(rc))
        goto error;
    memset(out.get(), 0, outSize);
    memset(in.get(), 0, inSize);
    ptr = in.get();
    ptr += serializeSize(ptr, operationHandle);
    ptr += serializeParamSetWithPresence(ptr, kmInParams);
    ptr += serializeData(ptr, kmInputBlob.data_length, kmInputBlob.data,
                        SIZE_OF_ITEM(kmInputBlob.data));
    rc = legacy_enum_conversion(
        optee_keystore_call(KM_UPDATE, in.get(), inSize, out.get(), outSize));

    if (rc != ErrorCode::OK) {
        ALOGE("Update failed with code %d [%x]", rc, rc);
        goto error;
    }

    ptr = out.get();
    memcpy(&consumed, ptr, sizeof(consumed));
    ptr += sizeof(consumed);
    ptr += deserializeBlob(kmOutBlob, ptr, rc);
    if (rc != ErrorCode::OK) {
        ALOGE("Failed to deserialize blob from TA");
        goto error;
    }
    ptr += deserializeParamSet(kmOutParams, ptr, rc);
    if (rc != ErrorCode::OK) {
        ALOGE("Failed to deserialize param set from TA");
        goto error;
    }

    resultConsumed = consumed;
    resultParams = kmParamSet2Hidl(kmOutParams);
    resultBlob = kmBlob2hidlVec(kmOutBlob);

error:
    //send results off to the client
    _hidl_cb(rc, resultConsumed, resultParams, resultBlob);

    keymaster_free_param_set(&kmOutParams);
    if (kmOutBlob.data)
        free(const_cast<uint8_t *>(kmOutBlob.data));

    return Void();
}

Return<void>  OpteeKeymasterDevice::finish(uint64_t operationHandle, const hidl_vec<KeyParameter> &inParams,
                    const hidl_vec<uint8_t> &input, const hidl_vec<uint8_t> &signature,
                    finish_cb _hidl_cb) {
    ErrorCode rc = ErrorCode::OK;
    hidl_vec<KeyParameter> resultParams;
    hidl_vec<uint8_t> resultBlob;
    KmParamSet kmOutParams;
    KmParamSet kmInParams = hidlParams2KmParamSet(inParams);
    keymaster_blob_t kmOutBlob{nullptr, 0};
    keymaster_blob_t kmInput = hidlVec2KmBlob(input);
    keymaster_blob_t kmSignature = hidlVec2KmBlob(signature);
    int outSize = recv_buf_size_;
    int inSize = sizeof(operationHandle) +
            sizeof(presence) + getBlobSize(kmSignature) +
            sizeof(presence) + getBlobSize(kmInput) +
            sizeof(presence) + getParamSetSize(kmInParams);
    std::unique_ptr<uint8_t[]> out(new uint8_t[outSize]);
    std::unique_ptr<uint8_t[]> in(new uint8_t[inSize]);
    uint8_t *ptr = nullptr;
    if (!checkConnection(rc))
        goto error;
    ptr = in.get();
    memcpy(ptr, &operationHandle, sizeof(operationHandle));
    ptr += sizeof(operationHandle);
    ptr += serializeParamSetWithPresence(ptr, kmInParams);
    ptr += serializeBlobWithPresenceInfo(ptr, kmInput, true);
    ptr += serializeBlobWithPresenceInfo(ptr, kmSignature, true);

    rc = legacy_enum_conversion(
        optee_keystore_call(KM_FINISH, in.get(), inSize, out.get(), outSize));

    if (rc != ErrorCode::OK) {
        ALOGE("Finish failed with code %d [%x]", rc, rc);
        goto error;
    }

    ptr = out.get();
    ptr += deserializeParamSet(kmOutParams, ptr, rc);
    if (rc != ErrorCode::OK) {
        ALOGE("Failed deserialize param set from TA");
        goto error;
    }
    ptr += deserializeBlob(kmOutBlob, ptr, rc);
    if (rc != ErrorCode::OK) {
        ALOGE("Failed to deserialize blob from TA");
        goto error;
    }

    resultParams = kmParamSet2Hidl(kmOutParams);
    resultBlob = kmBlob2hidlVec(kmOutBlob);

error:
    //send results off to the client
    _hidl_cb(rc, resultParams, resultBlob);

    keymaster_free_param_set(&kmOutParams);
    if (kmOutBlob.data)
        free(const_cast<uint8_t *>(kmOutBlob.data));

    return Void();
}

Return<ErrorCode>  OpteeKeymasterDevice::abort(uint64_t operationHandle) {
    ErrorCode rc = ErrorCode::OK;
    int inSize = sizeof(operationHandle);
    std::unique_ptr<uint8_t[]> in(new uint8_t[inSize]);
    if (!checkConnection(rc))
        goto error;
    memset(in.get(), 0, inSize);
    memcpy(in.get(), &operationHandle, sizeof(operationHandle));
    rc = legacy_enum_conversion(
        optee_keystore_call(KM_ABORT, in.get(), inSize, nullptr, 0));

    if (rc != ErrorCode::OK)
        ALOGE("Abort failed with code %d [%x]", rc, rc);

error:
    return rc;
}

bool OpteeKeymasterDevice::connect() {
    if (is_connected_) {
        ALOGE("Keymaster device is already connected");
        return false;
    }
    if (!optee_keystore_connect()) {
        ALOGE("Fail to load Keystore TA");
        return false;
    }
    is_connected_ = true;
    ALOGV("Keymaster connected");
    return true;
}

void OpteeKeymasterDevice::disconnect() {
    if (is_connected_) {
        optee_keystore_disconnect();
        is_connected_ = false;
        ALOGV("Keymaster has been disconnected");
    }
    else {
        ALOGE("Keymaster allready disconnected");
    }
}

bool OpteeKeymasterDevice::checkConnection(ErrorCode &rc) {
    if (!is_connected_) {
        ALOGE("Keymaster is not connected");
        rc = ErrorCode::SECURE_HW_COMMUNICATION_FAILED;
    }
    return is_connected_;
}

int OpteeKeymasterDevice::getParamSetBlobSize(const KmParamSet &paramSet) {
    int size = 0;
    for (size_t i = 0; i < paramSet.length; i++) {
        if (keymaster_tag_get_type(
                paramSet.params[i].tag) == KM_BIGNUM ||
                keymaster_tag_get_type(paramSet.params[i].tag) == KM_BYTES) {
            size += paramSet.params[i].blob.data_length + sizeof(size_t);
        }
    }
    return size;
}

int OpteeKeymasterDevice::getParamSetSize(const KmParamSet &paramSet) {
    int size = 0;
    size += getParamSetBlobSize(paramSet) + sizeof(paramSet.length) +
        paramSet.length * SIZE_OF_ITEM(paramSet.params);
    return size;
}

int OpteeKeymasterDevice::getBlobSize(const keymaster_blob_t &blob) {
    int size = 0;
    size += blob.data_length * SIZE_OF_ITEM(blob.data) + sizeof(size_t);
    return size;
}

int OpteeKeymasterDevice::getKeyBlobSize(const keymaster_key_blob_t &keyBlob) {
    int size = 0;
    size += keyBlob.key_material_size *
        SIZE_OF_ITEM(keyBlob.key_material) + sizeof(size_t);
    return size;
}

/****************************************************************************
 *             Functions for serialization base KM types                    *
 ****************************************************************************/

int OpteeKeymasterDevice::serializeData(uint8_t *dest, const size_t count,
                                    const uint8_t *source, const size_t objSize) {
    memcpy(dest, &count, sizeof(count));
    dest += sizeof(count);
    memcpy(dest, source, count * objSize);
    return sizeof(count) + count * objSize;
}

int OpteeKeymasterDevice::serializeSize(uint8_t *dest, const size_t size) {
    memcpy(dest, &size, sizeof(size));
    return sizeof(size);
}

int OpteeKeymasterDevice::serializeParamSet(uint8_t *dest,
                                const KmParamSet &paramSet) {
    uint8_t *start = dest;
    dest += serializeSize(dest, paramSet.length);
    for (size_t i = 0; i < paramSet.length; i++) {
        memcpy(dest, &paramSet.params[i], SIZE_OF_ITEM(paramSet.params));
        dest += SIZE_OF_ITEM(paramSet.params);
        if (keymaster_tag_get_type(paramSet.params[i].tag) == KM_BIGNUM ||
                keymaster_tag_get_type(paramSet.params[i].tag) == KM_BYTES) {
                    dest += serializeData(dest, paramSet.params[i].blob.data_length,
                    paramSet.params[i].blob.data,
                    SIZE_OF_ITEM(paramSet.params[i].blob.data));
        }
    }
    return dest - start;
}

int OpteeKeymasterDevice::serializePresence(uint8_t *dest, const presence p) {
    memcpy(dest, &p, sizeof(presence));
    return sizeof(presence);
}

int OpteeKeymasterDevice::serializeParamSetWithPresence(uint8_t *dest,
                       const KmParamSet &params) {
    uint8_t *start = dest;
    dest += serializePresence(dest, KM_POPULATED);
    dest += serializeParamSet(dest, params);
    return dest - start;
}

int OpteeKeymasterDevice::serializeBlobWithPresenceInfo(uint8_t *dest, 
                    const keymaster_blob_t &blob, bool presence) {
    uint8_t *start = dest;
    if (presence) {
        dest += serializePresence(dest, KM_POPULATED);
        dest += serializeData(dest, blob.data_length, blob.data,
                                    SIZE_OF_ITEM(blob.data));
    } else {
        dest += serializePresence(dest, KM_NULL);
    }
    return dest - start;                  
}

int OpteeKeymasterDevice::serializeKeyFormat(uint8_t *dest,
                const keymaster_key_format_t &keyFormat) {
    memcpy(dest, &keyFormat, sizeof(keyFormat));
    return sizeof(keyFormat);
}

/****************************************************************************
 *             Functions for deserialization base KM types                  *
 ****************************************************************************/

int OpteeKeymasterDevice::deserializeSize(size_t &size, const uint8_t *source) {
    memcpy(&size, source, sizeof(size));
    return sizeof(size);
}

int OpteeKeymasterDevice::deserializeKeyBlob(keymaster_key_blob_t &keyBlob,
                                const uint8_t *source, ErrorCode &rc) {
    size_t size = 0;
    uint8_t *material = nullptr;
    const uint8_t *start = source;

    source += deserializeSize(size, source);
    keyBlob.key_material_size = size;
    material = new (std::nothrow) uint8_t[keyBlob.key_material_size];
    if (!material) {
        rc = ErrorCode::MEMORY_ALLOCATION_FAILED;
        goto error;
    }
    memcpy(material, source, keyBlob.key_material_size);
    source += keyBlob.key_material_size;
    keyBlob.key_material = material;
error:
    return source - start;
}

int OpteeKeymasterDevice::deserializeKeyCharacteristics(keymaster_key_characteristics_t &characteristics,
                        const uint8_t *source, ErrorCode &rc) {
    size_t size = 0;
    const uint8_t *start = source;

    source += deserializeSize(size, source);
    characteristics.hw_enforced.length = size;
    characteristics.hw_enforced.params =
               new (std::nothrow) keymaster_key_param_t[characteristics.hw_enforced.length];
    if (!characteristics.hw_enforced.params) {
        rc = ErrorCode::MEMORY_ALLOCATION_FAILED;
        goto error;
    }
    for (size_t i = 0; i < characteristics.hw_enforced.length; i++) {
        memcpy(&characteristics.hw_enforced.params[i],
            source, SIZE_OF_ITEM(characteristics.hw_enforced.params));
            source += SIZE_OF_ITEM(characteristics.hw_enforced.params);
        if (keymaster_tag_get_type(
             characteristics.hw_enforced.params[i].tag) == KM_BIGNUM ||
             keymaster_tag_get_type(characteristics.hw_enforced.params[i].tag)
             == KM_BYTES) {
            source += deserializeBlob(characteristics.hw_enforced.params[i].blob,
                        source, rc);
            if (rc != ErrorCode::OK)
                    goto error;
        }
    }
    memcpy(&characteristics.sw_enforced.length, source,
                       sizeof(characteristics.sw_enforced.length));
    source += sizeof(characteristics.sw_enforced.length);
    characteristics.sw_enforced.params =
               new (std::nothrow) keymaster_key_param_t[characteristics.sw_enforced.length];
    if (!characteristics.sw_enforced.params) {
        rc = ErrorCode::MEMORY_ALLOCATION_FAILED;
        goto error;
    }
    for (size_t i = 0; i < characteristics.sw_enforced.length; i++) {
        memcpy(&characteristics.sw_enforced.params[i], source,
               SIZE_OF_ITEM(characteristics.sw_enforced.params));
               source += SIZE_OF_ITEM(characteristics.sw_enforced.params);
        if (keymaster_tag_get_type(
             characteristics.sw_enforced.params[i].tag) == KM_BIGNUM ||
             keymaster_tag_get_type(characteristics.sw_enforced.params[i].tag)
             == KM_BYTES) {
            source += deserializeBlob(characteristics.sw_enforced.params[i].blob,
                            source, rc);
            if (rc != ErrorCode::OK)
                    goto error;
        }
    }
error:
    return source - start;
}

int OpteeKeymasterDevice::deserializeBlob(keymaster_blob_t &blob,
                    const uint8_t *source, ErrorCode &rc) {
    size_t size = 0;
    uint8_t *data = nullptr;
    const uint8_t *start = source;

    source += deserializeSize(size, source);
    blob.data_length = size;
    data = new (std::nothrow) uint8_t[blob.data_length];
    if (!data) {
        ALOGE("Failed to allocate memory for blob");
        rc = ErrorCode::MEMORY_ALLOCATION_FAILED;
        goto error;
    }
    memcpy(data, source, blob.data_length);
    source += blob.data_length;
    blob.data = data;
error:
    return source - start;
}

int OpteeKeymasterDevice::deserializeParamSet(KmParamSet &params,
                    const uint8_t *source, ErrorCode &rc) {
    size_t size = 0;
    const uint8_t *start = source;

    source += deserializeSize(size, source);
    params.length = size;
    params.params = new (std::nothrow) keymaster_key_param_t[params.length];
    if (!params.params) {
        ALOGE("Failed to allocate memory for param set");
        rc = ErrorCode::MEMORY_ALLOCATION_FAILED;
        goto error;
    }
    for(size_t i = 0; i < params.length; i++) {
        memcpy(&params.params[i], source, SIZE_OF_ITEM(params.params));
        source += SIZE_OF_ITEM(params.params);
        if (keymaster_tag_get_type(
                params.params[i].tag) == KM_BIGNUM ||
                keymaster_tag_get_type(params.params[i].tag) == KM_BYTES) {
                    source += deserializeBlob(params.params[i].blob, source, rc);
            if (rc != ErrorCode::OK) {
                ALOGE("Failed to deserialize blob in param");
                goto error;
            }
        }
    }
error:
    return source - start;
}

}  // namespace renesas
}  // namespace V3_0
}  // namespace keymaster
}  // namespace hardware
}  // namespace android
