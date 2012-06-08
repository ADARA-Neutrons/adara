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
        : m_active(true)
    {}

    ~SyncDeque()
    {
        m_active = false;
        m_cvar.notify_all();
    }

    inline size_t size() const { return m_que.size(); }

    bool get( T& a_item )
    {
        //T  ret;

        boost::unique_lock<boost::mutex> lock(m_mutex);
        while( m_active )
        {
            if ( m_que.size() )
            {
                a_item = m_que.front();
                m_que.pop_front();
                return true;
            }
            else
            {
                m_cvar.wait(lock);
            }
        }

        return false;
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
    bool                        m_active;
};

#endif
