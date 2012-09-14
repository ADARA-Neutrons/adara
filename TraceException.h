#ifndef TRACEEXCEPTION_H
#define TRACEEXCEPTION_H

#include <list>


class TraceException
{
public:
    TraceException( unsigned long a_error_code, const std::string & a_context, const char *a_file, unsigned long a_line )
    {
        addContext( a_error_code, a_context, a_file, a_line );
    }

    virtual ~TraceException() {}


    void addContext( long a_error_code, const std::string & a_context, const char * a_file = "", unsigned long a_line = 0 )
    {
        m_context.push_front( ExceptionContext( a_error_code, a_context, a_file, a_line ));
    }


    std::string toString( bool a_trace = true, bool a_verbose = true ) const
    {
        std::string err_string;

        if ( a_trace )
        {
            for ( std::list<ExceptionContext>::const_iterator ic = m_context.begin(); ic != m_context.end(); ++ic )
            {
                if ( ic != m_context.begin())
                    err_string += "\n";

                err_string += std::string("Err ") + boost::lexical_cast<std::string>( ic->error_code) + ": " + ic->context_msg;
                if ( a_verbose )
                    err_string += " {" + ic->context_file + ":" + boost::lexical_cast<std::string>( ic->context_line ) + "}";
            }
        }
        else
        {
            std::list<ExceptionContext>::const_iterator ic = m_context.begin();

            err_string = std::string("Err ") + boost::lexical_cast<std::string>( ic->error_code) + ": " + ic->context_msg;
            if ( a_verbose )
                err_string += " {" + ic->context_file + ":" + boost::lexical_cast<std::string>( ic->context_line ) + "}";
        }

        return err_string;
    }

    long getErrorCode()
    {
        return m_context.front().error_code;
    }

private:
    struct ExceptionContext
    {
        ExceptionContext( long a_error_code, const std::string &a_context_msg, const std::string &a_context_file, unsigned long a_context_line)
            : error_code(a_error_code), context_msg(a_context_msg), context_file(a_context_file), context_line(a_context_line)
        {}

        long            error_code;
        std::string     context_msg;
        std::string     context_file;
        unsigned long   context_line;
    };

    std::list<ExceptionContext>     m_context;
};


//#define EXC(err_code,msg) throw TraceException( err_code, msg, __FUNCTION__, __LINE__ )

#define THROW_TRACE(err_code,msg) \
{ \
    std::stringstream s; \
    s << msg; \
    throw TraceException( err_code, s.str(), __FUNCTION__, __LINE__ ); \
}

#define RETHROW_TRACE(e,err_code,msg) \
{ \
    std::stringstream s; \
    s << msg; \
    e.addContext( err_code, s.str(), __FUNCTION__, __LINE__ ); \
    throw; \
}



#endif // TRACEEXCEPTION_H
