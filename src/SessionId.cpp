/*
 * Copyright (C) 2013-2014  Mozilla Foundation
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

#include "SessionId.h"

int SessionId::mId = 0;

int
SessionId::generateNewId() {
  return ++mId;
}

int
SessionId::getCurrentId() {
  return mId;
}

bool
SessionId::isValid(int id) {
  return mId == id;
}
