// Copyright 2020 MozoLM Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "mozolm/utf8_util.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::ElementsAre;

namespace mozolm {
namespace utf8 {
namespace {

TEST(Utf8UtilTest, CheckStrSplitByChar) {
  EXPECT_THAT(utf8::StrSplitByChar("abcdefg"), ElementsAre(
      "a", "b", "c", "d", "e", "f", "g"));
  EXPECT_THAT(utf8::StrSplitByChar("Բարեւ"), ElementsAre(
      "Բ", "ա", "ր", "ե", "ւ"));
  EXPECT_THAT(utf8::StrSplitByChar("ባህሪ"), ElementsAre(
      "ባ", "ህ", "ሪ"));
  EXPECT_THAT(utf8::StrSplitByChar("ස්වභාවය"), ElementsAre(
      "ස", "්", "ව", "භ", "ා", "ව", "ය"));
  EXPECT_THAT(utf8::StrSplitByChar("მოგესალმებით"), ElementsAre(
      "მ", "ო", "გ", "ე", "ს", "ა", "ლ", "მ", "ე", "ბ", "ი", "თ"));
  EXPECT_THAT(utf8::StrSplitByChar("ຍິນດີຕ້ອນຮັບ"), ElementsAre(
      "ຍ", "ິ", "ນ", "ດ", "ີ", "ຕ", "້", "ອ", "ນ", "ຮ", "ັ", "ບ"));
}

}  // namespace
}  // namespace utf8
}  // namespace mozolm