/*
    This file is part of Likyu — https://github.com/pob31/likyu

    Copyright (C) 2026 Pierre-Olivier Boulant

    Likyu is free software: you can redistribute it and/or modify it under the
    terms of the GNU General Public License as published by the Free Software
    Foundation, either version 3 of the License, or (at your option) any later
    version. Likyu is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
    or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
    (LICENSE, at the repository root) for more details.

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "BenchThread.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#if defined (_WIN32)
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <avrt.h>
#elif defined (__APPLE__)
 #include <pthread.h>
 #include <pthread/qos.h>
#elif defined (__linux__)
 #include <pthread.h>
 #include <sched.h>
 #include <unistd.h>
#endif

namespace likyubench
{
   #if defined (_WIN32)

    namespace
    {
        struct Core
        {
            int efficiencyClass = 0;
            WORD group = 0;
            KAFFINITY mask = 0;
        };

        std::vector<Core> coresOfThisMachine()
        {
            std::vector<Core> cores;
            DWORD length = 0;
            GetLogicalProcessorInformationEx (RelationProcessorCore, nullptr, &length);

            if (length == 0)
                return cores;

            std::vector<char> buffer (length);
            auto* first = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*> (buffer.data());

            if (! GetLogicalProcessorInformationEx (RelationProcessorCore, first, &length))
                return cores;

            for (DWORD offset = 0; offset < length;)
            {
                const auto* info = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*> (buffer.data() + offset);

                if (info->Relationship == RelationProcessorCore && info->Processor.GroupCount > 0)
                    cores.push_back ({ static_cast<int> (info->Processor.EfficiencyClass),
                                       info->Processor.GroupMask[0].Group, info->Processor.GroupMask[0].Mask });

                offset += info->Size;
            }

            return cores;
        }

        int indexOfLowestBit (KAFFINITY mask)
        {
            int index = 0;

            while (index < 63 && ((mask >> index) & 1u) == 0)
                ++index;

            return index;
        }
    }

    std::string claimThread (Cores cores, bool audioPriority)
    {
        std::string said;

        if (cores != Cores::any)
        {
            const auto all = coresOfThisMachine();
            auto highest = 0, next = -1;

            for (const auto& c : all)
                highest = std::max (highest, c.efficiencyClass);

            for (const auto& c : all)
                if (c.efficiencyClass < highest)
                    next = std::max (next, c.efficiencyClass);

            const auto wanted = cores == Cores::performance || next < 0 ? highest : next;
            const Core* chosen = nullptr;

            // The last core of its kind: the first ones take the machine's interrupts.
            for (const auto& c : all)
                if (c.efficiencyClass == wanted)
                    chosen = &c;

            if (chosen == nullptr)
            {
                said += "no core list, not pinned";
            }
            else
            {
                GROUP_AFFINITY affinity {};
                affinity.Group = chosen->group;
                const auto bit = indexOfLowestBit (chosen->mask);
                affinity.Mask = static_cast<KAFFINITY> (1) << bit;
                const auto pinned = SetThreadGroupAffinity (GetCurrentThread(), &affinity, nullptr) != 0;

                char text[128];
                std::snprintf (text, sizeof (text), "%s logical processor %d of group %d (efficiency class %d of 0..%d%s)",
                               pinned ? "pinned to" : "NOT pinned to", bit, static_cast<int> (chosen->group), wanted, highest,
                               cores == Cores::efficiency && next < 0 ? "; this machine has one kind of core" : "");
                said += text;
            }
        }
        else
        {
            said += "not pinned";
        }

        if (audioPriority)
        {
            DWORD taskIndex = 0;

            if (AvSetMmThreadCharacteristicsW (L"Pro Audio", &taskIndex) != nullptr)
            {
                said += ", MMCSS Pro Audio";
            }
            else
            {
                SetPriorityClass (GetCurrentProcess(), HIGH_PRIORITY_CLASS);
                const auto raised = SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL) != 0;
                said += raised ? ", MMCSS refused, time-critical in a high-priority process" : ", no priority obtained";
            }
        }
        else
        {
            said += ", ordinary priority";
        }

        return said;
    }

   #elif defined (__APPLE__)

    std::string claimThread (Cores cores, bool audioPriority)
    {
        // No affinity on macOS: the QoS class is what decides the kind of core.
        if (cores == Cores::efficiency)
            return pthread_set_qos_class_self_np (QOS_CLASS_BACKGROUND, 0) == 0
                       ? "background QoS (efficiency cores), not pinned" : "background QoS refused";

        if (audioPriority || cores == Cores::performance)
            return pthread_set_qos_class_self_np (QOS_CLASS_USER_INTERACTIVE, 0) == 0
                       ? "user-interactive QoS (performance cores), not pinned" : "user-interactive QoS refused";

        return "default QoS, not pinned";
    }

   #elif defined (__linux__)

    namespace
    {
        long maxFrequencyOf (int cpu)
        {
            char path[96];
            std::snprintf (path, sizeof (path), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", cpu);
            long khz = 0;

            if (auto* file = std::fopen (path, "r"))
            {
                if (std::fscanf (file, "%ld", &khz) != 1)
                    khz = 0;

                std::fclose (file);
            }

            return khz;
        }
    }

    std::string claimThread (Cores cores, bool audioPriority)
    {
        std::string said;

        if (cores != Cores::any)
        {
            const auto count = static_cast<int> (sysconf (_SC_NPROCESSORS_ONLN));
            long fastest = 0, slowest = 0;

            for (int cpu = 0; cpu < count; ++cpu)
            {
                const auto khz = maxFrequencyOf (cpu);
                fastest = std::max (fastest, khz);
                slowest = cpu == 0 ? khz : std::min (slowest, khz);
            }

            // Two kinds of core only where the slowest is well under the fastest:
            // preferred cores differ by a few percent on a machine with one kind.
            const auto twoKinds = slowest > 0 && slowest * 5 < fastest * 4;
            const auto wanted = cores == Cores::efficiency && twoKinds ? slowest : fastest;
            auto chosen = -1;

            // The last of its kind: the first ones take the machine's interrupts.
            for (int cpu = 0; cpu < count; ++cpu)
                if (maxFrequencyOf (cpu) == wanted)
                    chosen = cpu;

            if (chosen < 0)
                chosen = std::max (0, count - 1);

            cpu_set_t set;
            CPU_ZERO (&set);
            CPU_SET (chosen, &set);
            const auto pinned = sched_setaffinity (0, sizeof (set), &set) == 0;

            char text[128];
            std::snprintf (text, sizeof (text), "%s cpu %d (%ld MHz at most%s)", pinned ? "pinned to" : "NOT pinned to",
                           chosen, wanted / 1000, cores == Cores::efficiency && ! twoKinds ? "; this machine has one kind of core" : "");
            said += text;
        }
        else
        {
            said += "not pinned";
        }

        if (audioPriority)
        {
            sched_param parameters {};
            parameters.sched_priority = 70;
            said += sched_setscheduler (0, SCHED_FIFO, &parameters) == 0 ? ", SCHED_FIFO 70"
                                                                         : ", SCHED_FIFO refused (ordinary priority)";
        }
        else
        {
            said += ", ordinary priority";
        }

        return said;
    }

   #else

    std::string claimThread (Cores, bool)
    {
        return "not pinned, ordinary priority (unknown platform)";
    }

   #endif
}
