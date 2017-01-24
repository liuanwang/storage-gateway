#ifndef _BACKUP_TYPE_H
#define _BACKUP_TYPE_H

#include <string>
#include <set>
#include "../rpc/common.pb.h"
#include "../rpc/backup.pb.h"
using huawei::proto::BackupStatus;
using huawei::proto::BackupMode;
using namespace std;

/*backup id*/
typedef uint64_t backupid_t;
/*backup data object name*/
typedef string backup_object_t;
/*backup data object backup reference list*/
typedef set<backupid_t> backup_object_ref_t;

/*backup attribution*/
struct backup_attr {
    string volume_uuid;
    
    BackupMode    backup_mode;
    string        backup_name;
    backupid_t    backup_id;
    BackupStatus  backup_status;
};
typedef struct backup_attr backup_attr_t;

/*backup block size(4MB)*/
#define BACKUP_BLOCK_SIZE (1*1024*1024UL)

#define BACKUP_FS "#"
#define BACKUP_OBJ_SUFFIX ".backupobj"

/*backup indexstore key prefix*/
#define BACKUP_ID_PREFIX      "backup_latestid"
#define BACKUP_NAME_PREFIX    "backup_latestname"
#define BACKUP_MAP_PREFIX     "backup_map_prefix"
#define BACKUP_BLOCK_PREFIX   "backup_block_prefix"

#endif