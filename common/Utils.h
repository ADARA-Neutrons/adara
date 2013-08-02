/*
 * File:   Utils.h
 * Author: d3s
 *
 * Created on July 23, 2012, 3:42 PM
 */

#ifndef UTILS_H
#define	UTILS_H

#include <math.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>
#include <time.h>
#include <stdint.h>
#include <string.h>
#include <iostream>
#include <iomanip>
#include <sstream>

#include "ADARA.h"

/*! \class Statistics
 *
 * The Statistics class calculates running min, max, mean, variance, and standard deviation for a sequence of values.
 */
class Statistics
{
public:
    /// Default constructor.
    Statistics() : m_n(0), m_old_M(0.0), m_new_M(0.0), m_S(0.0), m_min(0.0), m_max(0.0)
    {}

    /// Adds a value to the current sequence and updates running statistics
    void push
    (
        double a_value  ///< Value to add to sequence
    )
    {
        ++m_n;

        if ( m_n == 1 )
        {
            m_old_M = m_new_M = m_min = m_max = a_value;
        }
        else
        {
            m_new_M = m_old_M + (a_value - m_old_M)/m_n;
            m_S = m_S + (a_value - m_old_M)*(a_value - m_new_M);
            m_old_M = m_new_M;

            if ( a_value < m_min )
                m_min = a_value;
            else if ( a_value > m_max )
                m_max = a_value;
        }
    }

    /// Returns number of values in sequence
    inline uint64_t    count() const { return m_n; }
    /// Returns mean value of sequence
    inline double      mean() const { return m_new_M; }
    /// Returns variance of sequence
    inline double      variance() const { return m_n>1?m_S/(m_n-1):0.0; }
    /// Returns standard deviation of sequence
    inline double      stdDev() const { return m_n>1?sqrt(variance()):0.0; }
    /// Returns minimum value of sequence
    inline double      min() const { return m_min; }
    /// Returns maximum value of sequence
    inline double      max() const { return m_max; }

    /// Resets sequence and statistics to initial state
    void reset()
    {
        m_n     = 0;
        m_old_M = 0.0;
        m_new_M = 0.0;
        m_S     = 0.0;
        m_min   = 0.0;
        m_max   = 0.0;
    }

    /// Output one-line output of statistics in human-readable format
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
    uint64_t    m_n;        ///< Number of values in sequence
    double      m_old_M;    ///< Previous mean value
    double      m_new_M;    ///< Current mean value
    double      m_S;        ///< Running sum of squared deviations
    double      m_min;      ///< Minimum value
    double      m_max;      ///< Maximum value
};


/*! \brief Converts a timespec struct to nanoseconds
 *  \return Time value in nanoseconds
 */
inline uint64_t timespec_to_nsec
(
    const struct timespec &a_ts     ///< [in] Time value to convert
)
{
    return a_ts.tv_sec*1000000000LL + a_ts.tv_nsec;
}


/*! \brief Converts a time offset in nanoseconds to a timespec struct
 *  \return Time value as a timespec
 */
inline struct timespec nsec_to_timespec
(
    uint64_t a_nsec ///< [in] Nanosecond offset value to convert
)
{
    struct timespec ts;

    ts.tv_sec = a_nsec / 1000000000LL;
    ts.tv_nsec = a_nsec - (ts.tv_sec*1000000000LL);

    return ts;
}


/*! \brief Calcultes T1 - T2
 *  \return Difference in microseconds
 */
inline double
calcDiffSeconds
(
    const struct timespec &a_ts1,   ///< [in] Base time value
    const struct timespec &a_ts2    ///< [in] Time value to subtract from base
)
{
    return ( a_ts1.tv_sec + ( a_ts1.tv_nsec * 1.0e-9 )) - ( a_ts2.tv_sec + ( a_ts2.tv_nsec * 1.0e-9 ));
}


/*! \brief Converts timespec to ISO8601 string format
 *  \return ISO8601 time in UTC with local offset as a string
 */
inline std::string
timeToISO8601
(
    const struct timespec &a_ts    ///< [in] Time value to format into ISO8601
)
{
    // Time must be in Unix epoch for strftime to work
    time_t              time = a_ts.tv_sec;
    struct tm          *timeinfo = localtime(&time);
    std::stringstream   result;
    char                date[100];

    strftime(date, sizeof(date), "%Y-%m-%dT%X", timeinfo);

    result << date << "." << std::right << std::setw(9) << std::setfill('0') << a_ts.tv_nsec;

    strftime(date, sizeof(date), "%z", timeinfo);

    unsigned long len = strlen( date );

    if ( len == 5 ) // If no ':' present (i.e. -0100)
    {
        result << date[0] << date[1] << date[2] << ":" << date[3] << date[4];
    }
    else if ( len == 3 ) // Short version (i.e. -01)
    {
        result << date << ":00";
    }
    else
    {
        result << date;
    }

    return result.str();
}


/*! \brief Creates a temporary filename.
 *  \return Temporary filename as string
 */
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

