/*
 * File:   ADARAUtils.h
 * Author: d3s
 *
 * Created on July 23, 2012, 3:42 PM
 */

#ifndef ADARA_UTILS_H
#define ADARA_UTILS_H

#include <math.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <map>
#include <vector>
#include <time.h>
#include <stdint.h>
#include <string.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>
#include <errno.h>

#include "ADARA.h"

// Macro to cut down on compiler warnings.
// from Dr. Martin Ettl article:
//     https://sites.google.com/site/opensourceconstriubtions/
//     ettl-martin-1/articles/suppress-unused-parameter-compiler-warning
#if 1  // there should be no more any compilers needing the "#else" version
	#define UNUSED(x) /* x */
#else  // stupid, broken compiler
	#define UNUSED(x) x
#endif

/*! \class Statistics
 *
 * The Statistics class calculates running min, max, mean, variance, and standard deviation for a sequence of values.
 *
 * NOTE: "Variance" here is really the "Bessell-Corrected Sample Variance".
 * See: https://en.wikipedia.org/wiki/Bessel%27s_correction
 *    -> i.e. ( sum of the squares of ( value - mean ) ) / ( n - 1 )
 * However, most of these pure mathematical formulas are computationally
 * unstable in cases of small variance. The funky algorithm used here
 * is actually Welford's from 1962, as presented by Donald Knuth.
 * An interesting article about the stability of this algorithm is at:
 *    https://www.johndcook.com/blog/standard_deviation/
 * I tried doing better than this, but it's a mess. We'll keep it as is!
 */
class Statistics
{
public:
	/// Default constructor.
	Statistics() : m_n(0), m_old_M(0.0), m_new_M(0.0), m_S(0.0),
		m_min(0.0), m_max(0.0)
	{}

	/// Adds a value to the current sequence and updates running statistics
	void push
	(
		double a_value   ///< Value to add to sequence
	)
	{
		++m_n;

		if ( m_n == 1 )
		{
			m_old_M = m_new_M = m_min = m_max = a_value;
		}

		else
		{
			m_new_M = m_old_M + ( (a_value - m_old_M) / ((double) m_n) );
			m_S = m_S + ( (a_value - m_old_M) * (a_value - m_new_M) );
			m_old_M = m_new_M;

			if ( a_value < m_min )
				m_min = a_value;
			else if ( a_value > m_max )
				m_max = a_value;
		}
	}

	/// Returns number of values in sequence
	inline uint64_t	count() const { return m_n; }

	/// Returns mean value of sequence
	inline double	mean() const { return m_new_M; }

	/// Returns variance of sequence
	inline double	variance() const
		{ return ( ( m_n > 1 ) ? ( m_S / ((double) (m_n - 1)) ) : 0.0 ); }

	/// Returns standard deviation of sequence
	inline double	stdDev() const
		{ return ( ( m_n > 1 ) ? ( sqrt( variance() ) ) : 0.0 ); }

	/// Returns minimum value of sequence
	inline double	min() const { return m_min; }

	/// Returns maximum value of sequence
	inline double	max() const { return m_max; }

	/// Resets sequence and statistics to initial state
	void reset()
	{
		m_n		= 0;
		m_old_M	= 0.0;
		m_new_M	= 0.0;
		m_S		= 0.0;
		m_min	= 0.0;
		m_max	= 0.0;
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
	uint64_t	m_n;      ///< Number of values in sequence
	double		m_old_M;  ///< Previous mean value
	double		m_new_M;  ///< Current mean value
	double		m_S;      ///< Running sum of squared deviations
	double		m_min;    ///< Minimum value
	double		m_max;    ///< Maximum value
};


/* ---------------------------------------------------------------------- */
class Utils
{

public:

	/**
	 * @brief Sends data over socket/fd
	 * @param a_fd - file descriptor to write to
	 * @param a_buffer - data buffer to send
	 * @param a_len - length of data buffer
	 * @return true on success, false on error
	 **/
	static bool sendBytes( int a_fd, const char *a_buffer, size_t a_len,
		std::string & log_info )
	{
		ssize_t ec = 0;
		size_t bytes_sent = 0;

		while ( bytes_sent < a_len )
		{
			if (( ec = ::write( a_fd,
					a_buffer + bytes_sent, a_len - bytes_sent )) < 0 )
			{
				// Save errno locally
				int errn = errno;

				switch ( errn )
				{
					case EAGAIN:	// This shouldn't occur,
									// as socket should be blocking...
					case EINTR:
						continue;	// Try again

					default:
						log_info = "sendBytes() write() failed: ";
						log_info.append( strerror( errn ) );
						break;
				}

				return false;
			}
			else
			{
				bytes_sent += (size_t)ec;
			}
		}

		return true;
	}

