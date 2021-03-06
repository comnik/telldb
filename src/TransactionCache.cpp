/*
 * (C) Copyright 2015 ETH Zurich Systems Group (http://www.systems.ethz.ch/) and others.
 *
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
 *
 * Contributors:
 *     Markus Pilman <mpilman@inf.ethz.ch>
 *     Simon Loesing <sloesing@inf.ethz.ch>
 *     Thomas Etter <etterth@gmail.com>
 *     Kevin Bocksrocker <kevin.bocksrocker@gmail.com>
 *     Lucas Braun <braunl@inf.ethz.ch>
 */
#include "TransactionCache.hpp"
#include "TableCache.hpp"
#include "Indexes.hpp"
#include "FieldSerialize.hpp"
#include <telldb/TellDB.hpp>
#include <telldb/Exceptions.hpp>
#include <tellstore/ClientManager.hpp>
#include <crossbow/Serializer.hpp>

using namespace tell::store;

namespace tell {
namespace db {
using namespace impl;

Future<table_t>::Future(std::shared_ptr<GetTableResponse>&& resp, TransactionCache& cache)
    : resp(resp)
    , cache(cache)
{
}

bool Future<table_t>::done() const {
    if (!resp) {
        return true;
    }
    return resp->done();
}

bool Future<table_t>::wait() const {
    if (!resp) {
        return false;
    }
    return resp->wait();
}

table_t Future<table_t>::get() {
    if (!resp) {
        return result;
    }
    auto table = resp->get();
    resp = nullptr;
    result = cache.addTable(std::move(table));
    return result;
}

Iterator TransactionCache::lower_bound(table_t tableId, const crossbow::string& idxName, const KeyType& key) {
    return mTables[tableId]->lower_bound(idxName, key);
}

Iterator TransactionCache::reverse_lower_bound(table_t tableId, const crossbow::string& idxName, const KeyType& key) {
    return mTables[tableId]->reverse_lower_bound(idxName, key);
}

TransactionCache::TransactionCache(TellDBContext& context,
        store::ClientHandle& handle,
        const commitmanager::SnapshotDescriptor& snapshot,
        crossbow::ChunkMemoryPool& pool)
    : context(context)
    , mHandle(handle)
    , mSnapshot(snapshot)
    , mPool(pool)
    , mTables(&pool)
{}

Future<table_t> TransactionCache::openTable(const crossbow::string& name) {
    auto iter = context.tableNames.find(name);
    if (iter != context.tableNames.end()) {
        auto tableId = iter->second;
        auto res = Future<table_t>(nullptr, *this);
        res.result.value = tableId.value;
        if (mTables.find(tableId) == mTables.end()) {
            const auto& t = *context.tables[res.result];
            addTable(t, context.indexes->openIndexes(mSnapshot, mHandle, t));
        }
        return res;
    }
    return Future<table_t>(mHandle.getTable(name), *this);
}

table_t TransactionCache::createTable(const crossbow::string& name, const store::Schema& schema) {
    auto table = mHandle.createTable(name, schema);
    table_t tableId{table.tableId()};
    context.tableNames.emplace(name, tableId);
    auto cTable = new Table(table);
    context.tables.emplace(tableId, cTable);
    mTables.emplace(tableId,
            new (&mPool) TableCache(*cTable,
                mHandle,
                mSnapshot,
                mPool,
                context.indexes->createIndexes(mSnapshot, mHandle, table)));
    return tableId;
}

Future<Tuple> TransactionCache::get(table_t table, key_t key) {
    auto cache = mTables.at(table);
    return cache->get(key);
}

void TransactionCache::insert(table_t table, key_t key, const Tuple& tuple) {
    mTables.at(table)->insert(key, tuple);
}

void TransactionCache::update(table_t table, key_t key, const Tuple& from, const Tuple& to) {
    mTables.at(table)->update(key, from, to);
}

void TransactionCache::remove(table_t table, key_t key, const Tuple& tuple) {
    mTables.at(table)->remove(key, tuple);
}

TransactionCache::~TransactionCache() {
    for (auto& p : mTables) {
        delete p.second;
    }
}

table_t TransactionCache::addTable(const tell::store::Table& table,
        std::unordered_map<crossbow::string,
        impl::IndexWrapper>&& indexes) {
    table_t id { table.tableId() };
    mTables.emplace(id, new (&mPool) TableCache(table, mHandle, mSnapshot, mPool, std::move(indexes)));
    return id;
}

table_t TransactionCache::addTable(tell::store::Table table) {
    auto indexes = context.indexes->openIndexes(mSnapshot, mHandle, table);
    table_t res{table.tableId()};
    Table* t = nullptr;
    auto iter = context.tables.find(res);
    if (iter == context.tables.end()) {
        context.tableNames.emplace(table.tableName(), res);
        auto p = context.tables.emplace(res, new Table(std::move(table)));
        t = p.first->second;
    } else {
        t = iter->second;
    }
    return addTable(*t, std::move(indexes));
}

void TransactionCache::rollback() {
    for (auto p : mTables) {
        p.second->rollback();
    }
}

void TransactionCache::writeBack() {
    for (auto p : mTables) {
        p.second->writeBack();
    }
}

void TransactionCache::writeIndexes() {
    for (auto p : mTables) {
        p.second->writeIndexes();
    }
}

bool TransactionCache::hasChanges() const {
    for (const auto& t : mTables) {
        if (t.second->changes().size() != 0) {
            return true;
        }
    }
    return false;
}

template<class A>
void TransactionCache::applyForLog(A& ar, bool withIndexes) const {
    for (const auto& t : mTables) {
        ar & t.first;
        const auto& cs = t.second->changes();
        uint32_t numChanges = cs.size();
        ar & numChanges;
        for (const auto& c : cs) {
            ar & c.first;
        }
        if (withIndexes) {
            const auto& indexes = t.second->indexes();
            ar & indexes.size();
            for (const auto& idx : indexes) {
                ar & idx.first;
                ar & idx.second.cache();
            }
        }
    }
}

std::pair<size_t, uint8_t*> TransactionCache::undoLog(bool withIndexes) const {
    crossbow::sizer s;
    applyForLog(s, withIndexes);
    auto res = reinterpret_cast<uint8_t*>(mPool.allocate(s.size));
    crossbow::serializer ser(res);
    applyForLog(ser, withIndexes);
    ser.buffer.release();
    return std::make_pair(s.size, res);
}

const store::Record& TransactionCache::record(table_t table) const {
    return mTables.at(table)->table().record();
}

template class Future<table_t>;
} // namespace db
} // namespace tell

