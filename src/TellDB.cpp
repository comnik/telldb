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
#include <telldb/TellDB.hpp>
#include <random>
#include <boost/lexical_cast.hpp>
#include "Indexes.hpp"

namespace tell {
namespace db {
namespace impl {

Indexes* createIndexes(store::ClientHandle& handle) {
    return new Indexes(handle);
}

TellDBContext::TellDBContext(ClientTable* table)
    : clientTable(table)
{}

void TellDBContext::setIndexes(Indexes* idxs) {
    indexes.reset(idxs);
}


void ClientTable::init(store::ClientHandle& handle) {
    std::random_device rd;
    std::uniform_int_distribution<uint64_t> dist;
    store::Schema schema(store::TableType::NON_TRANSACTIONAL);
    schema.addField(store::FieldType::BLOB, "value", true);
    schema.addField(store::FieldType::HASH128, "__partition_token", false);

    while (true) {
        auto clientsTableResp = handle.getTable("__clients");
        if (clientsTableResp->error()) {
            // table does not yet exist
            try {
                mClientsTable.reset(new store::Table(handle.createTable("__clients", schema)));
            } catch (std::system_error& e) {
                continue;
            }
        } else {
            mClientsTable.reset(new store::Table(clientsTableResp->get()));
        }
        break;
    }
    while (true) {
        // the client id is simply a random number
        mClientId = dist(rd);
        // try to insert the client
        auto tuple = store::GenericTuple({
            std::make_pair<crossbow::string, boost::any>("value", crossbow::string())
        });
        auto insertResp = handle.insert(*mClientsTable, mClientId, 0, tuple);
        if (!insertResp->error()) {
            break;
        }
    }
    mTransactionsTable.reset(new store::Table(handle.createTable("__transactions_" +
                    boost::lexical_cast<crossbow::string>(mClientId),
                    schema)));
}

void ClientTable::destroy(store::ClientHandle& handle) {
    // TODO: drop table
    // TODO: delete entry from ClientTable
    //
    // Currently the interface does not allow to delete
    // Tables. Therefore we will just keep it - this will
    // work but it leaks resources
}

} // namespace impl

} // namespace db
} // namespace tell