	/**
	 * @brief Sanitizes Strings for Various Nefarious Purposes.
	 * @param a_str - string to sanitize
	 * @param a_preserve_uri - protect any "proto://host:service" syntax
	 * @param a_preserve_whitespace - protect PV values, don't break stuff
	 * Note: a_preserve_uri overrides a_preserve_whitespace setting,
	 *    because URIs can't have any whitespace.
	 * @return true if string changed/was sanitized, else return false
	 **/
	static bool sanitizeString( std::string & a_str,
			bool a_preserve_uri, bool a_preserve_whitespace = false )
	{
		std::string all_bad = " \t\'\"`,.;:<>[]{}()|/\\?~!@#$%^&*+=";
		std::string uri_bad = " \t\'\"`;<>[]{}|\\~!@#$%^*+";
		std::string all_values = "\'\"`;<>|?~!#$&*";

		std::string bad = ( a_preserve_uri ) ? uri_bad :
			( ( a_preserve_whitespace ) ? all_values : all_bad );

		size_t next, last;

		bool changed = false;

		last = 0;

		while ( (next = a_str.find_first_of( bad, last ))
				!= std::string::npos )
		{
			if ( next != last + 1 || last == 0 )
				a_str.replace( next, 1, "-" );   // replace bad with '-'...
			else {
				a_str.replace( next, 1, "" );   // just remove double-bad...
			}

			changed = true;

			last = next;
		}

		return( changed );
	}

	/**
	 * @brief Parses String List [1,2,3] into Vector of Numbers.
	 * @param a_str - string to parse
	 * @param a_array - vector in which to place numbers
	 **/
	static void parseArrayString( std::string & arrayStr,
			std::vector<uint32_t> & arrayVec )
	{
		// Inspired by Jilles De Wit on StackOverflow... ;-D

		std::string sep = "[, ]";

		uint32_t value;

		size_t b, e;

		arrayVec.clear();

		b = 0;

		while ( b < arrayStr.length() )
		{
			e = arrayStr.find_first_of( sep, b );

			if ( e == std::string::npos )
				e = arrayStr.length();

			// Discard Empty Tokens...
			if ( b != e )
			{
				std::istringstream buffer( arrayStr.substr(b, e - b) );
				buffer >> value;

				arrayVec.push_back( value );

				b = e + 1;
			}

			else b++;
		}
	}

	/**
	 * @brief Parses String List [1,2,3] into Vector of Numbers.
	 * @param a_str - string to parse
	 * @param a_array - vector in which to place numbers
	 **/
	static void parseArrayString( std::string & arrayStr,
			std::vector<double> & arrayVec )
	{
		// Inspired by Jilles De Wit on StackOverflow... ;-D

		std::string sep = "[, ]";

		double value;

		size_t b, e;

		arrayVec.clear();

		b = 0;

		while ( b < arrayStr.length() )
		{
			e = arrayStr.find_first_of( sep, b );

			if ( e == std::string::npos )
				e = arrayStr.length();

			// Discard Empty Tokens...
			if ( b != e )
			{
				std::istringstream buffer( arrayStr.substr(b, e - b) );
				buffer >> value;

				arrayVec.push_back( value );

				b = e + 1;
			}

			else b++;
		}
	}

	/**
	 * @brief Creates String List [1,2,3] from Vector of Numbers.
	 * @param a_array - vector of numbers
	 * @param a_str - string to create
	 **/
	static void printArrayString( std::vector<uint32_t> arrayVec,
			std::string & arrayStr )
	{
		std::stringstream ss;

		ss << "[";

		std::vector<uint32_t>::iterator v;

		bool first = true;

		for ( v=arrayVec.begin(); v != arrayVec.end(); ++v )
		{
			if ( first )
				first = false;
			else
				ss << ", ";

			ss << *v;
		}

		ss << "]";

		arrayStr = ss.str();
	}

	/**
	 * @brief Creates String List [1,2,3] from Vector of Numbers.
	 * @param a_array - vector of numbers
	 * @param a_str - string to create
	 **/
	static void printArrayString( std::vector<double> arrayVec,
			std::string & arrayStr )
	{
		std::stringstream ss;

		ss << "[";

		std::vector<double>::iterator v;

		bool first = true;

		for ( v=arrayVec.begin(); v != arrayVec.end(); ++v )
		{
			if ( first )
				first = false;
			else
				ss << ", ";

			ss << *v;
		}

		ss << "]";

		arrayStr = ss.str();
	}
};


/* ---------------------------------------------------------------------- */
class RateLimitedLogging {

public:

	typedef
		std::map<std::pair<uint32_t, std::string>, std::vector<int64_t> >
			History;

