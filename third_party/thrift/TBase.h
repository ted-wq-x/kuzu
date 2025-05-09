/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements. See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership. The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef _KUZU_THRIFT_TBASE_H_
#define _KUZU_THRIFT_TBASE_H_ 1

#include "Thrift.h"
#include "protocol/TProtocol.h"

namespace kuzu_apache {
namespace thrift {

class TBase {
public:
  virtual ~TBase() = default;
  virtual uint32_t read(protocol::TProtocol* iprot) = 0;
  virtual uint32_t write(protocol::TProtocol* oprot) const = 0;
};
}
} // kuzu_apache::thrift

#endif // #ifndef _KUZU_THRIFT_TBASE_H_
