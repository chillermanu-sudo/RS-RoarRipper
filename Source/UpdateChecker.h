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

  This software uses the JUCE framework (https://juce.com), which is also
  licensed under the GNU Affero General Public License version 3.
*/
#pragma once

#include <JuceHeader.h>
#include "UpdateVersion.h"

#include <atomic>
#include <memory>

// Update-Check ganz abschaltbar: RS_ENABLE_UPDATE_CHECK=0 (z.B. in den Preprocessor-
// Definitionen des Projucer-Projekts) -> es werden keine Netzwerkzugriffe gemacht.
#ifndef RS_ENABLE_UPDATE_CHECK
#define RS_ENABLE_UPDATE_CHECK 1
#endif

namespace rsupd
{
    inline const char* repoPath() noexcept { return "chillermanu-sudo/RS-RoarRipper"; }

    inline juce::String releasesPageUrl()
    {
        return juce::String("https://github.com/") + repoPath() + "/releases";
    }

    struct Info
    {
        bool checked = false;   // Server wurde erfolgreich erreicht und ausgewertet
        bool newer = false;     // es gibt eine neuere Version als die installierte
        juce::String tag;       // z.B. "v1.5.1"
        juce::String url;       // Seite zum Herunterladen
    };

    namespace detail
    {
        inline juce::CriticalSection& lock() { static juce::CriticalSection l; return l; }
        inline Info& store() { static Info i; return i; }
        inline std::atomic<bool>& notified() { static std::atomic<bool> n{ false }; return n; }
    }

    // Letztes bekanntes Ergebnis (gilt fuer den ganzen Prozess, alle Plugin-Instanzen)
    inline Info getInfo()
    {
        const juce::ScopedLock sl(detail::lock());
        return detail::store();
    }

    // true genau einmal pro Prozess: fuer die einmalige Meldung "Update verfuegbar"
    inline bool claimNotification()
    {
        return !detail::notified().exchange(true);
    }

    // Fragt GitHub im Hintergrund nach der neuesten Version. Blockiert die GUI nie.
    // Ablauf: /releases/latest, bei Misserfolg Fallback auf /tags.
    class UpdateChecker : private juce::Thread
    {
    public:
        explicit UpdateChecker(const juce::String& currentVersion)
            : juce::Thread("RS update check"), current(currentVersion)
        {
            {
                const juce::ScopedLock sl(detail::lock());
                if (detail::store().checked)
                    return;   // in diesem Prozess schon geprueft
            }

            startThread();
        }

        ~UpdateChecker() override
        {
            stopThread(1500);
        }

    private:
        void run() override
        {
            const juce::String api = juce::String("https://api.github.com/repos/") + repoPath();

            bool reached = false;
            juce::String bestTag, bestUrl;

            juce::String body;

            // 1) Neuestes (nicht-Draft, nicht-Prerelease) Release
            if (httpGet(api + "/releases/latest", body))
            {
                reached = true;
                const auto j = juce::JSON::parse(body);
                bestTag = j["tag_name"].toString();
                bestUrl = j["html_url"].toString();
            }

            // 2) Fallback: hoechster Tag (falls es nur Tags, aber keine Releases gibt)
            if (bestTag.isEmpty() && !threadShouldExit() && httpGet(api + "/tags", body))
            {
                reached = true;
                const auto j = juce::JSON::parse(body);

                if (auto* arr = j.getArray())
                {
                    for (const auto& t : *arr)
                    {
                        const auto name = t["name"].toString();

                        if (name.isNotEmpty()
                            && (bestTag.isEmpty()
                                || rsupd::compareVersions(name.toStdString(), bestTag.toStdString()) > 0))
                            bestTag = name;
                    }
                }
            }

            if (threadShouldExit() || !reached)
                return;   // nichts gespeichert -> naechster Editor versucht es erneut

            // Nur Links auf das eigene Repository oeffnen, sonst die Releases-Seite
            if (!bestUrl.startsWith(juce::String("https://github.com/") + repoPath()))
                bestUrl = releasesPageUrl();

            Info i;
            i.checked = true;
            i.tag = bestTag;
            i.url = bestUrl;
            i.newer = bestTag.isNotEmpty()
                && rsupd::isNewerVersion(bestTag.toStdString(), current.toStdString());

            const juce::ScopedLock sl(detail::lock());
            detail::store() = i;
        }

        bool httpGet(const juce::String& address, juce::String& body)
        {
            body.clear();

            int status = 0;
            auto opts = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                .withConnectionTimeoutMs(6000)
                .withNumRedirectsToFollow(3)
                .withStatusCode(&status)
                .withExtraHeaders("User-Agent: RS-RoarRipper-UpdateCheck\r\n"
                                  "Accept: application/vnd.github+json\r\n");

            std::unique_ptr<juce::InputStream> in = juce::URL(address).createInputStream(opts);

            if (in == nullptr)
                return false;

            juce::MemoryOutputStream out;
            char buf[4096];

            while (!threadShouldExit() && !in->isExhausted() && out.getDataSize() < 262144)
            {
                const int n = in->read(buf, (int)sizeof(buf));

                if (n <= 0)
                    break;

                out.write(buf, (size_t)n);
            }

            body = out.toString();
            return !threadShouldExit();
        }

        juce::String current;
    };
}