	/**
	 * @brief Rate-Limited Logging Method
	 * @param log_id - log message identification number (caller supplied)
	 * @param log_name - (optional) additional log originator name
	 * @param log_info - (any) rate-limited logging commentary to prepend
	 * @return true on ok-to-log, false on don't-log
	 **/
	static bool checkLog( History & log_history,
		const uint32_t log_id, const std::string & log_name,
		const uint32_t window_seconds,
		const uint32_t threshold, const uint32_t log_rate,
		std::string & log_info )
	{
		std::pair<uint32_t,std::string> log(log_id, log_name);

		log_info.clear();

		// Append Current Time/Occurrence...
		struct timespec ts;
		clock_gettime(CLOCK_REALTIME, &ts);

		History::iterator lh = log_history.find(log);

		if ( lh == log_history.end() )
		{
			std::vector<int64_t> new_time_vec;
			lh = log_history.insert( lh,
				std::pair<std::pair<uint32_t, std::string>,
					std::vector<int64_t> >
				(log, new_time_vec) );
		}

		lh->second.push_back( static_cast<int64_t>(ts.tv_sec) );

		// std::stringstream sss;
		// sss << "[checkLog(";
		// sss << log_id;
		// sss << ", ";
		// sss << log_name;
		// sss << ") time=";
		// sss << ts.tv_sec;
		// sss << " size=";
		// sss << lh->second.size();
		// sss << "] ";
		// log_info.append(sss.str());

		// *Clear* Any Old Timestamps... (Don't "Remove" Until Next Log!)
		uint32_t old = 0;
		while ( old < lh->second.size()
				&& ( ts.tv_sec - labs(lh->second[old]) ) > window_seconds )
		{
			// std::stringstream ss;
			// ss << "[Marking old time index=";
			// ss << old;
			// ss << " time=";
			// ss << lh->second[old];
			// ss << "] ";
			// log_info.append(ss.str());

			// Retain "Already Logged" Information...
			if ( lh->second[old] < 0 )
				lh->second[old] = -1;
			else
				lh->second[old] = 0;

			old++;
		}

		// See if Things have Calmed Down now...
		// If so, then Reset the Threshold & Flush the Timestamp Vector!
		bool resetThresh = false;
		if ( lh->second.size() >= threshold
				&& lh->second.size() - old < threshold )
		{
			std::stringstream ss;
			ss << "[*** Reset Threshold, Log Rate has Slowed! ";
			ss << old;
			ss << " Timestamps Marked Old out of ";
			ss << lh->second.size();
			ss << " Total Saved";
			if ( lh->second.size() == old + 1 )
				ss << " [ALL OLD]";
			ss << ", Now Under Threshold of ";
			ss << threshold;
			ss << ".] ";
			log_info.append(ss.str());

			resetThresh = true;
		}

		// Check Occurrences in Latest Window Interval...
		if ( lh->second.size() >= threshold && !resetThresh )
		{
			// While Thrashing, Still Log Every "Nth" One...
			if ( !( ( lh->second.size() - threshold ) % log_rate) )
			{
				// Count How Many "New" (Unlogged) Timestamps We Have...
				uint32_t newLogs = 0;
				int64_t ri = lh->second.size() - 1;
				while ( ri >= 0 && lh->second[ri] >= 0 ) {
					newLogs++; ri--;
				}

				// Mark This Most Recent Timestamp as "Logged"...
				lh->second[ lh->second.size() - 1 ] *= -1;

				// Log How Badly We're Thrashing on This Log Message...
				std::stringstream ss;
				ss << "[*** Rate-Limited Log: ";
				ss << lh->second.size() - old;
				ss << " Occurrences (";
				ss << newLogs;
				ss << " New) in Last ";
				ss << window_seconds;
				ss << " Seconds!";
				ss << " (thresh=";
				ss << threshold;
				ss << ", rate=";
				ss << log_rate;
				ss << ", old=";
				ss << old;
				ss << ")] ";
				log_info.append(ss.str());

				// _NOW_ Erase Any Old ("Cleared") Timestamps... ;-D
				// (Yet Only Erase in "Log Rate-Sized" Chunks, to
				// Prevent Getting "Stuck", i.e. 1 Erase per 1 New Log...)
				while ( old >= log_rate )
				{
					uint32_t cnt = 0;
					while ( lh->second.size() > 0
							&& ( lh->second[0] == 0 || lh->second[0] == -1 )
							&& cnt++ < log_rate )
					{
						// log_info.append("[Erasing old time.]");
						lh->second.erase( lh->second.begin() );
					}

					old -= log_rate;
				}

				return( true );
			}

			else {

				// Don't Log! Thrashing... ;-Q
				// std::stringstream ss;
				// ss <<"[DON'T Log: ";
				// ss << lh->second.size();
				// ss << " Occurrences in Last ";
				// ss << window_seconds;
				// ss << " Seconds!";
				// ss << " (thresh=";
				// ss << threshold;
				// ss << ", rate=";
				// ss << log_rate;
				// ss << ", old=";
				// ss << old;
				// ss << ")] ";
				// log_info.append(ss.str());

				return( false );
			}
		}

		// It's Ok, Just Log It.
		else {

			// Be Sure to Erase Any Old ("Cleared") Timestamps...! ;-D
			while ( lh->second.size() > 0
					&& ( lh->second[0] == 0 || lh->second[0] == -1 ) )
			{
				// log_info.append("[Erasing old time.]");
				lh->second.erase( lh->second.begin() );
			}

			// Mark This Most Recent Timestamp as "Logged"...
			lh->second[ lh->second.size() - 1 ] *= -1;

			// No rate-limited logging commentary required...
			// log_info.append("[Under Threshold, Log Normally.]");
			return( true );
		}
	}
};


// Nanoseconds Per Second Constants
#define NANO_PER_SECOND_LL (1000000000LL)
#define NANO_PER_SECOND_D (1000000000.0)


/*! \brief Converts a timespec struct to nanoseconds
 *  \return Time value in nanoseconds
 */
inline uint64_t timespec_to_nsec
(
	const struct timespec &a_ts   ///< [in] Time value to convert
)
{
	return( ( a_ts.tv_sec * NANO_PER_SECOND_LL ) + a_ts.tv_nsec );
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

	ts.tv_sec = a_nsec / NANO_PER_SECOND_LL;
	ts.tv_nsec = a_nsec - ( ts.tv_sec * NANO_PER_SECOND_LL );

	return ts;
}


/*! \brief Calculates Elapsed Time (Duration) = endTime - startTime
 *  \return Difference in seconds (up to nanosecond precision)
 */
inline double
calcDiffSeconds
(
	const struct timespec &a_endTime,    ///< [in] Ending Time value
	const struct timespec &a_startTime   ///< [in] Starting Time value
)
{
	return( ( ((double) a_endTime.tv_sec)
			+ ( ((double) a_endTime.tv_nsec) * 1.0e-9 ) )
		- ( ((double) a_startTime.tv_sec)
			+ ( ((double) a_startTime.tv_nsec) * 1.0e-9 ) ) );
}


/*! \brief Precisely Compares Two TimeSpec TimeStamps, Via "Time1 - Time2"
 *  \return Comparison: "Less Than" (-1), "Equal" (0) or "Greater Than" (+1)
 */
inline int32_t
compareTimeStamps
(
	const struct timespec &a_Time1,		///< [in] First Time value
	const struct timespec &a_Time2		///< [in] Second Time value
)
{
	uint64_t t1_nsec, t2_nsec;

	t1_nsec = ( a_Time1.tv_sec * NANO_PER_SECOND_LL ) + a_Time1.tv_nsec;
	t2_nsec = ( a_Time2.tv_sec * NANO_PER_SECOND_LL ) + a_Time2.tv_nsec;

	// Time1 is:

	// "Less Than" (-1)
	if ( t1_nsec < t2_nsec )
		return( -1 );

	// "Greater Than" (+1)
	else if ( t1_nsec > t2_nsec )
		return( 1 );

	// "Equal" (0)
	else
		return( 0 );
}


/*! \brief Converts timespec to ISO8601 string format
 *  \return ISO8601 time in UTC with local offset as a string
 */
inline std::string
timeToISO8601
(
	const struct timespec &a_ts   ///< [in] Time value to format into ISO8601
)
{
	// Time must be in Unix epoch for strftime to work
	time_t				time = a_ts.tv_sec;
	struct tm			*timeinfo = localtime(&time);
	std::stringstream	result;
	char				date[100];

	strftime(date, sizeof(date), "%Y-%m-%dT%X", timeinfo);

	result << date << "." << std::right << std::setw(9) << std::setfill('0') << a_ts.tv_nsec;

	strftime(date, sizeof(date), "%z", timeinfo);

	unsigned long len = strlen( date );

	if ( len == 5 ) // If no ':' present (i.e. -0100)
	{
		result << date[0] << date[1] << date[2]
			<< ":" << date[3] << date[4];
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


/*! \brief Compares Two Double Floating Point Values
 *
 * Can't Go Wrong with "The Art of Computer Programming" by Don Knuth!
 *
 *  \return True if the Values are "Approximately" Equal, else False.
 */
inline bool
approximatelyEqual
(
	const double a,        ///< [in] A Double Floating Point Value
	const double b,        ///< [in] Another Double Floating Point Value
	const double epsilon   ///< [in] Epsilon to Measure the Difference
)
{
	return( fabs( a - b )
		<= ( ( fabs(a) < fabs(b) ? fabs(b) : fabs(a) ) * epsilon ) );
}


#endif	/* ADARA_UTILS_H */

