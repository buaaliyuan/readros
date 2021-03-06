/*
 * Copyright (C) 2009, Willow Garage, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the names of Willow Garage, Inc. nor the names of its
 *     contributors may be used to endorse or promote products derived from
 *     this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "ros/poll_manager.h"
#include "ros/common.h"

#include <signal.h>

namespace ros
{

const PollManagerPtr& PollManager::instance()
{
  //返回常量引用的智能指针
  static PollManagerPtr poll_manager = boost::make_shared<PollManager>();
  return poll_manager;
}

PollManager::PollManager()
  : shutting_down_(false)
{
}

PollManager::~PollManager()
{
  shutdown();
}

void PollManager::start()
{
  shutting_down_ = false;
  thread_ = boost::thread(&PollManager::threadFunc, this);
}

void PollManager::shutdown()
{
  if (shutting_down_) return;

  shutting_down_ = true;
  
  //判断是否在同一个线程结束自己，防止死锁
  if (thread_.get_id() != boost::this_thread::get_id())
  {
    thread_.join();
  }

  boost::recursive_mutex::scoped_lock lock(signal_mutex_);
  poll_signal_.disconnect_all_slots();
}

void PollManager::threadFunc()
{
  disableAllSignalsInThisThread();

  while (!shutting_down_)
  {
    {
      boost::recursive_mutex::scoped_lock lock(signal_mutex_);
	  //调用绑定的槽函数
      poll_signal_();
    }
	
	//可实现尽快退出
    if (shutting_down_)
    {
      return;
    }
	//调用poll的回调函数
    poll_set_.update(100);
  }
}

boost::signals2::connection PollManager::addPollThreadListener(const VoidFunc& func)
{
  boost::recursive_mutex::scoped_lock lock(signal_mutex_);
  //绑定函数到这个信号上
  return poll_signal_.connect(func);
}

void PollManager::removePollThreadListener(boost::signals2::connection c)
{
  boost::recursive_mutex::scoped_lock lock(signal_mutex_);
  c.disconnect();
}

}
