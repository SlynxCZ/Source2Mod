/**
* vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * Copyright (C) 2025-2026 Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl.
 * All rights reserved.
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
 * gives you permission to link the code of this program
 * (as well as its derivative works) to "Counter-Strike 2," "Source 2,"
 * "Steam," and any Game MODs or server software running on software by
 * Valve Corporation. You must obey the GNU General Public License in all
 * respects for all other code used.
 *
 * Additionally, this exception applies to all derivative works unless
 * otherwise stated in LICENSE.txt.
 *
 * Authors:
 *   - Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
 *
 * Project: Source2Mod
 */
#include "scheduler.h"

#include "shared.h"
#include "slowguard.h"
#include "tkvprof.h"

#include <engine2/gameshareddefs.h>

#include <algorithm>
#include <mutex>
#include <queue>

bool g_bHasTicked = false;
double g_dUniversalTime = 0.0;
double g_dLastTickTime = 0.0;
double g_dTimerNextThink = 0.0;

namespace
{
    // Who queued it, so RemoveAllForPlugin() can throw away the tasks of a
    // plugin that is going away before its library is closed.
    struct NextFrameTask
    {
        PluginId owner = 0;
        std::function<void()> task;
    };

    std::vector<Timer*> once_off_timers;
    std::vector<Timer*> repeat_timers;
    std::mutex nextFrameMutex;
    std::queue<NextFrameTask> nextFrameQueue;
}

namespace scheduler
{
    Scheduler schedulerManager;

    void Scheduler::NextFrame(PluginId owner, std::function<void()>&& task)
    {
        std::lock_guard lock(nextFrameMutex);
        nextFrameQueue.emplace(NextFrameTask{ owner, std::move(task) });
    }

    Timer* Scheduler::AddTimer(PluginId owner, float interval, TimerCallback callback, int flags)
    {
        Timer* timer = new Timer(owner, interval, g_dUniversalTime + interval, std::move(callback), flags);

        if (flags & TIMER_FLAG_REPEAT)
            repeat_timers.push_back(timer);
        else
            once_off_timers.push_back(timer);

        return timer;
    }

    void Scheduler::KillTimer(Timer* timer)
    {
        if (!timer) return;

        auto killFrom = [](std::vector<Timer*>& list, Timer* target)
        {
            auto it = std::remove_if(list.begin(), list.end(), [=](Timer* t) { return t == target; });
            if (it != list.end())
            {
                delete target;
                list.erase(it, list.end());
            }
        };

        if (timer->InExec)
        {
            timer->KillMe = true;
            return;
        }

        if (timer->Flags & TIMER_FLAG_REPEAT)
            killFrom(repeat_timers, timer);
        else
            killFrom(once_off_timers, timer);
    }

    void Scheduler::RemoveAllForPlugin(PluginId id)
    {
        auto purge = [id](std::vector<Timer*>& list)
        {
            for (int i = static_cast<int>(list.size()) - 1; i >= 0; --i)
            {
                Timer* timer = list[i];

                if (timer->Owner != id)
                    continue;

                // Same rule KillTimer follows: a callback that is running right
                // now still owns the object, so leave it to Tick, which drops
                // it the moment the callback returns.
                if (timer->InExec)
                {
                    timer->KillMe = true;
                    continue;
                }

                delete timer;
                list.erase(list.begin() + i);
            }
        };

        purge(once_off_timers);
        purge(repeat_timers);

        std::lock_guard lock(nextFrameMutex);
        std::queue<NextFrameTask> kept;

        while (!nextFrameQueue.empty())
        {
            if (nextFrameQueue.front().owner != id)
                kept.emplace(std::move(nextFrameQueue.front()));

            nextFrameQueue.pop();
        }

        std::swap(nextFrameQueue, kept);
    }

    void Init()
    {
        g_dUniversalTime = 0.0;
        g_dLastTickTime = 0.0;
        g_dTimerNextThink = 0.0;
    }

    void Shutdown()
    {
        for (auto* timer : once_off_timers)
            delete timer;
        for (auto* timer : repeat_timers)
            delete timer;
        once_off_timers.clear();
        repeat_timers.clear();
        std::lock_guard lock(nextFrameMutex);
        std::queue<NextFrameTask> empty;
        std::swap(nextFrameQueue, empty);
    }

