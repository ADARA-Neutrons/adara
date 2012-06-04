#ifndef SYNCDEQUE
#define SYNCDEQUE

#include <boost/thread/mutex.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/thread/locks.hpp>
#include <deque>

template<class T>
class SyncDeque
{
public:
    SyncDeque()
    {}

    ~SyncDeque()
    {}

    inline size_t size() const { return m_que.size(); }

    T get()
    {
        T  ret;

        boost::unique_lock<boost::mutex> lock(m_mutex);
        while(1)
        {
            if ( m_que.size() )
            {
                ret = m_que.front();
                m_que.pop_front();
                break;
            }
            else
            {
                m_cvar.wait(lock);
            }
        }

        return ret;
    }

    void put(T val)
    {
        boost::lock_guard<boost::mutex> lock(m_mutex);

        m_que.push_back(val);
        if ( m_que.size() == 1 )
            m_cvar.notify_one();
    }

private:
    boost::mutex                m_mutex;
    boost::condition_variable   m_cvar;
    std::deque<T>               m_que;
};

#endif
