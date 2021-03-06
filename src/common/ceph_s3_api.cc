/**********************************************
* Copyright (c) 2016 Huawei Technologies Co., Ltd. All rights reserved.
* 
* File name:    ceph_s3_api.cc
* Author: 
* Date:         2016/08/08
* Version:      1.0
* Description:
* 
************************************************/
#include <stdlib.h>
#include "ceph_s3_api.h"
#include "log/log.h"

#ifndef SLEEP_UNITS_PER_SECOND
#define SLEEP_UNITS_PER_SECOND 1
#endif
#ifndef MAX_RETRIES
#define MAX_RETRIES 5
#endif
using huawei::proto::sOk;
using huawei::proto::sInternalError;
using huawei::proto::sNotFound;

static int should_retry(s3_call_response_t &response)
{
    if (response.retries--) {
        // Sleep before next retry; start out with a 1 second sleep
        sleep(response.retrySleepInterval);
        // Next sleep 1 second longer
        response.retrySleepInterval++;
        LOG_WARN << "retry s3 api since last operation failed:"
            << S3_get_status_name(response.status);
        return 1;
    }
    return 0;
}

S3Status responsePropertiesCallback(
        const S3ResponseProperties *properties, void *callbackData){
    if(properties->metaDataCount <= 0 || nullptr == callbackData)
        return S3StatusOK;
    s3_call_response_t* response = (s3_call_response_t*) callbackData;
    if(nullptr != response->meta_data){
        std::map<string,string>* map = (std::map<string,string>*)response->meta_data;
        for (int i = 0; i < properties->metaDataCount; i++) {
            map->insert(std::pair<string,string>(properties->metaData[i].name,
                   properties->metaData[i].value));
            LOG_DEBUG << "object head meta:" << properties->metaData[i].name <<
                   ":" << properties->metaData[i].value;
        }
        map->insert(std::make_pair("last-modified",
                std::to_string(properties->lastModified)));
    }
    return S3StatusOK;
}
static void responseCompleteCallback(S3Status status,
        const S3ErrorDetails *error, void *callbackData){
    if(nullptr == callbackData)
        return;
    s3_call_response_t* response = (s3_call_response_t*) callbackData;
    response->status = status;
    if(status != S3StatusOK && error != nullptr && error->message){
        LOG_ERROR << error->message;
    }
    return;
}

S3ResponseHandler responseHandler = {
   &responsePropertiesCallback,
   &responseCompleteCallback
};

static int putObjectDataCallback(int bufferSize, char *buffer,
        void *callbackData){
    s3_call_response_t* response = (s3_call_response_t*)callbackData;
    if(NULL == response->pdata){
        return 0;
    }
    string* meta_s = (string*)(response->pdata);
    int res = 0;
    if(response->size > 0) {
        res = (response->size > bufferSize) ? bufferSize:response->size;
        memcpy(buffer,meta_s->c_str()+(meta_s->length()-response->size),res);
        response->size -= res;
    }
    return res;
}

static S3Status getObjectDataCallback(int bufferSize, const char *buffer, void *callbackData)
{
    string *value = (string*)(((s3_call_response_t*)callbackData)->pdata);
    value->append(buffer,bufferSize);
    return S3StatusOK;
}

// this callback may be called more than once during a list request
static S3Status listBucketCallback(int isTruncated, const char *nextMarker,
        int contentsCount, const S3ListBucketContent *contents, int commonPrefixesCount,
        const char **commonPrefixes, void *callbackData){
    s3_call_response_t* response = (s3_call_response_t*)callbackData;
    response->isTruncated = isTruncated;
    // This is tricky.  S3 doesn't return the NextMarker if there is no
    // delimiter. since it's still useful for paging through results. 
    // We want NextMarker to be the last content in the list,
    // so set it to that if necessary.
    if ((!nextMarker || !nextMarker[0]) && contentsCount) {
        nextMarker = contents[contentsCount - 1].key;
    }
    if (nextMarker) {
        snprintf(response->nextMarker, sizeof(response->nextMarker), "%s", nextMarker);
    }
    else {
        response->nextMarker[0] = 0;
    }
    std::list<string>* list = (std::list<string>*)(response->pdata);
    LOG_DEBUG << "list objects: " << contentsCount 
        << ",commonPrefixesCnt: " << commonPrefixesCount;
    for (int i = 0; i < contentsCount; i++) {
        const S3ListBucketContent *content = &(contents[i]);
        string key(content->key);
        list->push_back(key);
        LOG_DEBUG << content->key;
    }
    response->keyCount += contentsCount;
    for (int i = 0; i < commonPrefixesCount; i++) {
        string key(commonPrefixes[i]);
        list->push_back(key);
        LOG_DEBUG << key;
    }
    response->keyCount += commonPrefixesCount;
    return S3StatusOK;
}

