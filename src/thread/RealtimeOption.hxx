/*
 * Copyright (C) 2003-2010 The Music Player Daemon Project
 * http://www.musicpd.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

//
// mpd.conf
//
// realtime_option {
//     main_priority        "POLICY:PRIORITY"
//     io_priority          "POLICY:PRIORITY"
//     decorder_priority    "POLICY:PRIORITY"
//     player_priority      "POLICY:PRIORITY"
//     update_priority      "POLICY:PRIORITY"
//
//     memlock              "yes" or "no"
//     stackreserve	       "1024"
//     heapreserve	       "10240"
//
//   }
//
//  POLICY  "OTHER" | "FIFO" | "RR" | "BATCH" | "IDLE"
//  PRIORITY
//            OTHER,BATCH,IDLE   TIMERSLACK(ns)
//            FIFO, RR           1 - 99
//
//   audio_output {
//       ....
//       ....
//     priority              "POLICY:PRIORITY"
//     timerslack            unsigned long(default value = 100)
//   }
//

#ifndef REALTIMEOPTION_HXX_
#define REALTIMEOPTION_HXX_

#ifdef ENABLE_RTOPT

#include "config/Data.hxx"
#include "config/Block.hxx"

#include <string>
#include <unordered_map>

#define RTOPT_DISABLE (-1)

struct PriorityDef {
        std::string      name;
        int		 policy;
        int		 priority;
        unsigned long    timerslack;

        PriorityDef(std::string _name)
        //                :name(new std::string(_name)),
                :name(_name),
                 policy(RTOPT_DISABLE),
                 priority(0),
                 timerslack(0u) {}

        inline bool IsEnable() const noexcept {
                return policy != RTOPT_DISABLE;
        }

        inline bool isRealTime() const noexcept {
                return isRealTime(policy);
        }

        inline unsigned long getTimerSlack() const noexcept {
                return isRealTime() ? 0ul : timerslack;
        }

        static bool isRealTime(int p) {
                return (p == SCHED_FIFO) || (p == SCHED_RR);
        }
};


struct RealtimeOption {
public:
        static void Initialize(const ConfigData& config);
        static void LockMemory();
        static void ChangeThreadPriority(const char *name);

private:

        static bool enable_rtopt;
        static bool enable_memlock;
        static unsigned long stack_reserve;
        static unsigned long heap_reserve;

        static const std::unordered_map<std::string,int> policy_tbl;
        static std::unordered_map<std::string, PriorityDef*> priority_tbl;


        static bool isEnableRt() { return enable_rtopt; };
        static bool isEnableMemLock() { return enable_memlock; };

        static bool isPriority(int priority) {
                return ( priority >= 1) && ( priority <= 99 );
        }

        static void SetUnlimited( const int target, const char *target_name);
        static int GetPolicy(const char *name);
        static std::string GetPolicyName(int policy);
        static void SetOutputPriority(const ConfigData& config);
        static void ParsePriority(const char *param, PriorityDef *priority);
        static void SetParameter(const ConfigData& config);
        static PriorityDef *SetThreadPriority(const ConfigBlock *param,std::string name);
        static const PriorityDef *GetPriorityParam(const char *key);
        static void ResetLimit();
        static void ChangePriority(const PriorityDef *priority);
        static void PrintPriorityTbl();
};

#endif /* ENABLE_RTOPT */

#endif /* REALTIMEOPTION_HXX_ */
