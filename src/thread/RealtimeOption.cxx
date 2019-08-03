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



#include "config.h"

#ifdef ENABLE_RTOPT

#include "system/FatalError.hxx"
#include "Log.hxx"
#include "util/Domain.hxx"

#include "config/Data.hxx"
#include "config/Option.hxx"

#include "Util.hxx"
#include "Slack.hxx"
#include "RealtimeOption.hxx"

#include <sys/resource.h>
#include <sys/mman.h>


#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <sched.h>

#include <string>
#include <unordered_map>
#include <vector>


static constexpr Domain realtime_domain("realtimeoption");


const std::unordered_map<std::string,int> RealtimeOption::policy_tbl = {
        { "OTHER",   SCHED_OTHER },
        { "FIFO" ,   SCHED_FIFO  },
        { "RR"   ,   SCHED_RR    },
        { "BATCH",   SCHED_BATCH },
#ifdef SCHED_IDLE
        { "IDLE" ,   SCHED_IDLE }
#endif
};


std::unordered_map<std::string, PriorityDef*> RealtimeOption::priority_tbl = {
        { "",   new PriorityDef("") }
};

bool RealtimeOption::enable_rtopt = false;
bool RealtimeOption::enable_memlock = false;
unsigned long RealtimeOption::stack_reserve = 0ul;
unsigned long RealtimeOption::heap_reserve  = 0ul;


static int
strtonum(const char *str, unsigned long *ival) {
        char *endptr = NULL;

        *ival = strtoul(str, &endptr, 10);
        return (*endptr == '\0') ? 0 : -1;
}


void
RealtimeOption::SetUnlimited( const int target, const char *target_name) {
        const rlimit unlimited = {
          RLIM_INFINITY,
          RLIM_INFINITY
        };
        const int res = setrlimit(target,&unlimited);
        if ( res < 0 ) {
          FormatFatalError("SetUnlimied %s error %d(%s)\n",target_name,errno,strerror(errno));
        }
}

int
RealtimeOption::GetPolicy(const char  *name) {
        int policy = RTOPT_DISABLE;
        try {
                policy = policy_tbl.at(std::string(name));
        } catch (...) {
        }
        return policy;
}

std::string
RealtimeOption::GetPolicyName(int policy) {
        std::string name("UNDEF(" + std::to_string(policy) + ")");

        for (auto &x : policy_tbl) {
                if ( x.second == policy ) {
                        name = x.first;
                        break;
                }
        }
        return name;
}

void
RealtimeOption::SetOutputPriority(const ConfigData& config) {
        for (const auto& param: config.GetBlockList(ConfigBlockOption::AUDIO_OUTPUT)) {
                const char *name = param.GetBlockValue("name");
                const char *pval = param.GetBlockValue("priority");
                if ( ( name != nullptr ) && ( pval != nullptr ) ) {
                        PriorityDef *pri = new PriorityDef(std::string("output:") + std::string(name));
                        ParsePriority(pval, pri);
                        priority_tbl[pri->name] = pri;
                } else {
                        FormatWarning(realtime_domain, "SetOutputPriority: Missing \"name\" configuration\n");
                }
        }
}

static const char *parse_priority_msg = "ParsePriority(%d): illegal policy name = '%s'   priority = '%s'\n";

void
RealtimeOption::ParsePriority(const char *param, PriorityDef *priority) {


        std::string paramstr(param);

        int  policy_val;
        unsigned  long priority_val;

        priority->policy = RTOPT_DISABLE;
        priority->priority = 0;
        priority->timerslack = 0ul;


        int pos = paramstr.find(':');
        if ( pos < 0 ) {
                return;
        }

        if ( (policy_val = GetPolicy(paramstr.substr(0,pos).c_str()))  == RTOPT_DISABLE ) {
                FormatWarning(realtime_domain,
                              parse_priority_msg,
                              __LINE__,priority->name.c_str(),param);
                return;
        }

        if ( strtonum(paramstr.substr(pos+1).c_str(), &priority_val) != 0 ) {
                FormatWarning(realtime_domain,
                              parse_priority_msg,
                              __LINE__,priority->name.c_str(),param);
                return;
        }

        if ( PriorityDef::isRealTime(policy_val) ) {
                if ( isPriority(priority_val) ) {
                        priority->policy = policy_val;
                        priority->priority = priority_val;
                } else {
                        FormatWarning(realtime_domain,
                                      parse_priority_msg,
                                      __LINE__,priority->name.c_str(),param);
                        return;
                }
        } else {

               /*  OTHER, BATCH, IDLE   */
                priority->policy = policy_val;
                priority->timerslack = priority_val;
        }
}


void
RealtimeOption::SetParameter(const ConfigData& config) {
        enable_rtopt = false;

        const ConfigBlock *param = config.GetBlock(ConfigBlockOption::REALTIME_OPTION);
        if ( param == NULL ) {
                return;
        }
        enable_rtopt = true;

        for ( BlockParam  val : param->block_params ) {
                if ( val.name.compare("memlock") == 0 ) {
                        enable_memlock = param->GetBlockValue("memlock",false);
                } else if ( val.name.compare("stack_reserve") == 0 ) {
                        stack_reserve = param->GetBlockValue("stack_reserve",0u) * 1024;
                } else if ( val.name.compare("heap_reserve") == 0 ) {
                        heap_reserve = param->GetBlockValue("heap_reserve",0u) * 1024;
                } else {
                        PriorityDef *pdef = SetThreadPriority(param,val.name);
                        if ( pdef != nullptr ) {
                                priority_tbl[pdef->name] = pdef;
                        }
                }
        }
}


