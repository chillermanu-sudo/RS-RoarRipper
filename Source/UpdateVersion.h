/*
  RS Roar Ripper - Feedback Suppressor
  Copyright (c) 2026 Roar Sound

  SPDX-License-Identifier: AGPL-3.0-only

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published by
  the Free Software Foundation, version 3 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program. If not, see <https://www.gnu.org/licenses/>.
*/
#pragma once

// Reiner C++-Teil des Update-Checks (ohne JUCE), damit er einzeln testbar ist.

#include <algorithm>
#include <string>
#include <vector>

namespace rsupd
{
    // "v1.5.0", "1.5", "RS-RoarRipper-v1.5.0-beta" -> {1,5,0} / {1,5} / {1,5,0}
    inline std::vector<int> parseVersion(const std::string& s)
    {
        auto isDigit = [](char c) { return c >= '0' && c <= '9'; };

        std::vector<int> v;
        size_t i = 0;

        while (i < s.size() && !isDigit(s[i]))
            ++i;

        while (i < s.size() && isDigit(s[i]))
        {
            long cur = 0;

            while (i < s.size() && isDigit(s[i]))
            {
                cur = std::min(cur * 10 + (s[i] - '0'), 1000000L);
                ++i;
            }

            v.push_back((int)cur);

            if (i + 1 < s.size() && s[i] == '.' && isDigit(s[i + 1]))
                ++i;
            else
                break;
        }

        return v;
    }

    // <0: a kleiner b, 0: gleich, >0: a groesser b (fehlende Stellen zaehlen als 0)
    inline int compareVersions(const std::string& a, const std::string& b)
    {
        const auto pa = parseVersion(a);
        const auto pb = parseVersion(b);
        const size_t n = std::max(pa.size(), pb.size());

        for (size_t i = 0; i < n; ++i)
        {
            const int x = i < pa.size() ? pa[i] : 0;
            const int y = i < pb.size() ? pb[i] : 0;

            if (x != y)
                return x < y ? -1 : 1;
        }

        return 0;
    }

    // true nur, wenn beide Versionen lesbar sind und 'latest' echt neuer ist.
    inline bool isNewerVersion(const std::string& latest, const std::string& current)
    {
        if (parseVersion(latest).empty() || parseVersion(current).empty())
            return false;

        return compareVersions(latest, current) > 0;
    }
}
