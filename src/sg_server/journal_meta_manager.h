/**********************************************
* Copyright (c) 2016 Huawei Technologies Co., Ltd. All rights reserved.
* 
* File name:    journal_meta_manager.h
* Author: 
* Date:         2016/07/11
* Version:      1.0
* Description:
* 
***********************************************/
#ifndef JOURNAL_META_MANAGER_H_
#define JOURNAL_META_MANAGER_H_
#include <string>
#include <list>
#include "rpc/common.pb.h"
#include "rpc/journal.pb.h"
#include "rpc/consumer.pb.h"
#include "rpc/writer.pb.h"
using huawei::proto::JOURNAL_STATUS;
using huawei::proto::CONSUMER_TYPE;
using huawei::proto::JournalMarker;
using huawei::proto::RESULT;
using huawei::proto::JournalElement;
using std::string;
// this uuid is used by dr site to create/seal journals of secodary volumes
// GCTask will ignore this uuid, since that there is no need of lease in the
// same process
const string g_replicator_uuid = "replicator_uuid";

class JournalMetaManager {
public:
    virtual ~JournalMetaManager() {}

    virtual RESULT create_journals(const string& uuid,
            const string& vol_id,
            const int& limit, std::list<JournalElement> &list) = 0;

    virtual RESULT create_journals_by_given_keys(const string& uuid,
            const string& vol_id,const std::list<string> &list) = 0;
    
    virtual RESULT seal_volume_journals(const string& uuid,
            const string& vol_id,
            const string journals[],const int& count) = 0;

    virtual RESULT get_consumer_marker(const string& vol_id,
            const CONSUMER_TYPE& type,JournalMarker& marker) = 0;

    virtual RESULT update_consumer_marker(const string& vol_id,
            const CONSUMER_TYPE& type,
            const JournalMarker& marker) = 0;

    virtual RESULT get_consumable_journals(const string& vol_id,
            const JournalMarker& marker,const int& limit,
            std::list<JournalElement> &list,
            const CONSUMER_TYPE& type) = 0;

    virtual RESULT set_producer_marker(const string& vol_id,
            const JournalMarker& marker) = 0;

    virtual RESULT get_journal_meta(const string& key,
            huawei::proto::JournalMeta& meta) = 0;

    virtual RESULT get_producer_marker(const string& vol_id,
            const CONSUMER_TYPE& type, JournalMarker& marker) = 0;

    virtual int compare_journal_key(const string& key1,
            const string& key2) = 0;

    virtual int compare_marker(const JournalMarker& m1,
            const JournalMarker& m2) = 0;

};
#endif
