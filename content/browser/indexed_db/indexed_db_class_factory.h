// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_INDEXED_DB_INDEXED_DB_CLASS_FACTORY_H_
#define CONTENT_BROWSER_INDEXED_DB_INDEXED_DB_CLASS_FACTORY_H_

#include <stdint.h>

#include <memory>
#include <set>

#include "base/callback.h"
#include "base/lazy_instance.h"
#include "base/memory/ref_counted.h"
#include "content/browser/indexed_db/indexed_db_backing_store.h"
#include "content/browser/indexed_db/indexed_db_database.h"
#include "content/browser/indexed_db/scopes/scopes_lock_manager.h"
#include "content/common/content_export.h"
#include "third_party/blink/public/common/indexeddb/web_idb_types.h"
#include "third_party/blink/public/mojom/indexeddb/indexeddb.mojom.h"

namespace leveldb {
class Iterator;
class Snapshot;
}  // namespace leveldb

namespace content {

class IndexedDBBackingStore;
class IndexedDBConnection;
class IndexedDBFactory;
class IndexedDBTransaction;
class LevelDBDatabase;
class LevelDBIteratorImpl;
class LevelDBTransaction;

// Use this factory to create some IndexedDB objects. Exists solely to
// facilitate tests which sometimes need to inject mock objects into the system.
// TODO(dmurph): Remove this class in favor of dependency injection. This makes
// it really hard to iterate on the system.
class CONTENT_EXPORT IndexedDBClassFactory {
 public:
  typedef IndexedDBClassFactory* GetterCallback();
  // Used to report irrecoverable backend errors. The second argument can be
  // null.
  using ErrorCallback =
      base::RepeatingCallback<void(leveldb::Status, const char*)>;

  static IndexedDBClassFactory* Get();

  static void SetIndexedDBClassFactoryGetter(GetterCallback* cb);

  // See IndexedDBDatabase::Create.
  virtual std::unique_ptr<IndexedDBDatabase> CreateIndexedDBDatabase(
      const base::string16& name,
      IndexedDBBackingStore* backing_store,
      IndexedDBFactory* factory,
      IndexedDBDatabase::ErrorCallback error_callback,
      base::OnceClosure destroy_me,
      std::unique_ptr<IndexedDBMetadataCoding> metadata_coding,
      const IndexedDBDatabase::Identifier& unique_identifier,
      ScopesLockManager* transaction_lock_manager);

  // |error_callback| is used to report unrecoverable errors.
  virtual std::unique_ptr<IndexedDBTransaction> CreateIndexedDBTransaction(
      int64_t id,
      IndexedDBConnection* connection,
      ErrorCallback error_callback,
      const std::set<int64_t>& scope,
      blink::mojom::IDBTransactionMode mode,
      IndexedDBBackingStore::Transaction* backing_store_transaction);

  virtual std::unique_ptr<LevelDBIteratorImpl> CreateIteratorImpl(
      std::unique_ptr<leveldb::Iterator> iterator,
      LevelDBDatabase* db,
      const leveldb::Snapshot* snapshot);

  virtual scoped_refptr<LevelDBTransaction> CreateLevelDBTransaction(
      LevelDBDatabase* db);

 protected:
  IndexedDBClassFactory() {}
  virtual ~IndexedDBClassFactory() {}
  friend struct base::LazyInstanceTraitsBase<IndexedDBClassFactory>;
};

}  // namespace content

#endif  // CONTENT_BROWSER_INDEXED_DB_INDEXED_DB_CLASS_FACTORY_H_
