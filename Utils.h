/* 
 * File:   Utils.h
 * Author: d3s
 *
 * Created on July 23, 2012, 3:42 PM
 */

#ifndef UTILS_H
#define	UTILS_H

#include <math.h>
#include <vector>
#include <time.h>

#include <iostream>
#include <sstream>

#include "../SMS/ADARA.h"

class Statistics
{
public:
    Statistics() : m_n(0), m_old_M(0.0), m_new_M(0.0), m_S(0.0), m_min(0.0), m_max(0.0)
    {}

    void push(double x)
    {
        ++m_n;

        if ( m_n == 1 )
        {
            m_old_M = m_new_M = m_min = m_max = x;
        }
        else
        {
            m_new_M = m_old_M + (x - m_old_M)/m_n;
            m_S = m_S + (x - m_old_M)*(x - m_new_M);
            m_old_M = m_new_M;

            if ( x < m_min )
                m_min = x;
            else if ( x > m_max )
                m_max = x;
        }
    }

    inline uint64_t    count() const { return m_n; }
    inline double      mean() const { return m_new_M; }
    inline double      variance() const { return m_n>1?m_S/(m_n-1):0.0; }
    inline double      stdDev() const { return m_n>1?sqrt(variance()):0.0; }
    inline double      min() const { return m_min; }
    inline double      max() const { return m_max; }

    void reset()
    {
        m_n     = 0;
        m_old_M = 0.0;
        m_new_M = 0.0;
        m_S     = 0.0;
        m_min   = 0.0;
        m_max   = 0.0;
    }

    friend std::ostream & operator<<( std::ostream &os, const Statistics &stats )
    {
        os << "min: " << stats.min();
        os << " max: " << stats.max();
        os << " avg: " << stats.mean();
        os << " var: " << stats.variance();
        os << " dev: " << stats.stdDev();
        return os;
    }

private:
    short       m_mode;
    uint64_t    m_n;
    double      m_old_M;
    double      m_new_M;
    double      m_S;
    double      m_min;
    double      m_max;
};



inline uint64_t timespec_to_nsec( const struct timespec &ts )
{
    return ts.tv_sec*1000000000LL + ts.tv_nsec;
}


inline unsigned long difftime( struct timespec &t2, struct timespec &t1 )
{
    return ((t2.tv_sec*1000)+(t2.tv_nsec/1000000)) - ((t1.tv_sec*1000) + (t1.tv_nsec/1000000));
}

inline double
calcDiffSeconds( const struct timespec &ts1, const struct timespec &ts2 )
{
    double elapsed = ts1.tv_sec - ts2.tv_sec;

    if ( ts1.tv_nsec >= ts2.tv_nsec )
        elapsed += (ts1.tv_nsec - ts2.tv_nsec)*1.0e-9;
    else
    {
        elapsed -= 1;
        elapsed += (ts1.tv_nsec + 1.0e9 - ts2.tv_nsec)*1.0e-9;
    }

    return elapsed;
}

inline std::string
timeToISO8601( const struct timespec &ts )
{
    time_t time = ts.tv_sec;
    struct tm *timeinfo = localtime(&time);

    char date[100];
    strftime(date, sizeof(date), "%Y-%m-%dT%X%z", timeinfo);

    return std::string(date);
}


inline std::string
genTempName()
{
    time_t now = time(0);
    tm *ltm = localtime(&now);
    std::stringstream ss;

    ss << (1900 + ltm->tm_year) << "-";
    ss.width(2);
    ss.fill('0');
    ss << (1 + ltm->tm_mon) << "-";
    ss.width(2);
    ss << ltm->tm_mday << "-";
    ss.width(2);
    ss << ltm->tm_hour;
    ss.width(2);
    ss << ltm->tm_min;
    ss.width(2);
    ss << ltm->tm_sec;
    ss << "_" << getpid();

    return ss.str();
}


#endif	/* UTILS_H */