    void Tick(bool simulating)
    {
        TK_VPROF("Source2Mod::Scheduler::Tick");

        // Taken one at a time rather than swapping the queue out, so a task
        // that unloads a plugin -- the file watcher's reload comes through
        // here -- takes the rest of that plugin's tasks with it instead of
        // leaving them in a local copy the purge cannot reach. The count is
        // read up front, so a task queueing another one does not spin here.
        size_t pending;
        {
            std::lock_guard lock(nextFrameMutex);
            pending = nextFrameQueue.size();
        }

        for (size_t i = 0; i < pending; ++i)
        {
            NextFrameTask task;
            {
                std::lock_guard lock(nextFrameMutex);

                if (nextFrameQueue.empty())
                    break;

                task = std::move(nextFrameQueue.front());
                nextFrameQueue.pop();
            }

            try
            {
                ::slow::Guard slowGuard("next-frame task", "", task.owner);
                task.task();
            }
            catch (...)
            {
            }
        }

        // Game time: the engine's curtime, summed frame by frame so it never
        // goes back. curtime starts again at every map while timers outlive
        // maps, so a step backwards (or the first frame) counts as one frame.
        // g_dLastTickTime is the curtime of the last frame, 0 before the first.
        CGlobalVars* globals = shared::getGlobalVars();
        if (simulating && globals)
        {
            const double now = globals->curtime;
            double step = now - g_dLastTickTime;
            if (g_dLastTickTime <= 0.0 || step < 0.0)
                step = globals->frametime > 0.0f ? globals->frametime : 0.015;

            g_dUniversalTime += step;
            g_dLastTickTime = now;
        }
        else
        {
            g_dUniversalTime += 0.015;
            if (globals)
                g_dLastTickTime = globals->curtime;
        }

        if (g_dUniversalTime < g_dTimerNextThink)
            return;

        for (int i = static_cast<int>(once_off_timers.size()) - 1; i >= 0; --i)
        {
            Timer* timer = once_off_timers[i];
            if (g_dUniversalTime >= timer->ExecTime)
            {
                timer->InExec = true;
                try
                {
                    {
                        ::slow::Guard slowGuard("timer", "", timer->Owner);
                        timer->Callback();
                    }
                }
                catch (...)
                {
                }
                delete timer;
                once_off_timers.erase(once_off_timers.begin() + i);
            }
        }

        for (int i = static_cast<int>(repeat_timers.size()) - 1; i >= 0; --i)
        {
            Timer* timer = repeat_timers[i];
            if (g_dUniversalTime >= timer->ExecTime)
            {
                timer->InExec = true;
                try
                {
                    {
                        ::slow::Guard slowGuard("timer", "", timer->Owner);
                        timer->Callback();
                    }
                }
                catch (...)
                {
                }

                if (timer->KillMe)
                {
                    delete timer;
                    repeat_timers.erase(repeat_timers.begin() + i);
                    continue;
                }

                timer->InExec = false;

                // From when it was due, not from when it was noticed: the
                // checks run every 0.1 s, and counting from the check made a
                // 1 s timer fire every ~1.09 s. A timer left far behind (a
                // hitch) starts again from now instead of firing in a burst.
                timer->ExecTime += timer->Interval;
                if (timer->ExecTime <= g_dUniversalTime)
                    timer->ExecTime = g_dUniversalTime + timer->Interval;
            }
        }

        g_dTimerNextThink = g_dUniversalTime + 0.1;
    }

    void RemoveMapChangeTimers()
    {
        auto removeFrom = [](std::vector<Timer*>& list)
        {
            for (int i = static_cast<int>(list.size()) - 1; i >= 0; --i)
            {
                Timer* t = list[i];
                if (t->Flags & TIMER_FLAG_NO_MAPCHANGE)
                {
                    delete t;
                    list.erase(list.begin() + i);
                }
            }
        };

        removeFrom(once_off_timers);
        removeFrom(repeat_timers);
    }
}
