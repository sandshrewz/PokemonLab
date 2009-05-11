/* 
 * File:   ThreadedQueue.h
 * Author: Catherine
 *
 * Created on May 3, 2009, 6:57 AM
 *
 * This file is a part of Shoddy Battle.
 * Copyright (C) 2009  Catherine Fitzpatrick and Benjamin Gwin
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Affero General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program; if not, visit the Free Software Foundation, Inc.
 * online at http://gnu.org.
 */

#ifndef _THREADED_QUEUE_H_
#define _THREADED_QUEUE_H_

#include <boost/thread.hpp>
#include <boost/function.hpp>

namespace shoddybattle { namespace network {

/**
 * This is a class used for running code on a dispatch thread. Client threads
 * can post messages of type T to the ThreadedQueue, and the ThreadedQueue
 * will call a delegate method on each message. The ThreadedQueue does not
 * actually have any internal storage: attempts to post a message while one is
 * presently being processed while block until the message can be posted.
 */
template <class T>
class ThreadedQueue {
public:
    typedef boost::function<void (T &)> DELEGATE;

    ThreadedQueue(DELEGATE delegate):
            m_delegate(delegate),
            m_empty(true),
            m_terminated(false),
            m_thread(boost::bind(&ThreadedQueue::process, this)) { }

    void post(T elem) {
        boost::unique_lock<boost::mutex> lock(m_mutex);
        while (!m_empty) {
            m_condition.wait(lock);
        }
        m_item = elem;
        m_empty = false;
        lock.unlock();
        m_condition.notify_one();
    }

    void terminate() {
        boost::unique_lock<boost::mutex> lock(m_mutex);
        if (!m_terminated) {
            while (!m_empty) {
                m_condition.wait(lock);
            }
            m_thread.interrupt();
            m_terminated = true;
        }
    }

    ~ThreadedQueue() {
        terminate();
    }

private:

    void process() {
        boost::unique_lock<boost::mutex> lock(m_mutex);
        while (true) {
            while (m_empty) {
                m_condition.wait(lock);
            }
            m_delegate(m_item);
            m_empty = true;
            m_condition.notify_one();
        }
    }

    bool m_terminated;
    bool m_empty;
    DELEGATE m_delegate;
    T m_item;
    boost::mutex m_mutex;
    boost::condition_variable m_condition;
    boost::thread m_thread;
};

}} // namespace shoddybattle::network

#endif