CephS3Api::CephS3Api(const char* access_key, const char* secret_key,
        const char* host, const char* bucket_name):KVApi() {
    strncpy(access_key_,access_key,BUFF_LEN);
    strncpy(secret_key_,secret_key,BUFF_LEN);
    strncpy(host_,host,BUFF_LEN);
    strncpy(bucket_,bucket_name,BUFF_LEN);
    bucketContext.accessKeyId = access_key_;
    bucketContext.secretAccessKey = secret_key_;
    bucketContext.hostName = host_;
    bucketContext.bucketName = bucket_;
    bucketContext.protocol = S3ProtocolHTTP;
    bucketContext.uriStyle = S3UriStylePath;
    S3Status status = S3_initialize("s3", S3_INIT_ALL, host);
    if(status != S3StatusOK) {
        LOG_FATAL << "Failed to initialize libs3:: " << S3_get_status_name(status);
        exit(EXIT_FAILURE);
    }
    create_bucket_if_not_exists(bucket_name);
}
CephS3Api::~CephS3Api() {
    S3_deinitialize();
}

StatusCode CephS3Api::create_bucket_if_not_exists(const char* bucket_name) {
    s3_call_response_t response;
    response.status = S3StatusHttpErrorUnknown;
    response.retries = MAX_RETRIES;
    response.retrySleepInterval = SLEEP_UNITS_PER_SECOND;
    do{
        S3_test_bucket(bucketContext.protocol,bucketContext.uriStyle,
                bucketContext.accessKeyId,bucketContext.secretAccessKey,
                bucketContext.hostName, bucket_name,0,NULL,NULL,
                &responseHandler,&response);
    }while(S3_status_is_retryable(response.status)
                && should_retry(response));
    if(S3StatusOK == response.status) {
        return sOk; // bucket exists
    }
    else if(S3StatusErrorNoSuchBucket != response.status) {
        LOG_ERROR << "test bucket:" << bucket_name << " failed:"
            <<  S3_get_status_name(response.status);
        return sInternalError;
    }
    response.retries = MAX_RETRIES;
    response.retrySleepInterval = SLEEP_UNITS_PER_SECOND;
    do{
        S3_create_bucket(bucketContext.protocol, bucketContext.accessKeyId,
                bucketContext.secretAccessKey,bucketContext.hostName, bucket_name,
                S3CannedAclPrivate, NULL, NULL, &responseHandler, &response);
    }while(S3_status_is_retryable(response.status)
                && should_retry(response));
    if(S3StatusOK != response.status) {
        LOG_ERROR << "create bucket:" << bucket_name << " failed:"
            <<  S3_get_status_name(response.status);
        return sInternalError;
    }
    return sOk;
}

StatusCode CephS3Api::put_object(const char* obj_name, const string* value,
            const std::map<string,string>* meta) {
    S3PutObjectHandler putObjectHandler =
    {
        responseHandler,
        &putObjectDataCallback
    };
    int64_t len = 0;
    if(nullptr != value)
        len = value->length();
    s3_call_response_t response;
    response.pdata = (void*)(value);
    response.size = len;
    response.retries = MAX_RETRIES;
    response.retrySleepInterval = SLEEP_UNITS_PER_SECOND;
    LOG_DEBUG << "put object " << obj_name << ",length: " << len;
    S3PutProperties properties;
    memset(&properties,0,sizeof(S3PutProperties));
    if(meta != nullptr && !meta->empty()){
        properties.metaDataCount = meta->size();
        S3NameValue* meta_value = new S3NameValue[properties.metaDataCount];
        int i=0;
        for(auto it=meta->begin();it!=meta->end();it++,i++){
            meta_value[i].name = (it->first).c_str();
            meta_value[i].value = (it->second).c_str();
        }
        properties.metaData = meta_value;
    }
    do {
        S3_put_object(&bucketContext,obj_name,len,&properties,
                nullptr,&putObjectHandler,&response);
    } while(S3_status_is_retryable(response.status)
                && should_retry(response));
    if(properties.metaData){
        delete [] properties.metaData;
    }
    if(S3StatusOK != response.status){
        LOG_ERROR << "create object " << obj_name << " failed:"
            << S3_get_status_name(response.status);
        return sInternalError;
    }
    return sOk;
}

