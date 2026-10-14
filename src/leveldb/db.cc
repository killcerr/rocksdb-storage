// Provided here instead of vendoring db/db_impl.cc: leveldb::DB has an
// out-of-line virtual destructor (its key function), so a subclass deleting
// dtor needs the symbol even though the mod never links leveldb.

#include "leveldb/db.h"

namespace leveldb {

DB::~DB() = default;

}  // namespace leveldb
