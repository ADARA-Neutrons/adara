#ifndef TRACEEXCEPTION_H
#define TRACEEXCEPTION_H

#include <list>
#include <sstream>

/*! \class TraceException
 *  \brief Exception class provideing stack-trace-like context capturing
 *
 * The TraceException class provides a general error reporting and context-capturing feature to enable more useful
 * logging of run-time errors. Rather than simply logging a low-level error, the TraceException can be propogated up
 * the call stack where handlers can remap low-level error codes to higher-level error codes add useful context
 * information at each level. Once the TraceException reaches the top-level handler, the full error context can be
 * output appropriately. This class is a "heavy" exception class - meaning it shouldn't be used for non-exceptional
 * conditions, or for resource error reporting (i.e. out of memory). Macros are provided to permit easy creation of
 * TraceExceptions using stream semantics.
 */
class TraceException
{
public:
    /// Default constructor
    TraceException
    (
        long                a_error_code,   ///< [in] Generic error value
        const std::string  &a_context,      ///< [in] Human-readble error message
        const char         *a_file,         ///< [in] Filename (from __FILE__ macro)
        unsigned long       a_line          ///< [in] Linenumber (from __LINE__ macro)
    )
        : m_error_code(a_error_code)
    {
        addContext( a_context, a_file, a_line );
    }

    /// Destructor
    virtual ~TraceException() {}

    /// Adds error context information to the exception.
    void addContext
    (
        const std::string  &a_context,      ///< [in] Human-readble error message
        const char         *a_file = "",    ///< [in] Filename (from __FILE__ macro)
        unsigned long       a_line = 0      ///< [in] Linenumber (from __LINE__ macro)
    )
    {
        m_context.push_front( ExceptionContext( a_context, a_file, a_line ));
    }

    /// Generates output string from error context information
    std::string toString
    (
        bool a_trace = true,    ///< [in] Controls full (true) or most recent context output
        bool a_debug = true     ///< [in] If true, includes file and linenumber information
    ) const
    {
        std::string err_string = std::string("Trace exception (code ")
            + boost::lexical_cast<std::string>( m_error_code ) + "); ";

        if ( a_trace )
        {
            for ( std::list<ExceptionContext>::const_iterator ic = m_context.begin(); ic != m_context.end(); ++ic )
            {
                if ( ic != m_context.begin())
                    err_string += "; ";

                err_string += ic->context_msg;
                if ( a_debug )
                    err_string += " {" + ic->context_file + ":" + boost::lexical_cast<std::string>( ic->context_line ) + "}";
            }
        }
        else
        {
            std::list<ExceptionContext>::const_iterator ic = m_context.begin();

            err_string = ic->context_msg;
            if ( a_debug )
                err_string += " {" + ic->context_file + ":" + boost::lexical_cast<std::string>( ic->context_line ) + "}";
        }

        return err_string;
    }

    /// Retrieves most recent error code from context stack.
    long getErrorCode()
    {
        return m_error_code;
    }

private:
    /// Structure used to capture context information
    struct ExceptionContext
    {
        ExceptionContext( const std::string &a_msg, const std::string &a_file, unsigned long a_line )
            : context_msg(a_msg), context_file(a_file), context_line(a_line)
        {}

        std::string     context_msg;
        std::string     context_file;
        unsigned long   context_line;
    };

    long                            m_error_code;   ///< App defined error code
    std::list<ExceptionContext>     m_context;      ///< Error context "stack"
};


/// Macro to throw a trace exception using stream semantics for error message
#define THROW_TRACE(err_code,msg) \
{ \
    std::stringstream s; \
    s << msg; \
    throw TraceException( err_code, s.str(), __FILE__, __LINE__ ); \
}

/// Macro to rethrow a trace exception using stream semantics for error message
#define RETHROW_TRACE(e,msg) \
{ \
    std::stringstream s; \
    s << msg; \
    e.addContext( s.str(), __FILE__, __LINE__ ); \
    throw; \
}

#endif // TRACEEXCEPTION_H

// vim: expandtab

