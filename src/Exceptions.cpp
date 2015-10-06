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
#include <telldb/Exceptions.hpp>
#include <boost/lexical_cast.hpp>

namespace tell {
namespace db {

// KeyException
key_t KeyException::key() const noexcept {
    return mKey;
}

const char* KeyException::what() const noexcept {
    return mMsg.c_str();
}

// TupleExistsException
TupleExistsException::TupleExistsException(key_t key)
    : KeyException(key, "Key " + boost::lexical_cast<crossbow::string>(key) + " already exists")
{}


// TupleDoesNotExist
TupleDoesNotExist::TupleDoesNotExist(key_t key)
    : KeyException(key, "Key " + boost::lexical_cast<crossbow::string>(key) + " does not exists or got deleted")
{}

// Conflict
Conflict::Conflict(key_t key)
    : KeyException(key, "Conflict on " + boost::lexical_cast<crossbow::string>(key))
{}

} // namespace db
} // namespace tell
