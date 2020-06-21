// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/services/ime/public/cpp/rulebased/def/ml_phone.h"

#include "base/stl_util.h"

namespace ml_phone {

const char* kId = "ml_phone";
bool kIs102 = false;
const char* kTransforms[] = {
    u8"\\\\([a-zA-Z0-9@$])",
    u8"\\1",
    u8"([a-zA-Z])\u001d([a-zA-Z0-9`~!@#$%^&*()_=+:;\"',<.>?/|\\-])",
    u8"\\1\\2",
    u8"\\\\(ch)",
    u8"\\1",
    u8"([a-zA-Z])\u001d(ch)",
    u8"\\1\\2",
    u8"(M|\u001d?_M|\u0d02\u001d?m)",
    u8"\u0d2e\u0d4d\u0d2e\u0d4d",
    u8"([\u0d03-\u0d0a\u0d0c-\u0d4c\u0d4e-\u0d79])\u001d?R",
    u8"\\1\u0d7c",
    u8"([\u0d02-\u0d4c\u0d4e-\u0d79])\u001d?~",
    u8"\\1\u0d4d",
    u8"([\u0d02-\u0d4c\u0d4e-\u0d7f])\u0d7b\u001d?j",
    u8"\\1\u0d1e\u0d4d\u0d1e\u0d4d",
    u8"([\u0d15-\u0d3a])\u001d?a",
    u8"\\1\u0d3e",
    u8"([\u0d15-\u0d3a])\u001d?i",
    u8"\\1\u0d48",
    u8"([\u0d15-\u0d3a])\u001d?u",
    u8"\\1\u0d57",
    u8"([\u0d66-\u0d6f])\u001d?0",
    u8"\\1\u0d66",
    u8"([\u0d66-\u0d6f])\u001d?1",
    u8"\\1\u0d67",
    u8"([\u0d66-\u0d6f])\u001d?2",
    u8"\\1\u0d68",
    u8"([\u0d66-\u0d6f])\u001d?3",
    u8"\\1\u0d69",
    u8"([\u0d66-\u0d6f])\u001d?4",
    u8"\\1\u0d6a",
    u8"([\u0d66-\u0d6f])\u001d?5",
    u8"\\1\u0d6b",
    u8"([\u0d66-\u0d6f])\u001d?6",
    u8"\\1\u0d6c",
    u8"([\u0d66-\u0d6f])\u001d?7",
    u8"\\1\u0d6d",
    u8"([\u0d66-\u0d6f])\u001d?8",
    u8"\\1\u0d6e",
    u8"([\u0d66-\u0d6f])\u001d?9",
    u8"\\1\u0d6f",
    u8"([wv]|\u001d?_[wv])",
    u8"\u0d35\u0d4d",
    u8"(\u001d?R|\u0d0b\u001d?)A",
    u8"\u0d31\u0d3e",
    u8"(\u001d?R|\u0d0b\u001d?)E",
    u8"\u0d31\u0d47",
    u8"(\u001d?R|\u0d0b\u001d?)I",
    u8"\u0d31\u0d40",
    u8"(\u001d?R|\u0d0b\u001d?)U",
    u8"\u0d31\u0d42",
    u8"(\u001d?R|\u0d0b\u001d?)a",
    u8"\u0d31",
    u8"(\u001d?R|\u0d0b\u001d?)e",
    u8"\u0d31\u0d46",
    u8"(\u001d?R|\u0d0b\u001d?)i",
    u8"\u0d31\u0d3f",
    u8"(\u001d?R|\u0d0b\u001d?)u",
    u8"\u0d31\u0d41",
    u8"(\u001d?_)?B",
    u8"\u0d2c\u0d4d\u0d2c\u0d4d",
    u8"(\u001d?_)?D",
    u8"\u0d21\u0d4d",
    u8"(\u001d?_)?G",
    u8"\u0d17\u0d4d\u0d17\u0d4d",
    u8"(\u001d?_)?J",
    u8"\u0d1c\u0d4d\u0d1c\u0d4d",
    u8"(\u001d?_)?K",
    u8"\u0d15\u0d4d\u0d15\u0d4d",
    u8"(\u001d?_)?P",
    u8"\u0d2a\u0d4d\u0d2a\u0d4d",
    u8"(\u001d?_)?Y",
    u8"\u0d2f\u0d4d\u0d2f\u0d4d",
    u8"(\u001d?_)?Z",
    u8"\u0d34\u0d4d",
    u8"(\u001d?_)?T",
    u8"\u0d1f\u0d4d",
    u8"(\u001d?_)?[Sz]",
    u8"\u0d36\u0d4d",
    u8"(\u001d?_)?[VW]",
    u8"\u0d35\u0d4d\u0d35\u0d4d",
    u8"(\u001d?_)?[Cc]",
    u8"\u0d1a\u0d4d",
    u8"(\u001d?_)?[Xx]",
    u8"\u0d15\u0d4d\u0d38\u0d4d",
    u8"(\u001d?_)?b",
    u8"\u0d2c\u0d4d",
    u8"(\u001d?_)?d",
    u8"\u0d26\u0d4d",
    u8"(\u001d?_)?g",
    u8"\u0d17\u0d4d",
    u8"(\u001d?_)?h",
    u8"\u0d39\u0d4d",
    u8"(\u001d?_)?j",
    u8"\u0d1c\u0d4d",
    u8"(\u001d?_)?p",
    u8"\u0d2a\u0d4d",
    u8"(\u001d?_)?s",
    u8"\u0d38\u0d4d",
    u8"(\u001d?_)?y",
    u8"\u0d2f\u0d4d",
    u8"(\u0d05\u001d?a|_?A)",
    u8"\u0d06",
    u8"(\u0d07\u001d?i|\u0d0e\u001d?a|_?I|[\u0d0e\u0d07]\u001d?e)",
    u8"\u0d08",
    u8"(\u0d09\u001d?u|\u0d12\u001d?o|_?U)",
    u8"\u0d0a",
    u8"(\u0d0b\u001d?|\u001d?R)O",
    u8"\u0d31\u0d4b",
    u8"(\u0d0b\u001d?|\u001d?R)o",
    u8"\u0d31\u0d4a",
    u8"(\u0d0b\u001d?|\u001d?R)~",
    u8"\u0d31\u0d4d",
    u8"(\u0d15\u0d4d\u001d?h|\u001d?_[qQ]|[qQ])",
    u8"\u0d16\u0d4d",
    u8"(\u0d15\u0d4d|\u0d7f)\u001d?\\^",
    u8"\u0d15\u0d4d\u200d",
    u8"(\u0d1f\u0d4d\u001d?t|\u0d31\u0d4d\u0d31\u0d4d\u001d?[tT])",
    u8"\u0d1f\u0d4d\u0d1f\u0d4d",
    u8"(\u0d28\u0d4d\u001d?T|\u0d7a\u001d?[Tt])",
    u8"\u0d23\u0d4d\u0d1f\u0d4d",
    u8"(\u0d23\u0d4d\u001d?t|\u0d7b\u001d?T)",
    u8"\u0d23\u0d4d\u0d1f\u0d4d",
    u8"(\u0d23\u0d4d|\u0d7a)\u001d?\\^",
    u8"\u0d23\u0d4d\u200d",
    u8"(\u0d28\u0d4d\u001d?ch?|\u0d7b\u001d?ch?)",
    u8"\u0d1e\u0d4d\u0d1a\u0d4d",
    u8"(\u0d28\u0d4d|\u0d7b)\u001d?k",
    u8"\u0d19\u0d4d\u0d15\u0d4d",
    u8"(\u0d2a\u0d4d\u001d?h|\u001d?_[Ff]|[Ff])",
    u8"\u0d2b\u0d4d",
    u8"(\u0d30\u0d4d|\u0d7c)\u001d?\\^",
    u8"\u0d30\u0d4d\u200d",
    u8"(\u0d30\u0d4d|\u0d7c)\u001d?r",
    u8"\u0d31\u0d4d",
    u8"(\u0d4b\u0d3e*)\u001d?O",
    u8"\\1\u0d3e",
    u8"(\u0d4d\u001d?I|\u0d46\u001d?[ea]|\u0d3f\u001d?[ie])",
    u8"\u0d40",
    u8"(\u0d4d\u001d?U|\u0d41\u001d?u|\u0d4a\u001d?o)",
    u8"\u0d42",
    u8"(\u0d4d\u0d05|\u0d46)\u001d?i",
    u8"\u0d48",
    u8"(\u0d4d\u0d05|\u0d4a)\u001d?u",
    u8"\u0d57",
    u8"(\u0d7b|\u0d28\u0d4d)\u001d?\\^",
    u8"\u0d28\u0d4d\u200d",
    u8"(\u0d7b|\u0d28\u0d4d)\u001d?t",
    u8"\u0d28\u0d4d\u0d31\u0d4d",
    u8"(\u0d7d\u001d?L|\u0d7e\u001d?[lL])",
    u8"\u0d33\u0d4d\u0d33\u0d4d",
    u8"(\u0d7d|\u0d32\u0d4d)\u001d?\\^",
    u8"\u0d32\u0d4d\u200d",
    u8"(\u0d7e|\u0d33\u0d4d)\u001d?\\^",
    u8"\u0d33\u0d4d\u200d",
    u8"(k|\u001d?_[kc])",
    u8"\u0d15\u0d4d",
    u8"(\u0d7c\u001d?~|\u001d?_r)",
    u8"\u0d30\u0d4d",
    u8"(\u0d7e\u001d?~|\u001d?_L)",
    u8"\u0d33\u0d4d",
    u8"(\u0d7d\u001d?~|\u001d?_l)",
    u8"\u0d32\u0d4d",
    u8"(\u0d7a\u001d?~|\u001d?_N)",
    u8"\u0d23\u0d4d",
    u8"(\u0d7b\u001d?~|\u001d?_n)",
    u8"\u0d28\u0d4d",
    u8"(\u0d02\u001d?~|\u001d?_m)",
    u8"\u0d2e\u0d4d",
    u8"0#",
    u8"\u0d66",
    u8"1#",
    u8"\u0d67",
    u8"1/2#",
    u8"\u0d74",
    u8"1/4#",
    u8"\u0d73",
    u8"10#",
    u8"\u0d70",
    u8"100#",
    u8"\u0d71",
    u8"1000#",
    u8"\u0d72",
    u8"2#",
    u8"\u0d68",
    u8"3#",
    u8"\u0d69",
    u8"3/4#",
    u8"\u0d75",
    u8"4#",
    u8"\u0d6a",
    u8"5#",
    u8"\u0d6b",
    u8"6#",
    u8"\u0d6c",
    u8"7#",
    u8"\u0d6d",
    u8"8#",
    u8"\u0d6e",
    u8"9#",
    u8"\u0d6f",
    u8"@",
    u8"\u0d4d",
    u8"@a",
    u8"\u0d4d\u0d05",
    u8"@aL",
    u8"\u0d7e",
    u8"@aN",
    u8"\u0d7a",
    u8"@aa",
    u8"\u0d3e",
    u8"@ai",
    u8"\u0d48",
    u8"@al",
    u8"\u0d7d",
    u8"@am",
    u8"\u0d02",
    u8"@an",
    u8"\u0d7b",
    u8"@ar",
    u8"\u0d7c",
    u8"@au",
    u8"\u0d57",
    u8"C",
    u8"\u0d1a\u0d4d\u0d1a\u0d4d",
    u8"H",
    u8"\u0d03",
    u8"[\u0d05\u0d0e]\u001d?i",
    u8"\u0d10",
    u8"[\u0d05\u0d12]\u001d?u",
    u8"\u0d14",
    u8"\\$",
    u8"\u20b9",
    u8"\u001d?_X",
    u8"\u0d15\u0d4d\u0d37\u0d4d",
    u8"\u0d02\u001d?A",
    u8"\u0d2e\u0d3e",
    u8"\u0d02\u001d?E",
    u8"\u0d2e\u0d47",
    u8"\u0d02\u001d?I",
    u8"\u0d2e\u0d40",
    u8"\u0d02\u001d?O",
    u8"\u0d2e\u0d4b",
    u8"\u0d02\u001d?U",
    u8"\u0d2e\u0d42",
    u8"\u0d02\u001d?[Ll]",
    u8"\u0d2e\u0d4d\u0d32\u0d4d",
    u8"\u0d02\u001d?a",
    u8"\u0d2e",
    u8"\u0d02\u001d?e",
    u8"\u0d2e\u0d46",
    u8"\u0d02\u001d?i",
    u8"\u0d2e\u0d3f",
    u8"\u0d02\u001d?n",
    u8"\u0d2e\u0d4d\u0d28\u0d4d",
    u8"\u0d02\u001d?o",
    u8"\u0d2e\u0d4a",
    u8"\u0d02\u001d?p",
    u8"\u0d2e\u0d4d\u0d2a\u0d4d",
    u8"\u0d02\u001d?r",
    u8"\u0d2e\u0d4d\u0d30\u0d4d",
    u8"\u0d02\u001d?R",
    u8"\u0d2e\u0d43",
    u8"\u0d02\u001d?u",
    u8"\u0d2e\u0d41",
    u8"\u0d02\u001d?y",
    u8"\u0d2e\u0d4d\u0d2f\u0d4d",
    u8"\u0d05\u001d?#",
    u8"\u0d3d",
    u8"\u0d06\u001d?[Aa]",
    u8"\u0d06\u0d3e",
    u8"\u0d08\u001d?#",
    u8"\u0d5f",
    u8"\u0d08\u001d?[eiI]",
    u8"\u0d08\u0d57",
    u8"\u0d0a\u001d?[uoU]",
    u8"\u0d0a\u0d57",
    u8"\u0d0b\u0d0b\u001d?#",
    u8"\u0d60",
    u8"\u0d0c\u001d?L",
    u8"\u0d61",
    u8"\u0d13\u001d?O",
    u8"\u0d13\u0d3e",
    u8"\u0d14\u001d?u",
    u8"\u0d14\u0d57",
    u8"\u0d15\u0d4d\u001d?#",
    u8"\u0d7f",
    u8"\u0d17\u0d4d\u001d?h",
    u8"\u0d18\u0d4d",
    u8"\u0d1a\u0d4d\u001d?h",
    u8"\u0d1b\u0d4d",
    u8"\u0d1c\u0d4d\u001d?h",
    u8"\u0d1d\u0d4d",
    u8"\u0d1e\u0d4d\u0d1e\u0d4d\u001d?ch",
    u8"\u0d1e\u0d4d\u0d1a\u0d4d",
    u8"\u0d1e\u0d4d\u0d1e\u0d4d\u001d?j",
    u8"\u0d1e\u0d4d\u0d1c\u0d4d",
    u8"\u0d1e\u0d4d\u0d1e\u0d4d\u0d28\u0d4d\u001d?j",
    u8"\u0d1e\u0d4d\u0d1e\u0d4d",
    u8"\u0d1e\u0d4d\u0d1e\u0d4d\u0d7b\u001d?j",
    u8"\u0d1e\u0d4d\u0d1e\u0d4d",
    u8"\u0d1e\u0d4d\u0d28\u0d4d\u001d?j",
    u8"\u0d1e\u0d4d\u0d1e\u0d4d",
    u8"\u0d1f\u0d4d\u001d?h",
    u8"\u0d20\u0d4d",
    u8"\u0d1f\u0d4d\u0d1f\u0d4d\u001d?h",
    u8"\u0d24\u0d4d\u0d24\u0d4d",
    u8"\u0d21\u0d4d\u001d?h",
    u8"\u0d22\u0d4d",
    u8"\u0d23\u0d4d\u0d1f\u0d4d\u001d?T",
    u8"\u0d7a\u0d1f\u0d4d\u0d1f\u0d4d",
    u8"\u0d23\u0d4d\u0d21\u0d4d\u001d?D",
    u8"\u0d7a\u0d21\u0d4d\u0d21\u0d4d",
    u8"\u0d23\u0d4d\u0d26\u0d4d\u001d?d",
    u8"\u0d7a\u0d26\u0d4d\u0d26\u0d4d",
    u8"\u0d23\u0d4d\u0d28\u0d4d\u001d?n",
    u8"\u0d7a\u0d28\u0d4d\u0d28\u0d4d",
    u8"\u0d23\u0d4d\u0d2a\u0d4d\u001d?p",
    u8"\u0d7a\u0d2a\u0d4d\u0d2a\u0d4d",
    u8"\u0d23\u0d4d\u0d2e\u0d4d\u001d?m",
    u8"\u0d7a\u0d2e\u0d4d\u0d2e\u0d4d",
    u8"\u0d23\u0d4d\u0d2f\u0d4d\u001d?y",
    u8"\u0d7a\u0d2f\u0d4d\u0d2f\u0d4d",
    u8"\u0d23\u0d4d\u0d32\u0d4d\u001d?l",
    u8"\u0d7a\u0d32\u0d4d\u0d32\u0d4d",
    u8"\u0d23\u0d4d\u0d33\u0d4d\u001d?L",
    u8"\u0d7a\u0d33\u0d4d\u0d33\u0d4d",
    u8"\u0d23\u0d4d\u0d35\u0d4d\u001d?v",
    u8"\u0d7a\u0d35\u0d4d\u0d35\u0d4d",
    u8"\u0d24\u0d4d\u001d?h",
    u8"\u0d25\u0d4d",
    u8"\u0d24\u0d4d\u0d24\u0d4d\u001d?h",
    u8"\u0d24\u0d4d\u0d25\u0d4d",
    u8"\u0d26\u0d4d\u001d?h",
    u8"\u0d27\u0d4d",
    u8"\u0d28\u0d41\u001d?#",
    u8"\u0d79",
    u8"\u0d28\u0d4d\u001d?#",
    u8"\u0d29\u0d4d",
    u8"\u0d28\u001d?#",
    u8"\u0d29",
    u8"\u0d28\u0d4d\u001d?g",
    u8"\u0d19\u0d4d",
    u8"\u0d7b\u001d?j",
    u8"\u0d1e\u0d4d",
    u8"\u0d28\u0d4d\u0d1f\u0d4d\u001d?T",
    u8"\u0d7b\u0d1f\u0d4d\u0d1f\u0d4d",
    u8"\u0d28\u0d4d\u0d21\u0d4d\u001d?D",
    u8"\u0d7b\u0d21\u0d4d\u0d21\u0d4d",
    u8"\u0d28\u0d4d\u0d26\u0d4d\u001d?d",
    u8"\u0d7b\u0d26\u0d4d\u0d26\u0d4d",
    u8"\u0d28\u0d4d\u0d28\u0d4d\u001d?n",
    u8"\u0d7b\u0d28\u0d4d\u0d28\u0d4d",
    u8"\u0d28\u0d4d\u0d2a\u0d4d\u001d?p",
    u8"\u0d7b\u0d2a\u0d4d\u0d2a\u0d4d",
    u8"\u0d28\u0d4d\u0d2e\u0d4d\u001d?m",
    u8"\u0d7b\u0d2e\u0d4d\u0d2e\u0d4d",
    u8"\u0d28\u0d4d\u0d2f\u0d4d\u001d?y",
    u8"\u0d7b\u0d2f\u0d4d\u0d2f\u0d4d",
    u8"\u0d28\u0d4d\u0d30\u0d4d\u001d?r",
    u8"\u0d7b\u0d31\u0d4d",
    u8"\u0d28\u0d4d\u0d31\u0d4d\u001d?h",
    u8"\u0d28\u0d4d\u0d24\u0d4d",
    u8"\u0d28\u0d4d\u0d32\u0d4d\u001d?l",
    u8"\u0d7b\u0d32\u0d4d\u0d32\u0d4d",
    u8"\u0d28\u0d4d\u0d33\u0d4d\u001d?L",
    u8"\u0d7b\u0d33\u0d4d\u0d33\u0d4d",
    u8"\u0d28\u0d4d\u0d35\u0d4d\u001d?v",
    u8"\u0d7b\u0d35\u0d4d\u0d35\u0d4d",
    u8"\u0d2c\u0d4d\u001d?h",
    u8"\u0d2d\u0d4d",
    u8"\u0d2e\u0d4d\u0d1f\u0d4d\u001d?T",
    u8"\u0d02\u0d1f\u0d4d\u0d1f\u0d4d",
    u8"\u0d2e\u0d4d\u0d21\u0d4d\u001d?D",
    u8"\u0d02\u0d21\u0d4d\u0d21\u0d4d",
    u8"\u0d2e\u0d4d\u0d26\u0d4d\u001d?d",
    u8"\u0d02\u0d26\u0d4d\u0d26\u0d4d",
    u8"\u0d2e\u0d4d\u0d28\u0d4d\u001d?n",
    u8"\u0d02\u0d28\u0d4d\u0d28\u0d4d",
    u8"\u0d2e\u0d4d\u0d2a\u0d4d\u001d?p",
    u8"\u0d02\u0d2a\u0d4d\u0d2a\u0d4d",
    u8"\u0d2e\u0d4d\u0d2e\u0d4d\u001d?m",
    u8"\u0d02\u0d2e\u0d4d\u0d2e\u0d4d",
    u8"\u0d2e\u0d4d\u0d2f\u0d4d\u001d?y",
    u8"\u0d02\u0d2f\u0d4d\u0d2f\u0d4d",
    u8"\u0d2e\u0d4d\u0d32\u0d4d\u001d?l",
    u8"\u0d02\u0d32\u0d4d\u0d32\u0d4d",
    u8"\u0d2e\u0d4d\u0d33\u0d4d\u001d?L",
    u8"\u0d02\u0d33\u0d4d\u0d33\u0d4d",
    u8"\u0d2e\u0d4d\u0d35\u0d4d\u001d?v",
    u8"\u0d02\u0d35\u0d4d\u0d35\u0d4d",
    u8"\u0d30\u0d4d\u0d1f\u0d4d\u001d?T",
    u8"\u0d7c\u0d1f\u0d4d\u0d1f\u0d4d",
    u8"\u0d30\u0d4d\u0d21\u0d4d\u001d?D",
    u8"\u0d7c\u0d21\u0d4d\u0d21\u0d4d",
    u8"\u0d30\u0d4d\u0d26\u0d4d\u001d?d",
    u8"\u0d7c\u0d26\u0d4d\u0d26\u0d4d",
    u8"\u0d30\u0d4d\u0d28\u0d4d\u001d?n",
    u8"\u0d7c\u0d28\u0d4d\u0d28\u0d4d",
    u8"\u0d30\u0d4d\u0d2a\u0d4d\u001d?p",
    u8"\u0d7c\u0d2a\u0d4d\u0d2a\u0d4d",
    u8"\u0d30\u0d4d\u0d2e\u0d4d\u001d?m",
    u8"\u0d7c\u0d2e\u0d4d\u0d2e\u0d4d",
    u8"\u0d30\u0d4d\u0d2f\u0d4d\u001d?y",
    u8"\u0d7c\u0d2f\u0d4d\u0d2f\u0d4d",
    u8"\u0d30\u0d4d\u0d32\u0d4d\u001d?l",
    u8"\u0d7c\u0d32\u0d4d\u0d32\u0d4d",
    u8"\u0d30\u0d4d\u0d33\u0d4d\u001d?L",
    u8"\u0d7c\u0d33\u0d4d\u0d33\u0d4d",
    u8"\u0d30\u0d4d\u0d35\u0d4d\u001d?v",
    u8"\u0d7c\u0d35\u0d4d\u0d35\u0d4d",
    u8"\u0d31\u0d4d\u0d31\u0d4d\u001d?#",
    u8"\u0d3a\u0d4d",
    u8"\u0d31\u0d4d\u0d31\u001d?#",
    u8"\u0d3a",
    u8"\u0d31\u0d4d\u0d31\u0d4d\u001d?h",
    u8"\u0d24\u0d4d",
    u8"\u0d32\u0d4d\u0d1f\u0d4d\u001d?T",
    u8"\u0d7d\u0d1f\u0d4d\u0d1f\u0d4d",
    u8"\u0d32\u0d4d\u0d21\u0d4d\u001d?D",
    u8"\u0d7d\u0d21\u0d4d\u0d21\u0d4d",
    u8"\u0d32\u0d4d\u0d26\u0d4d\u001d?d",
    u8"\u0d7d\u0d26\u0d4d\u0d26\u0d4d",
    u8"\u0d32\u0d4d\u0d28\u0d4d\u001d?n",
    u8"\u0d7d\u0d28\u0d4d\u0d28\u0d4d",
    u8"\u0d32\u0d4d\u0d2a\u0d4d\u001d?p",
    u8"\u0d7d\u0d2a\u0d4d\u0d2a\u0d4d",
    u8"\u0d32\u0d4d\u0d2e\u0d4d\u001d?m",
    u8"\u0d7d\u0d2e\u0d4d\u0d2e\u0d4d",
    u8"\u0d32\u0d4d\u0d2f\u0d4d\u001d?y",
    u8"\u0d7d\u0d2f\u0d4d\u0d2f\u0d4d",
    u8"\u0d32\u0d4d\u0d32\u0d4d\u001d?l",
    u8"\u0d7d\u0d32\u0d4d\u0d32\u0d4d",
    u8"\u0d32\u0d4d\u0d33\u0d4d\u001d?L",
    u8"\u0d7d\u0d33\u0d4d\u0d33\u0d4d",
    u8"\u0d32\u0d4d\u0d35\u0d4d\u001d?v",
    u8"\u0d7d\u0d35\u0d4d\u0d35\u0d4d",
    u8"\u0d33\u0d4d\u001d?#",
    u8"\u0d0c",
    u8"\u0d33\u0d4d\u001d?L",
    u8"\u0d33\u0d4d\u0d33\u0d4d",
    u8"\u0d33\u0d4d\u0d1f\u0d4d\u001d?T",
    u8"\u0d7e\u0d1f\u0d4d\u0d1f\u0d4d",
    u8"\u0d33\u0d4d\u0d21\u0d4d\u001d?D",
    u8"\u0d7e\u0d21\u0d4d\u0d21\u0d4d",
    u8"\u0d33\u0d4d\u0d26\u0d4d\u001d?d",
    u8"\u0d7e\u0d26\u0d4d\u0d26\u0d4d",
    u8"\u0d33\u0d4d\u0d28\u0d4d\u001d?n",
    u8"\u0d7e\u0d28\u0d4d\u0d28\u0d4d",
    u8"\u0d33\u0d4d\u0d2a\u0d4d\u001d?p",
    u8"\u0d7e\u0d2a\u0d4d\u0d2a\u0d4d",
    u8"\u0d33\u0d4d\u0d2e\u0d4d\u001d?m",
    u8"\u0d7e\u0d2e\u0d4d\u0d2e\u0d4d",
    u8"\u0d33\u0d4d\u0d2f\u0d4d\u001d?y",
    u8"\u0d7e\u0d2f\u0d4d\u0d2f\u0d4d",
    u8"\u0d33\u0d4d\u0d32\u0d4d\u001d?l",
    u8"\u0d7e\u0d32\u0d4d\u0d32\u0d4d",
    u8"\u0d33\u0d4d\u0d33\u0d4d\u001d?L",
    u8"\u0d7e\u0d33\u0d4d\u0d33\u0d4d",
    u8"\u0d33\u0d4d\u0d33\u0d4d\u001d?#",
    u8"\u0d61",
    u8"\u0d33\u0d4d\u0d35\u0d4d\u001d?v",
    u8"\u0d7e\u0d35\u0d4d\u0d35\u0d4d",
    u8"\u0d36\u0d4d\u001d?h",
    u8"\u0d34\u0d4d",
    u8"\u0d38\u0d02\u001d?r",
    u8"\u0d38\u0d02\u0d7c",
    u8"\u0d38\u0d02\u001d?y",
    u8"\u0d38\u0d02\u0d2f\u0d4d",
    u8"\u0d38\u0d4d\u001d?h",
    u8"\u0d37\u0d4d",
    u8"\u0d3e\u001d?[Aa]",
    u8"\u0d3e\u0d3e",
    u8"\u0d40\u001d?[eiI]",
    u8"\u0d40\u0d40",
    u8"\u0d42\u001d?[uoU]",
    u8"\u0d42\u0d42",
    u8"\u0d43\u001d?R",
    u8"\u0d43\u0d7c",
    u8"\u0d43\u0d7c\u001d?#",
    u8"\u0d44",
    u8"\u0d4c\u001d?u",
    u8"\u0d4c\u0d57",
    u8"\u0d4d(\u001d?A|\u0d05\u001d?a)",
    u8"\u0d3e",
    u8"\u0d4d[\u0d33\u0d32]\u0d4d\u001d?#",
    u8"\u0d62",
    u8"\u0d4d[\u0d33\u0d32]\u0d4d[\u0d33\u0d32]\u0d4d\u001d?#",
    u8"\u0d63",
    u8"\u0d4d\u001d?E",
    u8"\u0d47",
    u8"\u0d4d\u001d?L",
    u8"\u0d4d\u0d32\u0d4d",
    u8"\u0d4d\u001d?O",
    u8"\u0d4b",
    u8"\u0d4d\u001d?R",
    u8"\u0d43",
    u8"\u0d4d\u001d?RA",
    u8"\u0d4d\u0d30\u0d3e",
    u8"\u0d4d\u001d?RE",
    u8"\u0d4d\u0d30\u0d47",
    u8"\u0d4d\u001d?RI",
    u8"\u0d4d\u0d30\u0d40",
    u8"\u0d4d\u001d?RO",
    u8"\u0d4d\u0d30\u0d4b",
    u8"\u0d4d\u001d?RU",
    u8"\u0d4d\u0d30\u0d42",
    u8"\u0d4d\u001d?Ra",
    u8"\u0d4d\u0d30",
    u8"\u0d4d\u001d?Re",
    u8"\u0d4d\u0d30\u0d46",
    u8"\u0d4d\u001d?Ri",
    u8"\u0d4d\u0d30\u0d3f",
    u8"\u0d4d\u001d?Ro",
    u8"\u0d4d\u0d30\u0d4a",
    u8"\u0d4d\u001d?Ru",
    u8"\u0d4d\u0d30\u0d41",
    u8"\u0d4d\u001d?R~",
    u8"\u0d4d\u0d30\u0d4d",
    u8"\u0d4d\u001d?_B",
    u8"\u0d4d\u200c\u0d2c\u0d4d\u0d2c\u0d4d",
    u8"\u0d4d\u001d?_C",
    u8"\u0d4d\u200c\u0d1a\u0d4d",
    u8"\u0d4d\u001d?_G",
    u8"\u0d4d\u200c\u0d17\u0d4d\u0d17\u0d4d",
    u8"\u0d4d\u001d?_J",
    u8"\u0d4d\u200c\u0d1c\u0d4d\u0d1c\u0d4d",
    u8"\u0d4d\u001d?_K",
    u8"\u0d4d\u200c\u0d15\u0d4d\u0d15\u0d4d",
    u8"\u0d4d\u001d?_N",
    u8"\u0d4d\u200c\u0d23\u0d4d",
    u8"\u0d4d\u001d?_Z",
    u8"\u0d4d\u200c\u0d36\u0d4d\u0d36\u0d4d",
    u8"\u0d4d\u001d?_b",
    u8"\u0d4d\u200c\u0d2c\u0d4d",
    u8"\u0d4d\u001d?_g",
    u8"\u0d4d\u200c\u0d17\u0d4d",
    u8"\u0d4d\u001d?_j",
    u8"\u0d4d\u200c\u0d1c\u0d4d",
    u8"\u0d4d\u001d?_n",
    u8"\u0d4d\u200c\u0d28\u0d4d",
    u8"\u0d4d\u001d?_r",
    u8"\u0d4d\u200c\u0d30\u0d4d",
    u8"\u0d4d\u001d?_s",
    u8"\u0d4d\u200c\u0d38\u0d4d",
    u8"\u0d4d\u001d?_T",
    u8"\u0d4d\u200c\u0d1f\u0d4d",
    u8"\u0d4d\u001d?_t",
    u8"\u0d4d\u200c\u0d31\u0d4d\u0d31\u0d4d",
    u8"\u0d4d\u001d?_D",
    u8"\u0d4d\u200c\u0d21\u0d4d",
    u8"\u0d4d\u001d?_L",
    u8"\u0d4d\u200c\u0d33\u0d4d",
    u8"\u0d4d\u001d?_M",
    u8"\u0d4d\u200c\u0d2e\u0d4d\u0d2e\u0d4d",
    u8"\u0d4d\u001d?_P",
    u8"\u0d4d\u200c\u0d2a\u0d4d\u0d2a\u0d4d",
    u8"\u0d4d\u001d?_X",
    u8"\u0d4d\u200c\u0d15\u0d4d\u0d37\u0d4d",
    u8"\u0d4d\u001d?_Y",
    u8"\u0d4d\u200c\u0d2f\u0d4d\u0d2f\u0d4d",
    u8"\u0d4d\u001d?_d",
    u8"\u0d4d\u200c\u0d26\u0d4d",
    u8"\u0d4d\u001d?_h",
    u8"\u0d4d\u200c\u0d39\u0d4d",
    u8"\u0d4d\u001d?_l",
    u8"\u0d4d\u200c\u0d32\u0d4d",
    u8"\u0d4d\u001d?_m",
    u8"\u0d4d\u200c\u0d2e\u0d4d",
    u8"\u0d4d\u001d?_p",
    u8"\u0d4d\u200c\u0d2a\u0d4d",
    u8"\u0d4d\u001d?_x",
    u8"\u0d4d\u200c\u0d15\u0d4d\u0d38\u0d4d",
    u8"\u0d4d\u001d?_y",
    u8"\u0d4d\u200c\u0d2f\u0d4d",
    u8"\u0d4d\u001d?_[kc]",
    u8"\u0d4d\u200c\u0d15\u0d4d",
    u8"\u0d4d\u001d?_[qQ]",
    u8"\u0d4d\u200c\u0d16\u0d4d",
    u8"\u0d4d\u001d?_[fF]",
    u8"\u0d4d\u200c\u0d2b\u0d4d",
    u8"\u0d4d\u001d?_[VW]",
    u8"\u0d4d\u200c\u0d35\u0d4d\u0d35\u0d4d",
    u8"\u0d4d\u001d?_[vw]",
    u8"\u0d4d\u200c\u0d35\u0d4d",
    u8"\u0d4d\u001d?_[zS]",
    u8"\u0d4d\u200c\u0d36\u0d4d",
    u8"\u0d33\u0d4d\u001d?_",
    u8"\u0d7e",
    u8"\u0d23\u0d4d\u001d?_",
    u8"\u0d7a",
    u8"\u0d32\u0d4d\u001d?_",
    u8"\u0d7d",
    u8"\u0d2e\u0d4d\u001d?_",
    u8"\u0d02",
    u8"\u0d28\u0d4d\u001d?_",
    u8"\u0d7b",
    u8"\u0d30\u0d4d\u001d?_",
    u8"\u0d7c",
    u8"\u0d4d\u001d?a",
    u8"",
    u8"\u0d4d\u001d?e",
    u8"\u0d46",
    u8"\u0d4d\u001d?i",
    u8"\u0d3f",
    u8"\u0d4d\u001d?o",
    u8"\u0d4a",
    u8"\u0d4d\u001d?u",
    u8"\u0d41",
    u8"\u0d4d\u001d?~",
    u8"\u0d4d",
    u8"\u0d4d\u001d?~A",
    u8"\u0d4d\u0d06",
    u8"\u0d4d\u001d?~E",
    u8"\u0d4d\u0d0f",
    u8"\u0d4d\u001d?~I",
    u8"\u0d4d\u0d08",
    u8"\u0d4d\u001d?~O",
    u8"\u0d4d\u0d13",
    u8"\u0d4d\u001d?~R",
    u8"\u0d4d\u0d0b",
    u8"\u0d4d\u001d?~U",
    u8"\u0d4d\u0d0a",
    u8"\u0d4d\u001d?~a",
    u8"\u0d4d\u0d05",
    u8"\u0d4d\u001d?~e",
    u8"\u0d4d\u0d0e",
    u8"\u0d4d\u001d?~i",
    u8"\u0d4d\u0d07",
    u8"\u0d4d\u001d?~o",
    u8"\u0d4d\u0d12",
    u8"\u0d4d\u001d?~u",
    u8"\u0d4d\u0d09",
    u8"\u0d57\u001d?#",
    u8"\u0d4c",
    u8"\u0d57\u001d?[uieIuUou]",
    u8"\u0d57\u0d57",
    u8"\u0d7a\u001d?A",
    u8"\u0d23\u0d3e",
    u8"\u0d7a\u001d?D",
    u8"\u0d23\u0d4d\u0d21\u0d4d",
    u8"\u0d7a\u001d?E",
    u8"\u0d23\u0d47",
    u8"\u0d7a\u001d?I",
    u8"\u0d23\u0d40",
    u8"\u0d7a\u001d?N",
    u8"\u0d23\u0d4d\u0d23\u0d4d",
    u8"\u0d7a\u001d?O",
    u8"\u0d23\u0d4b",
    u8"\u0d7a\u001d?U",
    u8"\u0d23\u0d42",
    u8"\u0d7a\u001d?a",
    u8"\u0d23",
    u8"\u0d7a\u001d?e",
    u8"\u0d23\u0d46",
    u8"\u0d7a\u001d?i",
    u8"\u0d23\u0d3f",
    u8"\u0d7a\u001d?m",
    u8"\u0d23\u0d4d\u0d2e\u0d4d",
    u8"\u0d7a\u001d?o",
    u8"\u0d23\u0d4a",
    u8"\u0d7a\u001d?R",
    u8"\u0d23\u0d43",
    u8"\u0d7a\u001d?u",
    u8"\u0d23\u0d41",
    u8"\u0d7a\u001d?v",
    u8"\u0d23\u0d4d\u0d35\u0d4d",
    u8"\u0d7a\u001d?y",
    u8"\u0d23\u0d4d\u0d2f\u0d4d",
    u8"\u0d7b\u001d?A",
    u8"\u0d28\u0d3e",
    u8"\u0d7b\u001d?E",
    u8"\u0d28\u0d47",
    u8"\u0d7b\u001d?I",
    u8"\u0d28\u0d40",
    u8"\u0d7b\u001d?O",
    u8"\u0d28\u0d4b",
    u8"\u0d7b\u001d?U",
    u8"\u0d28\u0d42",
    u8"\u0d7b\u001d?a",
    u8"\u0d28",
    u8"\u0d7b\u001d?d",
    u8"\u0d28\u0d4d\u0d26\u0d4d",
    u8"\u0d7b\u001d?e",
    u8"\u0d28\u0d46",
    u8"\u0d7b\u001d?g",
    u8"\u0d19\u0d4d",
    u8"\u0d7b\u001d?i",
    u8"\u0d28\u0d3f",
    u8"\u0d7b\u001d?m",
    u8"\u0d28\u0d4d\u0d2e\u0d4d",
    u8"\u0d7b\u001d?n",
    u8"\u0d28\u0d4d\u0d28\u0d4d",
    u8"\u0d7b\u001d?o",
    u8"\u0d28\u0d4a",
    u8"\u0d7b\u001d?r",
    u8"\u0d28\u0d4d\u0d30\u0d4d",
    u8"\u0d7b\u001d?R",
    u8"\u0d28\u0d43",
    u8"\u0d7b\u001d?u",
    u8"\u0d28\u0d41",
    u8"\u0d7b\u001d?v",
    u8"\u0d28\u0d4d\u0d35\u0d4d",
    u8"\u0d7b\u001d?y",
    u8"\u0d28\u0d4d\u0d2f\u0d4d",
    u8"\u0d7c\u001d?#",
    u8"\u0d4e",
    u8"\u0d7c\u001d?A",
    u8"\u0d30\u0d3e",
    u8"\u0d7c\u001d?E",
    u8"\u0d30\u0d47",
    u8"\u0d7c\u001d?I",
    u8"\u0d30\u0d40",
    u8"\u0d7c\u001d?O",
    u8"\u0d30\u0d4b",
    u8"\u0d7c\u001d?U",
    u8"\u0d30\u0d42",
    u8"\u0d7c\u001d?a",
    u8"\u0d30",
    u8"\u0d7c\u001d?e",
    u8"\u0d30\u0d46",
    u8"\u0d7c\u001d?i",
    u8"\u0d30\u0d3f",
    u8"\u0d7c\u001d?o",
    u8"\u0d30\u0d4a",
    u8"\u0d7c\u001d?R",
    u8"\u0d30\u0d43",
    u8"\u0d7c\u001d?u",
    u8"\u0d30\u0d41",
    u8"\u0d7c\u001d?y",
    u8"\u0d30\u0d4d\u0d2f\u0d4d",
    u8"\u0d7d\u001d?A",
    u8"\u0d32\u0d3e",
    u8"\u0d7d\u001d?E",
    u8"\u0d32\u0d47",
    u8"\u0d7d\u001d?I",
    u8"\u0d32\u0d40",
    u8"\u0d7d\u001d?O",
    u8"\u0d32\u0d4b",
    u8"\u0d7d\u001d?U",
    u8"\u0d32\u0d42",
    u8"\u0d7d\u001d?[lL]",
    u8"\u0d32\u0d4d\u0d32\u0d4d",
    u8"\u0d7d\u001d?a",
    u8"\u0d32",
    u8"\u0d7d\u001d?e",
    u8"\u0d32\u0d46",
    u8"\u0d7d\u001d?i",
    u8"\u0d32\u0d3f",
    u8"\u0d7d\u001d?m",
    u8"\u0d32\u0d4d\u0d2e\u0d4d",
    u8"\u0d7d\u001d?o",
    u8"\u0d32\u0d4a",
    u8"\u0d7d\u001d?p",
    u8"\u0d32\u0d4d\u0d2a\u0d4d",
    u8"\u0d7d\u001d?R",
    u8"\u0d32\u0d43",
    u8"\u0d7d\u001d?u",
    u8"\u0d32\u0d41",
    u8"\u0d7d\u001d?v",
    u8"\u0d32\u0d4d\u0d35\u0d4d",
    u8"\u0d7d\u001d?y",
    u8"\u0d32\u0d4d\u0d2f\u0d4d",
    u8"\u0d7e\u001d?A",
    u8"\u0d33\u0d3e",
    u8"\u0d7e\u001d?E",
    u8"\u0d33\u0d47",
    u8"\u0d7e\u001d?I",
    u8"\u0d33\u0d40",
    u8"\u0d7e\u001d?O",
    u8"\u0d33\u0d4b",
    u8"\u0d7e\u001d?U",
    u8"\u0d33\u0d42",
    u8"\u0d7e\u001d?a",
    u8"\u0d33",
    u8"\u0d7e\u001d?e",
    u8"\u0d33\u0d46",
    u8"\u0d7e\u001d?i",
    u8"\u0d33\u0d3f",
    u8"\u0d7e\u001d?o",
    u8"\u0d33\u0d4a",
    u8"\u0d7e\u001d?R",
    u8"\u0d33\u0d43",
    u8"\u0d7e\u001d?u",
    u8"\u0d33\u0d41",
    u8"\u0d7e\u001d?y",
    u8"\u0d33\u0d4d\u0d2f\u0d4d",
    u8"_?E",
    u8"\u0d0f",
    u8"_?O",
    u8"\u0d13",
    u8"_?R",
    u8"\u0d0b",
    u8"_?a",
    u8"\u0d05",
    u8"_?e",
    u8"\u0d0e",
    u8"_?i",
    u8"\u0d07",
    u8"_?o",
    u8"\u0d12",
    u8"_?u",
    u8"\u0d09",
    u8"cch",
    u8"\u0d1a\u0d4d\u0d1a\u0d4d",
    u8"cchh",
    u8"\u0d1a\u0d4d\u0d1b\u0d4d",
    u8"ch",
    u8"\u0d1a\u0d4d",
    u8"t",
    u8"\u0d31\u0d4d\u0d31\u0d4d",
    u8"L",
    u8"\u0d7e",
    u8"N",
    u8"\u0d7a",
    u8"l",
    u8"\u0d7d",
    u8"m",
    u8"\u0d02",
    u8"n",
    u8"\u0d7b",
    u8"r",
    u8"\u0d7c"};
const unsigned int kTransformsLen = base::size(kTransforms);
const char* kHistoryPrune = "a|@|@a|c|R|_|~";

}  // namespace ml_phone