PriorityDef *
RealtimeOption::SetThreadPriority(const ConfigBlock *param,std::string name) {

        const char *paramstr = param->GetBlockValue(name.c_str());
        if ( paramstr == nullptr ) {
                return nullptr;
        }

        auto idx = name.find("_priority");
        if ( idx == std::string::npos ) {
                return nullptr;
        }

        PriorityDef *pri = new PriorityDef(name.substr(0,idx));
        ParsePriority(paramstr,pri);
        return pri;
}

const PriorityDef *
RealtimeOption::GetPriorityParam(const char *key) {
        PriorityDef *pdef = nullptr;

        try {
                pdef = priority_tbl[std::string(key)];
        } catch (...) {
        }

        return pdef;
}

void
RealtimeOption::ResetLimit() {
        SetUnlimited(RLIMIT_MEMLOCK,"memlock");
        SetUnlimited(RLIMIT_RTPRIO, "rtprio");
}

void
RealtimeOption::ChangePriority(const PriorityDef *priority) {
        sched_param param = { priority->priority };

        if ( priority->policy == SCHED_IDLE ) {
                SetThreadIdlePriority();
        } else {
                int res = sched_setscheduler(0,priority->policy,&param);
                if ( res < 0 ) {
                        FormatWarning(realtime_domain,
                                      "ChangePriority: sched_setscheduler error errno = %s(%d)\n",
                                      strerror(errno),errno);
                }
        }
}

void
RealtimeOption::PrintPriorityTbl() {

        FormatDebug(realtime_domain,
                    "enable_rtopt: %d  enable_memlock: %d   stack_reserve: %ld  heap_reserve: %ld\n",
                    enable_rtopt,enable_memlock,stack_reserve,heap_reserve);

        for ( auto &x : priority_tbl ) {
                FormatDebug(realtime_domain,
                            "thread name: '%s'  policy: %s  priority: %d  timerslack: %ld\n",
                            x.second->name.c_str(),
                            GetPolicyName(x.second->policy).c_str(),
                            x.second->priority,
                            x.second->timerslack);
        }
}

/**
 *
 */
void RealtimeOption::Initialize(const ConfigData& config) {
        SetParameter(config);
        if ( !isEnableRt() ) {
                return;
        }
        SetOutputPriority(config);
        ResetLimit();
        PrintPriorityTbl();
}


void RealtimeOption::LockMemory() {
        void *ptr = NULL;

        if ( !isEnableRt() ) {
                FormatDebug(realtime_domain,
                            "LockMemory: realtime_option disabled");
                return;
        }

        if ( stack_reserve != (size_t)0 ) {
                FormatDebug(realtime_domain,
                         "LockMemory: stack_reserve %ld",stack_reserve);
                bzero(alloca(stack_reserve), stack_reserve);
        }

        if ( heap_reserve != (size_t)0 ) {
                FormatDebug(realtime_domain,
                         "LockMemory: heap_reserve %ld",heap_reserve);
                ptr = malloc(heap_reserve);
                if ( ptr != NULL ) {
                        bzero(ptr, heap_reserve);
                        free(ptr);
                } else {
                        FormatFatalError("LockMemory: heap allocate error reserved size = %d",
                                         heap_reserve);
                }
        }

        if ( !isEnableMemLock() ) {
                FormatDebug(realtime_domain,
                         "LockMemory: memlock disabled");
                return;
        }

        FormatDebug(realtime_domain,"LockMemory: mlockall");
        int stat = mlockall(MCL_CURRENT);
        if ( stat < 0 ) {
                FormatFatalError("LockMemory: mlockall error errno = %d(%s)\n",
                                 errno,strerror(errno));
        }
}

/**
 *
 */
void RealtimeOption::ChangeThreadPriority(const char *name) {
        if ( !isEnableRt() ) {
                return;
        }

        const PriorityDef *pdef = GetPriorityParam(name);
        if ( pdef == nullptr ) {
                FormatDebug(realtime_domain,
                         "ChangeThreadPriority: name not found name = '%s'\n",name);
                return;
        }

        if ( !pdef->IsEnable() ) {
                return;
        }

        FormatDebug(realtime_domain,
                 "ChangeThreadPriority: name %s   policy %s(%d)  priority %d\n",
                    pdef->name.c_str(), GetPolicyName(pdef->policy).c_str(),pdef->policy, pdef->priority);

        ChangePriority(pdef);
        unsigned long ts = pdef->getTimerSlack();
        if (ts != 0ul ) {
                FormatDebug(realtime_domain,
                            "SetThreadTimerSlackNS: name %s   policy %s(%d)  timerslack %ld\n",
                            pdef->name.c_str(),
                            GetPolicyName(pdef->policy).c_str(),pdef->policy, pdef->timerslack);
                SetThreadTimerSlackNS(ts);
        }
}

#endif /* ENABLE_RTOPT */