StatusCode CephS3Api::get_object(const char* key, string* value){
    S3GetObjectHandler getObjectHandler =
    {
        responseHandler,
        &getObjectDataCallback
    };
    s3_call_response_t response;
    response.pdata = value;
    response.meta_data = nullptr;
    response.retries = MAX_RETRIES;
    response.retrySleepInterval = SLEEP_UNITS_PER_SECOND;
    do {
        S3_get_object(&bucketContext, key, NULL, 0, 0, NULL,
                &getObjectHandler, &response);
    } while(S3_status_is_retryable(response.status)
                && should_retry(response));
    if(S3StatusOK != response.status) {
        if(S3StatusErrorNoSuchKey == response.status)
            return sNotFound;
        LOG_ERROR << "get object " << key << " failed:"
            << S3_get_status_name(response.status);
        return sInternalError;
    }
    return sOk;
}

StatusCode CephS3Api::head_object(const char* key,
        std::map<string,string>* meta){
    s3_call_response_t response;
    response.retries = MAX_RETRIES;
    response.retrySleepInterval = SLEEP_UNITS_PER_SECOND;
    response.meta_data = (void*)(meta);
    do {
        S3_head_object(&bucketContext, key, 0, &responseHandler, &response);
    } while (S3_status_is_retryable(response.status) && should_retry(response));
    if(S3StatusOK != response.status) {
        LOG_ERROR << "head object " << key << " failed:" << S3_get_status_name(response.status);
        return sInternalError;
    }
    return sOk;
}

StatusCode CephS3Api::delete_object(const char* key) {
    S3ResponseHandler deleteResponseHandler = {
        0,
        &responseCompleteCallback
    };
    s3_call_response_t response;
    response.retries = MAX_RETRIES;
    response.retrySleepInterval = SLEEP_UNITS_PER_SECOND;
    do {
        S3_delete_object(&bucketContext, key, NULL, &deleteResponseHandler, &response);
    } while(S3_status_is_retryable(response.status)
                && should_retry(response));
    if(S3StatusErrorNoSuchKey == response.status)
        return sOk; // object key not found
    if(S3StatusOK != response.status) {
        LOG_ERROR << "delete object " << key << " failed:" << S3_get_status_name(response.status);
        return sInternalError;
    }
    return sOk;
}
// if maxkeys <= 0, return all matched keys
StatusCode CephS3Api::list_objects(const char*prefix, const char*marker,
            int maxkeys, std::list<string>* list,const char* delimiter) {
    S3ListBucketHandler listBucketHandler =
    {
        responseHandler,
        &listBucketCallback
    };
    s3_call_response_t response;
    response.pdata = (void*)list;
    response.retries = MAX_RETRIES;
    response.retrySleepInterval = SLEEP_UNITS_PER_SECOND;
    response.keyCount = 0;
    response.isTruncated = 0;
    if(NULL!=marker && 0!=marker[0]){
        snprintf(response.nextMarker, sizeof(response.nextMarker), "%s", marker);
    }
    else {
        response.nextMarker[0] = 0;
    }
    do {
        do {
            S3_list_bucket(&bucketContext,prefix,response.nextMarker,
                delimiter,maxkeys,NULL,&listBucketHandler,&response);
        } while(S3_status_is_retryable(response.status)
                && should_retry(response));
        if(response.status != S3StatusOK)
            break;
    } while(response.isTruncated && (maxkeys<=0 || (response.keyCount < maxkeys)));
    if(S3StatusOK != response.status){
        LOG_ERROR << "list object failed: " << S3_get_status_name(response.status);
        return sInternalError;
    }
    return sOk;
}
