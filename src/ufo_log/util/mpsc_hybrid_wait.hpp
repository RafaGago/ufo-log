/*
The BSD 3-clause license
--------------------------------------------------------------------------------
Copyright (c) 2014 Rafael Gago Castano. All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
 are permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.

   3. Neither the name of the copyright holder nor the names of its contributors
      may be used to endorse or promote products derived from this software
      without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY RAFAEL GAGO CASTANO "AS IS" AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
SHALL RAFAEL GAGO CASTANO OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

The views and conclusions contained in the software and documentation are those
of the authors and should not be interpreted as representing official policies,
either expressed or implied, of Rafael Gago Castano.
--------------------------------------------------------------------------------
*/

#ifndef MAL_MPSC_HYBRID_WAIT_HPP_
#define MAL_MPSC_HYBRID_WAIT_HPP_

#include <ufo_log/util/integer.hpp>
#include <ufo_log/util/atomic.hpp>
#include <ufo_log/util/chrono.hpp>
#include <ufo_log/util/thread.hpp>
#include <ufo_log/util/mpsc_hybrid_wait_cfg.hpp>

namespace ufo {

//------------------------------------------------------------------------------
// This class implements an hybrid lock to work with a lock-free queue. It is
// to be used under the next assumtions:
//
// -There is just a (worker) thread that waits (calls block() and reset()).
// -Many threads can call unblock().
// -The unblock() call is better to be avoided when you are sure that the worker
//  is unblocked.
//
// If low latency in the worker (at the expense of some CPU) is needed set the
// "never_block" parameter
//------------------------------------------------------------------------------
class mpsc_hybrid_wait
{
public:
    //--------------------------------------------------------------------------
     mpsc_hybrid_wait()
    {
        m_cfg.spin_max    = 50000;
        m_cfg.yield_max   = 200;
        m_cfg.never_block = false;
        m_cfg.block_us    = 2000;
        reset();
    }
    //--------------------------------------------------------------------------
    void reset()
    {
        m_spins  = 0;
        m_yields = 0;
        m_state  = unblocked;
    }
    //--------------------------------------------------------------------------
    void unblock()
    {
        m_state = unblocked;
        m_cond.notify_one();
    }
    //--------------------------------------------------------------------------
    bool block()
    {
        if (m_spins < m_cfg.spin_max)
        {
            ++m_spins;
            return false;
        }
        else if (m_yields < m_cfg.yield_max)
        {
            m_yields += ((!m_cfg.never_block) ? 1 : 0);
            th::this_thread::yield();
            return false;
        }
        else
        {
            auto pred = [&]() -> bool
            {
                auto state = m_state.load (mo_relaxed);
                if (state == blocked)
                {
                    th::this_thread::yield();                                   //waiting an extra timeslice in case there was no memory visibility for the changes made (a case were the scheduler gives control to this thread just after the notify call). If this was a spurious wakeup it won't hurt either, as the mutex is a dummy. We still can miss the user notificatio and then wait for the whole timeout, but I think that if it happens it won't be frequently.
                    state = m_state.load (mo_relaxed);
                }
                return state == unblocked;
            };
            m_state.store (blocked, mo_relaxed);
            bool is_unblocked = m_cond.wait_for(                                 //note that it's possible that the worker misses a notification and waits the whole timeout, this is by design and preferable to mutex locking the callers. if you can't tolerate this latency in the worker you can throw CPU at it by setting cfg never_block
                                    m_dummy_mutex,
                                    ch::microseconds (m_cfg.block_us),
                                    pred
                                    );
            assert (!is_unblocked || m_state.load (mo_relaxed) == unblocked);
            m_state.store (unblocked, mo_relaxed);
            return !is_unblocked;
        }
    }
    //--------------------------------------------------------------------------
    bool would_block_now_hint() const                                           //can give false positives
    {
        return (m_spins >= m_cfg.spin_max) && (m_yields >= m_cfg.yield_max);
    }
    //--------------------------------------------------------------------------
    bool never_blocks() const
    {
        return m_cfg.never_block;
    }
    //--------------------------------------------------------------------------
    mpsc_hybrid_wait_cfg get_cfg() const
    {
        return m_cfg;
    }
    //--------------------------------------------------------------------------
    void set_cfg (const mpsc_hybrid_wait_cfg& cfg)
    {
        m_cfg           = cfg;
        m_cfg.yield_max = (m_cfg.never_block) ? (uword) -1 : m_cfg.yield_max;
    }
    //--------------------------------------------------------------------------
private:
    //--------------------------------------------------------------------------
    enum state
    {
        unblocked,
//        will_block,
        blocked,
    };
    //--------------------------------------------------------------------------
    th::mutex                  m_dummy_mutex;
    th::condition_variable_any m_cond;
    mpsc_hybrid_wait_cfg       m_cfg;
    uword                      m_spins;
    uword                      m_yields;
    at::atomic<uword>          m_state;
};
 //-----------------------------------------------------------------------------
} //namespace ufo

#endif /* MAL_MPSC_HYBRID_WAIT_HPP_ */
